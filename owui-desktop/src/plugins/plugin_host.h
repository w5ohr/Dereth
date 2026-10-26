// JavaScriptCore plugins: every *.js in the plugins folder runs in its own
// JSContext on one serial plugin queue and registers palette commands,
// model-callable tools, before-send hooks and code-block renderers through
// a small `owui` API. The host is UI-free: the app hands it callbacks that it
// marshals onto the UI thread itself, and asks it for snapshots of what the
// plugins registered. Loading is synchronous; calls into plugins either run
// in the background (commands) or block with a timeout (hooks, tools,
// renderers), so a stuck plugin never stalls the app for long.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace plugins {

using Json = nlohmann::json;

struct Command {
  std::string pluginId;
  std::string pluginName;
  std::string name;
  std::string description;
};

struct Tool {
  std::string pluginId;
  std::string pluginName;
  std::string name;
  std::string description;
  Json parameters;      // JSON schema the model sees
};

struct Renderer {
  std::string pluginId;
  std::string pluginName;
  std::string language;  // code-block tag it handles ("dot", "abc", ...)
  std::string label;     // button text
};

struct PluginInfo {
  std::string id;        // file stem
  std::string name;      // from owui.plugin({name}) or the id
  std::string version;
  std::string description;
  std::string path;
  bool ok = false;       // loaded without an exception
  std::vector<std::string> errors;
  std::vector<std::string> log;   // console.log / owui.log, newest last (capped)
  int commands = 0, tools = 0, hooks = 0, renderers = 0;
};

// What `owui.*` does to the app. The host calls these from the plugin
// queue; the app posts them to its UI thread.
struct HostCallbacks {
  std::function<void(const std::string& text)> toast;
  std::function<void(const std::string& title, const std::string& body)> notify;
  std::function<void(const std::string& text)> insert;        // into the composer
  std::function<void(const std::string& text)> send;          // as a new message
  std::function<void(const std::string& html)> openArtifact;
  std::function<std::string()> clipboardRead;                 // may run on the plugin queue
  std::function<void(const std::string& text)> clipboardWrite;
  std::function<void(const std::string& pluginId, const std::string& line)> log;
  bool allowNetwork = true;    // owui.fetch
  bool allowFiles = true;      // owui.readFile / writeFile
};

class Host {
 public:
  static Host& instance();

  void setCallbacks(const HostCallbacks& callbacks);
  void setDirectory(const std::string& path);
  std::string directory() const;

  // (Re)loads every plugin from the directory. Blocks until done.
  void loadAll();
  void unloadAll();
  bool loaded() const;

  std::vector<PluginInfo> plugins() const;
  std::vector<Command> commands() const;
  std::vector<Tool> tools() const;
  std::vector<Renderer> renderers() const;
  bool hasBeforeSendHooks() const;

  // `snapshot` is what the plugin sees as `ctx`: {chat: {id, title, messages}, model}.
  void runCommand(const std::string& pluginId, const std::string& name, const Json& snapshot);  // background
  // Returns the (possibly rewritten) text. A hook returning nothing leaves it alone.
  std::string beforeSend(const std::string& text, const Json& snapshot);
  // Blocks up to `timeoutMs`; on failure sets *error and returns "".
  std::string callTool(const std::string& pluginId, const std::string& name, const Json& args,
                       const Json& snapshot, std::string* error, int timeoutMs = 60000);
  std::string render(const std::string& pluginId, const std::string& language, const std::string& code,
                     std::string* error, int timeoutMs = 5000);

  // Writes a documented example plugin into the directory when there is none
  // yet; returns its path (or "" when the directory already has plugins).
  std::string writeExample();

 private:
  Host() = default;
};

}  // namespace plugins
