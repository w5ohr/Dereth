#import <Foundation/Foundation.h>
#import <JavaScriptCore/JavaScriptCore.h>

#include "plugin_host.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <map>
#include <mutex>

#include "core/common.h"
#include "core/http.h"
#include "native.h"

namespace plugins {
namespace {

constexpr size_t kLogLimit = 60;

// Everything a loaded plugin owns. JSValue keeps its context alive, so the
// registered functions pin the context for as long as the plugin is loaded.
struct Plugin {
  PluginInfo info;
  JSContext* context = nil;
  NSMutableDictionary<NSString*, JSValue*>* commands = nil;    // name -> fn
  NSMutableDictionary<NSString*, NSString*>* commandDescriptions = nil;
  NSMutableDictionary<NSString*, JSValue*>* tools = nil;       // name -> fn
  std::map<std::string, Tool> toolSpecs;
  NSMutableArray<JSValue*>* beforeSend = nil;
  NSMutableDictionary<NSString*, JSValue*>* renderers = nil;   // language -> fn
  std::map<std::string, Renderer> rendererSpecs;
};

struct State {
  HostCallbacks callbacks;
  std::string directory;
  bool loaded = false;
  // `plugins` is only touched on the queue; the snapshot below is what the
  // UI reads, refreshed after every load / log line / registration.
  std::vector<std::unique_ptr<Plugin>> plugins;
  mutable std::mutex snapshotMutex;
  std::vector<PluginInfo> infoSnapshot;
  std::vector<Command> commandSnapshot;
  std::vector<Tool> toolSnapshot;
  std::vector<Renderer> rendererSnapshot;
  bool anyHooks = false;
  dispatch_queue_t queue = dispatch_queue_create("com.davidcochran.owui-desktop.plugins", DISPATCH_QUEUE_SERIAL);
};

State& state() {
  static State* s = new State();
  return *s;
}

NSString* ns(const std::string& s) { return [NSString stringWithUTF8String:s.c_str()]; }
std::string str(NSString* s) { return s ? std::string([s UTF8String]) : std::string(); }

// JSON <-> JSValue through Foundation objects.
JSValue* toJs(const Json& value, JSContext* context) {
  if (value.is_null()) return [JSValue valueWithNullInContext:context];
  NSData* data = [ns(value.dump()) dataUsingEncoding:NSUTF8StringEncoding];
  id object = [NSJSONSerialization JSONObjectWithData:data options:NSJSONReadingFragmentsAllowed error:nil];
  return object ? [JSValue valueWithObject:object inContext:context] : [JSValue valueWithUndefinedInContext:context];
}

Json fromJs(JSValue* value) {
  if (!value || value.isUndefined || value.isNull) return Json();
  if (value.isString) return Json(str(value.toString));
  if (value.isBoolean) return Json(value.toBool);
  if (value.isNumber) {
    const double d = value.toDouble;
    if (d == std::floor(d) && std::fabs(d) < 9e15) return Json(static_cast<int64_t>(d));
    return Json(d);
  }
  id object = [value toObject];
  if (!object || ![NSJSONSerialization isValidJSONObject:object]) {
    return Json(str([value toString]));
  }
  NSData* data = [NSJSONSerialization dataWithJSONObject:object options:0 error:nil];
  if (!data) return Json(str([value toString]));
  std::string text(static_cast<const char*>(data.bytes), data.length);
  Json parsed = Json::parse(text, nullptr, false);
  return parsed.is_discarded() ? Json(str([value toString])) : parsed;
}

// A result as text: strings as they are, anything else as JSON.
std::string resultText(JSValue* value) {
  if (!value || value.isUndefined || value.isNull) return "";
  if (value.isString) return str(value.toString);
  const Json j = fromJs(value);
  return j.is_string() ? j.get<std::string>() : j.dump(2);
}

// Promises settle through the context's microtask queue, which drains when
// the outermost API call returns - so attaching `then` to an already-settled
// promise yields its value before this function returns. Promises waiting on
// timers do not resolve here; plugins are told to keep tools synchronous.
JSValue* settle(JSValue* value, std::string* error) {
  if (!value || !value.isObject || ![value hasProperty:@"then"]) return value;
  JSValue* thenFn = value[@"then"];
  if (!thenFn || ![thenFn isObject]) return value;
  __block JSValue* settled = nil;
  __block JSValue* failed = nil;
  JSValue* onOk = [JSValue valueWithObject:^(JSValue* v) { settled = v; } inContext:value.context];
  JSValue* onFail = [JSValue valueWithObject:^(JSValue* v) { failed = v; } inContext:value.context];
  [value invokeMethod:@"then" withArguments:@[onOk, onFail]];
  if (failed) {
    *error = "rejected: " + resultText(failed);
    return nil;
  }
  if (!settled) {
    *error = "the promise did not settle (tools and hooks must not wait on timers)";
    return nil;
  }
  return settled;
}

void refreshSnapshot() {
  State& s = state();
  std::lock_guard<std::mutex> lock(s.snapshotMutex);
  s.infoSnapshot.clear();
  s.commandSnapshot.clear();
  s.toolSnapshot.clear();
  s.rendererSnapshot.clear();
  s.anyHooks = false;
  for (const auto& plugin : s.plugins) {
    PluginInfo info = plugin->info;
    info.commands = static_cast<int>(plugin->commands.count);
    info.tools = static_cast<int>(plugin->tools.count);
    info.hooks = static_cast<int>(plugin->beforeSend.count);
    info.renderers = static_cast<int>(plugin->renderers.count);
    s.infoSnapshot.push_back(info);
    NSArray* names = [plugin->commands.allKeys sortedArrayUsingSelector:@selector(compare:)];
    for (NSString* name in names) {
      s.commandSnapshot.push_back({plugin->info.id, plugin->info.name, str(name),
                                   str(plugin->commandDescriptions[name])});
    }
    for (const auto& [name, tool] : plugin->toolSpecs) s.toolSnapshot.push_back(tool);
    for (const auto& [language, renderer] : plugin->rendererSpecs) s.rendererSnapshot.push_back(renderer);
    if (plugin->beforeSend.count > 0) s.anyHooks = true;
  }
}

void logLine(Plugin* plugin, const std::string& line) {
  plugin->info.log.push_back(line);
  if (plugin->info.log.size() > kLogLimit) plugin->info.log.erase(plugin->info.log.begin());
  if (state().callbacks.log) state().callbacks.log(plugin->info.id, line);
}

std::string storagePath(const Plugin* plugin) {
  return state().directory + "/.data/" + plugin->info.id + ".json";
}

std::string expandHome(std::string path) {
  if (!path.empty() && path[0] == '~') path = native::homeDirectory() + path.substr(1);
  return path;
}

// Builds the `owui` object for one plugin. Blocks capture the raw Plugin
// pointer; the plugin outlives its context (unloadAll clears both together).
void installApi(Plugin* plugin) {
  JSContext* context = plugin->context;
  JSValue* api = [JSValue valueWithNewObjectInContext:context];
  HostCallbacks& cb = state().callbacks;

  api[@"plugin"] = ^(JSValue* meta) {
    if (!meta.isObject) return;
    JSValue* name = meta[@"name"];
    JSValue* version = meta[@"version"];
    JSValue* description = meta[@"description"];
    if (name.isString) plugin->info.name = str(name.toString);
    if (version.isString || version.isNumber) plugin->info.version = str(version.toString);
    if (description.isString) plugin->info.description = str(description.toString);
  };
  api[@"registerCommand"] = ^(NSString* name, JSValue* a, JSValue* b) {
    // (name, description, fn) or (name, fn)
    JSValue* fn = b.isUndefined ? a : b;
    NSString* description = b.isUndefined ? @"" : (a.isString ? a.toString : @"");
    if (!name.length || !fn.isObject) {
      logLine(plugin, "registerCommand: needs a name and a function");
      return;
    }
    plugin->commands[name] = fn;
    plugin->commandDescriptions[name] = description;
  };
  api[@"registerTool"] = ^(JSValue* spec, JSValue* fn) {
    if (!spec.isObject || !fn.isObject) {
      logLine(plugin, "registerTool: needs {name, description, parameters} and a function");
      return;
    }
    const std::string name = str(spec[@"name"].toString);
    if (name.empty()) {
      logLine(plugin, "registerTool: a tool needs a name");
      return;
    }
    Tool tool;
    tool.pluginId = plugin->info.id;
    tool.pluginName = plugin->info.name;
    tool.name = name;
    tool.description = spec[@"description"].isString ? str(spec[@"description"].toString) : "";
    tool.parameters = spec[@"parameters"].isObject ? fromJs(spec[@"parameters"])
                                                   : Json{{"type", "object"}, {"properties", Json::object()}};
    plugin->tools[ns(name)] = fn;
    plugin->toolSpecs[name] = tool;
  };
  api[@"onBeforeSend"] = ^(JSValue* fn) {
    if (fn.isObject) [plugin->beforeSend addObject:fn];
  };
  api[@"registerRenderer"] = ^(NSString* language, JSValue* a, JSValue* b) {
    JSValue* fn = b.isUndefined ? a : b;
    NSString* label = b.isUndefined ? [NSString stringWithFormat:@"Render %@", language] : a.toString;
    if (!language.length || !fn.isObject) {
      logLine(plugin, "registerRenderer: needs a language and a function");
      return;
    }
    const std::string lang = core::lower(str(language));
    plugin->renderers[ns(lang)] = fn;
    plugin->rendererSpecs[lang] = Renderer{plugin->info.id, plugin->info.name, lang, str(label)};
  };

  api[@"toast"] = ^(JSValue* text) { if (cb.toast) cb.toast(resultText(text)); };
  api[@"notify"] = ^(JSValue* title, JSValue* body) { if (cb.notify) cb.notify(resultText(title), resultText(body)); };
  api[@"insert"] = ^(JSValue* text) { if (cb.insert) cb.insert(resultText(text)); };
  api[@"send"] = ^(JSValue* text) { if (cb.send) cb.send(resultText(text)); };
  api[@"openArtifact"] = ^(JSValue* html) { if (cb.openArtifact) cb.openArtifact(resultText(html)); };
  api[@"log"] = ^(JSValue* text) { logLine(plugin, resultText(text)); };

  JSValue* clipboard = [JSValue valueWithNewObjectInContext:context];
  clipboard[@"read"] = ^NSString*() { return cb.clipboardRead ? ns(cb.clipboardRead()) : @""; };
  clipboard[@"write"] = ^(JSValue* text) { if (cb.clipboardWrite) cb.clipboardWrite(resultText(text)); };
  api[@"clipboard"] = clipboard;

  api[@"fetch"] = ^JSValue*(NSString* url, JSValue* options) {
    JSValue* out = [JSValue valueWithNewObjectInContext:context];
    if (!cb.allowNetwork) {
      out[@"ok"] = @NO;
      out[@"status"] = @0;
      out[@"error"] = @"network access is switched off for plugins";
      return out;
    }
    net::Request req;
    req.url = str(url);
    req.timeoutSec = 60;
    if (options.isObject) {
      if (options[@"method"].isString) req.method = str(options[@"method"].toString);
      if (options[@"body"].isString) req.body = str(options[@"body"].toString);
      else if (options[@"body"].isObject) req.body = fromJs(options[@"body"]).dump();
      if (options[@"headers"].isObject) {
        for (const auto& [k, v] : fromJs(options[@"headers"]).items()) {
          if (v.is_string()) req.headers.emplace_back(k, v.get<std::string>());
        }
      }
      if (options[@"contentType"].isString) req.contentType = str(options[@"contentType"].toString);
    }
    if (!req.body.empty() && req.method == "GET") req.method = "POST";
    const net::Response res = net::perform(req);
    out[@"ok"] = @(res.ok());
    out[@"status"] = @(res.status);
    out[@"body"] = ns(res.body);
    out[@"error"] = ns(res.error);
    return out;
  };
  api[@"readFile"] = ^JSValue*(NSString* path) {
    if (!cb.allowFiles) return [JSValue valueWithNullInContext:context];
    bool ok = false;
    const std::string data = core::readFile(expandHome(str(path)), &ok);
    return ok ? [JSValue valueWithObject:ns(data) inContext:context] : [JSValue valueWithNullInContext:context];
  };
  api[@"writeFile"] = ^BOOL(NSString* path, JSValue* text) {
    if (!cb.allowFiles) return NO;
    return core::writeFile(expandHome(str(path)), resultText(text)) ? YES : NO;
  };

  // Per-plugin key/value storage in plugins/.data/<id>.json.
  JSValue* storage = [JSValue valueWithNewObjectInContext:context];
  storage[@"get"] = ^JSValue*(NSString* key) {
    bool ok = false;
    const std::string raw = core::readFile(storagePath(plugin), &ok);
    Json all = ok ? Json::parse(raw, nullptr, false) : Json::object();
    if (all.is_discarded() || !all.is_object()) all = Json::object();
    const std::string k = str(key);
    return all.contains(k) ? toJs(all[k], context) : [JSValue valueWithUndefinedInContext:context];
  };
  storage[@"set"] = ^(NSString* key, JSValue* value) {
    bool ok = false;
    const std::string raw = core::readFile(storagePath(plugin), &ok);
    Json all = ok ? Json::parse(raw, nullptr, false) : Json::object();
    if (all.is_discarded() || !all.is_object()) all = Json::object();
    all[str(key)] = fromJs(value);
    mkdir((state().directory + "/.data").c_str(), 0755);
    core::writeFile(storagePath(plugin), all.dump(2));
  };
  api[@"storage"] = storage;

  context[@"owui"] = api;

  // console.log and friends go to the plugin's log.
  JSValue* console = [JSValue valueWithNewObjectInContext:context];
  for (NSString* level in @[@"log", @"info", @"warn", @"error", @"debug"]) {
    console[level] = ^(JSValue* a, JSValue* b, JSValue* c) {
      std::string line = resultText(a);
      if (!b.isUndefined) line += " " + resultText(b);
      if (!c.isUndefined) line += " " + resultText(c);
      logLine(plugin, line);
    };
  }
  context[@"console"] = console;

  context.exceptionHandler = ^(JSContext* ctx, JSValue* exception) {
    std::string where;
    JSValue* line = exception[@"line"];
    if (line && line.isNumber) where = " (line " + std::to_string(line.toInt32) + ")";
    const std::string message = str([exception toString]) + where;
    plugin->info.errors.push_back(message);
    if (plugin->info.errors.size() > 20) plugin->info.errors.erase(plugin->info.errors.begin());
    logLine(plugin, "error: " + message);
    ctx.exception = nil;
  };
}

std::vector<std::string> pluginFiles(const std::string& dir) {
  std::vector<std::string> out;
  DIR* d = opendir(dir.c_str());
  if (!d) return out;
  while (dirent* entry = readdir(d)) {
    const std::string name = entry->d_name;
    if (name.size() < 4 || name.substr(name.size() - 3) != ".js" || name[0] == '.' || name[0] == '_') continue;
    out.push_back(dir + "/" + name);
  }
  closedir(d);
  std::sort(out.begin(), out.end());
  return out;
}

// Runs `fn` with `args` on the plugin queue and waits up to timeoutMs.
bool callBlocking(Plugin* plugin, JSValue* fn, NSArray* args, int timeoutMs, std::string* result, std::string* error) {
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  __block std::string out;
  __block std::string err;
  dispatch_async(state().queue, ^{
    const size_t errorsBefore = plugin->info.errors.size();
    JSValue* value = [fn callWithArguments:args];
    if (plugin->info.errors.size() > errorsBefore) {
      err = plugin->info.errors.back();
    } else {
      value = settle(value, &err);
      if (value) out = resultText(value);
    }
    dispatch_semaphore_signal(done);
  });
  if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, static_cast<int64_t>(timeoutMs) * NSEC_PER_MSEC)) != 0) {
    *error = "timed out after " + std::to_string(timeoutMs / 1000) + " s";
    return false;
  }
  if (!err.empty()) {
    *error = err;
    return false;
  }
  *result = out;
  return true;
}

Plugin* find(const std::string& pluginId) {
  for (const auto& plugin : state().plugins) {
    if (plugin->info.id == pluginId) return plugin.get();
  }
  return nullptr;
}

}  // namespace

Host& Host::instance() {
  static Host host;
  return host;
}

void Host::setCallbacks(const HostCallbacks& callbacks) { state().callbacks = callbacks; }
void Host::setDirectory(const std::string& path) { state().directory = path; }
std::string Host::directory() const { return state().directory; }
bool Host::loaded() const { return state().loaded; }

void Host::unloadAll() {
  dispatch_sync(state().queue, ^{
    state().plugins.clear();
    state().loaded = false;
  });
  refreshSnapshot();
}

void Host::loadAll() {
  State& s = state();
  dispatch_sync(s.queue, ^{
    s.plugins.clear();
    mkdir(s.directory.c_str(), 0755);
    for (const std::string& path : pluginFiles(s.directory)) {
      auto plugin = std::make_unique<Plugin>();
      const size_t slash = path.find_last_of('/');
      std::string stem = path.substr(slash + 1);
      stem = stem.substr(0, stem.size() - 3);
      plugin->info.id = stem;
      plugin->info.name = stem;
      plugin->info.path = path;
      plugin->context = [[JSContext alloc] init];
      plugin->context.name = ns(stem);
      plugin->commands = [NSMutableDictionary dictionary];
      plugin->commandDescriptions = [NSMutableDictionary dictionary];
      plugin->tools = [NSMutableDictionary dictionary];
      plugin->beforeSend = [NSMutableArray array];
      plugin->renderers = [NSMutableDictionary dictionary];
      installApi(plugin.get());
      bool ok = false;
      const std::string source = core::readFile(path, &ok);
      if (!ok) {
        plugin->info.errors.push_back("could not read the file");
      } else {
        [plugin->context evaluateScript:ns(source) withSourceURL:[NSURL fileURLWithPath:ns(path)]];
      }
      plugin->info.ok = plugin->info.errors.empty();
      s.plugins.push_back(std::move(plugin));
    }
    s.loaded = true;
  });
  refreshSnapshot();
}

std::vector<PluginInfo> Host::plugins() const {
  std::lock_guard<std::mutex> lock(state().snapshotMutex);
  return state().infoSnapshot;
}
std::vector<Command> Host::commands() const {
  std::lock_guard<std::mutex> lock(state().snapshotMutex);
  return state().commandSnapshot;
}
std::vector<Tool> Host::tools() const {
  std::lock_guard<std::mutex> lock(state().snapshotMutex);
  return state().toolSnapshot;
}
std::vector<Renderer> Host::renderers() const {
  std::lock_guard<std::mutex> lock(state().snapshotMutex);
  return state().rendererSnapshot;
}
bool Host::hasBeforeSendHooks() const {
  std::lock_guard<std::mutex> lock(state().snapshotMutex);
  return state().anyHooks;
}

void Host::runCommand(const std::string& pluginIdRef, const std::string& nameRef, const Json& snapshot) {
  // The block outlives this call: copy what it needs (a captured reference
  // would dangle once the caller's strings are gone).
  const std::string pluginId = pluginIdRef;
  const std::string name = nameRef;
  const std::string snap = snapshot.dump();
  dispatch_async(state().queue, ^{
    Plugin* plugin = find(pluginId);
    if (!plugin) return;
    JSValue* fn = plugin->commands[ns(name)];
    if (!fn) return;
    const size_t errorsBefore = plugin->info.errors.size();
    JSValue* value = [fn callWithArguments:@[toJs(Json::parse(snap, nullptr, false), plugin->context)]];
    std::string err;
    if (plugin->info.errors.size() == errorsBefore) value = settle(value, &err);
    if (!err.empty()) logLine(plugin, "command " + name + ": " + err);
    refreshSnapshot();
  });
}

std::string Host::beforeSend(const std::string& text, const Json& snapshot) {
  if (!hasBeforeSendHooks()) return text;
  const std::string snap = snapshot.dump();
  dispatch_semaphore_t done = dispatch_semaphore_create(0);
  __block std::string current = text;
  dispatch_async(state().queue, ^{
    for (const auto& plugin : state().plugins) {
      for (JSValue* fn in plugin->beforeSend) {
        const size_t errorsBefore = plugin->info.errors.size();
        JSValue* value = [fn callWithArguments:@[ns(current), toJs(Json::parse(snap, nullptr, false), plugin->context)]];
        if (plugin->info.errors.size() > errorsBefore) continue;
        std::string err;
        value = settle(value, &err);
        if (!err.empty()) {
          logLine(plugin.get(), "onBeforeSend: " + err);
          continue;
        }
        if (value && value.isString) current = str(value.toString);
      }
    }
    dispatch_semaphore_signal(done);
  });
  if (dispatch_semaphore_wait(done, dispatch_time(DISPATCH_TIME_NOW, 3 * NSEC_PER_SEC)) != 0) {
    return text;   // a stuck hook does not hold the message hostage
  }
  refreshSnapshot();
  return current;
}

std::string Host::callTool(const std::string& pluginId, const std::string& name, const Json& args,
                           const Json& snapshot, std::string* error, int timeoutMs) {
  error->clear();
  __block Plugin* plugin = nullptr;
  __block JSValue* fn = nil;
  dispatch_sync(state().queue, ^{
    plugin = find(pluginId);
    if (plugin) fn = plugin->tools[ns(name)];
  });
  if (!plugin || !fn) {
    *error = "plugin tool '" + name + "' is not loaded";
    return "";
  }
  std::string result;
  NSArray* jsArgs = @[toJs(args, plugin->context), toJs(snapshot, plugin->context)];
  if (!callBlocking(plugin, fn, jsArgs, timeoutMs, &result, error)) return "";
  refreshSnapshot();
  return result;
}

std::string Host::render(const std::string& pluginId, const std::string& language, const std::string& code,
                         std::string* error, int timeoutMs) {
  error->clear();
  __block Plugin* plugin = nullptr;
  __block JSValue* fn = nil;
  dispatch_sync(state().queue, ^{
    plugin = find(pluginId);
    if (plugin) fn = plugin->renderers[ns(core::lower(language))];
  });
  if (!plugin || !fn) {
    *error = "no renderer for " + language;
    return "";
  }
  std::string result;
  if (!callBlocking(plugin, fn, @[ns(code)], timeoutMs, &result, error)) return "";
  return result;
}

std::string Host::writeExample() {
  const std::string dir = state().directory;
  mkdir(dir.c_str(), 0755);
  if (!pluginFiles(dir).empty()) return "";
  const std::string path = dir + "/example.js";
  const char* source = R"JS(// Example Open WebUI Desktop plugin. Edit and save: plugins reload on their own.
// Everything you can call lives on `owui`; see docs/PLUGINS.md in the repo.
owui.plugin({ name: "Example", version: "1.0", description: "Shows what a plugin can do" });

// A palette command (Cmd+K, type its name).
owui.registerCommand("Word count of the clipboard", "Counts words on the clipboard", (ctx) => {
  const text = owui.clipboard.read();
  const words = text.trim() ? text.trim().split(/\s+/).length : 0;
  owui.toast(`${words} words on the clipboard`);
});

// A command that uses the current chat: ctx.chat.messages is [{role, content}].
owui.registerCommand("Insert last reply as quote", (ctx) => {
  const replies = ctx.chat.messages.filter(m => m.role === "assistant");
  if (!replies.length) { owui.toast("No reply to quote yet"); return; }
  owui.insert(replies[replies.length - 1].content.split("\n").map(l => "> " + l).join("\n") + "\n");
});

// A tool the model can call when MCP tools are switched on in the composer.
owui.registerTool({
  name: "word_count",
  description: "Counts the words in a piece of text.",
  parameters: { type: "object", properties: { text: { type: "string" } }, required: ["text"] },
}, (args) => {
  const words = (args.text || "").trim().split(/\s+/).filter(Boolean).length;
  return { words };
});

// Rewrites outgoing prompts. Return a string to replace the text, nothing to leave it.
owui.onBeforeSend((text, ctx) => {
  if (text.startsWith("!shout ")) return text.slice(7).toUpperCase();
});

// Renders ```csv blocks as an HTML table in the artifact pane.
owui.registerRenderer("csv", "Table", (code) => {
  const rows = code.trim().split("\n").map(r => r.split(","));
  const cell = (c, tag) => `<${tag}>${c.replace(/&/g, "&amp;").replace(/</g, "&lt;")}</${tag}>`;
  const head = rows.length ? "<tr>" + rows[0].map(c => cell(c, "th")).join("") + "</tr>" : "";
  const body = rows.slice(1).map(r => "<tr>" + r.map(c => cell(c, "td")).join("") + "</tr>").join("");
  return `<style>table{border-collapse:collapse;font:14px system-ui}td,th{border:1px solid #888;padding:4px 8px}</style><table>${head}${body}</table>`;
});

// Per-plugin storage survives reloads: owui.storage.get(key) / owui.storage.set(key, value).
owui.storage.set("loads", (owui.storage.get("loads") || 0) + 1);
console.log("example plugin loaded", owui.storage.get("loads"), "times");
)JS";
  core::writeFile(path, source);
  return path;
}

}  // namespace plugins
