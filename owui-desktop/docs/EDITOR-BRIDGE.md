# Editor bridge

Two directions:

- **App -> editor.** A file reference in a reply - `src/app.cpp:120`,
  `/abs/path/file.h:3:4`, `~/notes.md:9`, in prose or in backticks - is a
  link. Clicking it opens that line in the editor chosen under Settings >
  Editor. Relative paths resolve against the pinned project folder, then your
  home folder.
- **Editor -> app.** Any editor that can run a shell command can send the
  current selection to a new chat through the `owui://` scheme:

      owui://explain?file=<path>&line=<n>&language=<lang>&text=<selection>

  Add `&send=0` to put the prompt in the composer without sending it.
  Everything is URL-encoded; the app decodes it, builds "Explain this code
  from <file>:<line> ..." with the selection in a fenced block, and sends.

## VS Code / Cursor

`.vscode/tasks.json`:

```json
{
  "version": "2.0.0",
  "tasks": [
    {
      "label": "Ask Open WebUI about the selection",
      "type": "shell",
      "command": "open",
      "args": [
        "owui://explain?file=${relativeFile}&line=${lineNumber}&language=${fileExtname}&text=${selectedText}"
      ],
      "presentation": { "reveal": "never" },
      "problemMatcher": []
    }
  ]
}
```

A keybinding (`keybindings.json`):

```json
{ "key": "cmd+shift+o", "command": "workbench.action.tasks.runTask", "args": "Ask Open WebUI about the selection" }
```

VS Code fills `${selectedText}` and `${lineNumber}` itself; `open` passes the
URL to the app, which decodes it. Selections with `&` or `#` in them arrive
intact because VS Code does not re-encode the argument, and the app reads
`text=` up to the end of the query.

## Zed

`~/.config/zed/tasks.json`:

```json
[
  {
    "label": "Ask Open WebUI",
    "command": "open",
    "args": ["owui://explain?file=$ZED_RELATIVE_FILE&line=$ZED_ROW&text=$ZED_SELECTED_TEXT"],
    "hide": "always"
  }
]
```

Bind it under `"task::Spawn"` with `"task_name": "Ask Open WebUI"`.

## Xcode and everything else

Select text in any app and use **Services > Ask Open WebUI** (the app
registers that service). For Xcode specifically, a Behavior or an Automator
Quick Action can run:

```sh
open "owui://explain?text=$(pbpaste | python3 -c 'import sys,urllib.parse;print(urllib.parse.quote(sys.stdin.read()))')"
```

## Choosing the editor

Settings > Editor: VS Code (`vscode://file/<path>:line:col`), Cursor, Zed,
Xcode (`xed -l line file`), Sublime Text (`subl file:line:col`), or a custom
command with `{file}`, `{line}` and `{col}` placeholders - `{file}` is quoted
for you. The "Test" button opens the app's own settings file at line 1.
