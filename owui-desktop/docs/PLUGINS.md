# Plugins

Open WebUI Desktop loads every `*.js` file in

```
~/Library/Application Support/Open WebUI Desktop/plugins/
```

Each file runs in its own JavaScriptCore context with one global, `owui`, and
registers what it adds. Save a file and it reloads (the folder is watched);
Tools ▸ Plugins lists every plugin with its counts, errors and log, and can
write a documented example plugin to start from. Files whose name starts with
`_` or `.` are ignored, so you can keep drafts next to live plugins.

Plugins are ordinary code running inside the app: install only ones you have
read. Network and file access can be switched off in Tools ▸ Plugins.

## Registering things

```js
owui.plugin({ name: "My plugin", version: "1.0", description: "..." });

// Palette command (Cmd+K). `ctx` is described below.
owui.registerCommand("Name shown in the palette", "optional description", (ctx) => { ... });

// A tool the model can call. Appears next to MCP tools when the composer's
// MCP toggle is on; approvals work like MCP tools (per tool: once / always / never).
owui.registerTool(
  { name: "tool_name", description: "what it does", parameters: { type: "object", properties: {...}, required: [...] } },
  (args, ctx) => "a string" /* or an object, returned to the model as JSON */
);

// Rewrite a prompt before it is sent (after prompt variables are filled in).
// Return a string to replace it; return nothing to leave it alone.
owui.onBeforeSend((text, ctx) => { ... });

// A button under every ```lang code block; return HTML for the artifact pane.
owui.registerRenderer("lang", "Button label", (code) => "<html>...");
```

## Acting on the app

| call | effect |
|---|---|
| `owui.toast(text)` | a toast |
| `owui.notify(title, body)` | a macOS notification |
| `owui.insert(text)` | append to the composer and focus it |
| `owui.send(text)` | send a message in the open chat |
| `owui.openArtifact(html)` | show HTML in the artifact pane |
| `owui.clipboard.read()` / `owui.clipboard.write(text)` | the pasteboard |
| `owui.fetch(url, {method, headers, body, contentType})` | synchronous HTTP; returns `{ok, status, body, error}` |
| `owui.readFile(path)` / `owui.writeFile(path, text)` | files (`~` expands); `null`/`false` when refused |
| `owui.storage.get(key)` / `owui.storage.set(key, value)` | per-plugin JSON storage in `plugins/.data/<id>.json`, kept across reloads |
| `owui.log(...)`, `console.log(...)` | the plugin's log in Tools ▸ Plugins |

## `ctx`

Commands, tools and before-send hooks receive a snapshot:

```js
{
  chat: { id, title, messages: [{ id, role, content, model }, ...] },  // the visible branch, last 40
  model: "qwen3:8b",      // the selected model
  composer: "...",        // the composer's text
  user: "Ann"             // the signed-in user's name
}
```

## Rules of the road

- Plugins run on one serial queue off the UI thread. Commands run in the
  background; before-send hooks block the send for up to 3 s, tools for 60 s,
  renderers for 5 s - after that the app moves on and the plugin's log says so.
- `async` functions are fine as long as they resolve without waiting on
  timers (there is no `setTimeout`); `owui.fetch` is synchronous for that
  reason.
- A thrown error lands in the plugin's log and, for tools, goes back to the
  model as the tool's error.
- Tool names are qualified as `<plugin name>__<tool>` for the model, like MCP
  tools.

`Open WebUI --plugintest` runs the host's offline checks against a scratch
folder.
