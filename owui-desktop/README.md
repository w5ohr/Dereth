# Open WebUI Desktop

A native macOS client for [Open WebUI](http://davids-macbook-pro.tailba9313.ts.net:3000),
written in C++20. No Electron, no browser: a Cocoa window, a Metal renderer and
Dear ImGui, talking to the server over its own REST API and socket.io stream.

## Building

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j8
    cmake --install build          # -> /Applications/Open WebUI.app
    open -a "Open WebUI"

Leave out the install step to run it from `build/` instead. The install
destination is `/Applications` unless `-DCMAKE_INSTALL_PREFIX=...` says
otherwise, and the bundle is re-signed where it lands.

The icon is generated rather than checked in by hand — `tools/make_icon.sh`
rebuilds `resources/AppIcon.icns` from `tools/make_icon.mm`.

Requirements: macOS 13+, Xcode command line tools and CMake. Everything else is
vendored under `third_party/` (Dear ImGui, nlohmann/json, stb_image) or comes
from the system (libcurl, SQLite, Metal, AVFoundation, Security).

Once you have signed in, macOS asks for permission to read the session token
back out of the keychain. The bundle is ad-hoc signed, so every rebuild counts
as a new program and the prompt returns; choose "Always Allow" after a build to
settle it. The sign-in screen stays usable while the prompt is up, and a fresh
install that has never saved a session does not touch the keychain at all.

## What it does

**Chat** — multi-model conversations with live streaming, reasoning
("thinking") sections, markdown with tables, fenced code and inline images,
citations, per-message copy/edit/regenerate/branch, thumbs up/down feedback that
is sent to the server's evaluation endpoints, text-to-speech playback, usage and
tokens-per-second, conversation branching with sibling navigation, and
server-generated chat titles.

**Composer** — file and image attachments (drag a file in with the paperclip, or
paste an image), voice input recorded from the microphone and transcribed by the
server, plus the web-search, image-generation, code-interpreter and tool toggles
the web UI has.

**Sidebar** — folders, pinned chats, date-bucketed history, server-side search,
rename/pin/archive/duplicate/share/move, and an archived view.

**Notes** — the server's markdown notes, with an editor and a live preview.

**Channels** — the shared rooms, refreshed live from socket.io events.

**Workspace** — custom models, knowledge bases (upload straight into one),
prompts, tools, functions and skills, with enable/disable where the server
supports it.

**Images** — text-to-image against whatever backend the server is configured
with (ComfyUI, A1111, OpenAI), with a gallery and save-to-disk.

**Files** — everything uploaded, with preview, download and delete.

**Memories** — add, edit, delete and semantically search the long-term memories.

**Admin** — users and roles, Ollama model management (pull with live progress,
unload, delete), server configuration, analytics, the model leaderboard and
connection settings.

**API Console** — a browsable catalogue of the server's endpoints plus a
free-form request runner, so anything without dedicated UI is still reachable.

## How it talks to the server

Open WebUI 0.12 runs completions as background tasks when a `chat_id` is
present, and streams the tokens over socket.io rather than in the HTTP response.
This client implements both paths:

- **socket.io over WebSocket** (`src/core/socketio.cpp`, `src/core/websocket.mm`)
  is used whenever the live connection is up. It carries `response:completion`
  deltas, status events, citations, chat titles and channel activity, exactly as
  the web UI receives them.
- **HTTP SSE** (`src/api/client.cpp`) is the fallback when the socket is down,
  by omitting `chat_id` so the server streams inline.

Chats are written back in the web UI's own format (`history.messages` map plus
the flat `messages` array), so conversations started here open normally in the
browser and vice versa.

## Testing against a scratch server

Rather than poke at a live instance, run a throwaway one:

    docker run -d --name owui-test -p 3001:8080 \
      -e OLLAMA_BASE_URL=http://host.docker.internal:11434 \
      ghcr.io/open-webui/open-webui:main

The first account created on it becomes the admin. Point the app or the
self-test at `http://localhost:3001`, then `docker rm -f owui-test` when done.

## Self-test

    OWUI_SERVER=http://localhost:3000 OWUI_EMAIL=you@example.com \
      OWUI_PASSWORD=... "build/Open WebUI.app/Contents/MacOS/Open WebUI" --selftest

Exercises ~50 endpoints end to end — sign-in, models, chat lifecycle, both
streaming transports, files, knowledge, prompts, memories, notes, admin and
Ollama passthrough — and prints a pass/fail line for each.

`OWUI_TOKEN` can be used instead of email/password, and `OWUI_SERVER` /
`OWUI_TOKEN` also override the saved settings when running the GUI, which is
handy for pointing a build at a scratch server.

    "build/Open WebUI.app/Contents/MacOS/Open WebUI" --mcptest tools/mcp_test_server.py

Drives the MCP client against the bundled Python test server over stdio and
both Streamable HTTP reply styles (JSON and SSE), checks the tool-loop
helpers, and - with `OLLAMA_URL` or `OWUI_SERVER`/`OWUI_TOKEN` set - runs one
real tool-calling turn (`OWUI_MCP_MODEL`, default `qwen3:8b`).

## Beyond the web UI

Things a browser tab can't do, plus the workflow features built on top:

| | |
|---|---|
| **⌥Space** (anywhere) | Quick Ask - a floating prompt, answered in place |
| **⌘K** | Command palette: commands, panes, models, prompts, chats, presets |
| **⌘J** | Quick Ask inside the app |
| **⌘F** | Find in the open chat, with hit markers |
| **⌘D / ⌘I / ⌘E** | Compare sibling answers word-by-word / pinned messages / what was sent |
| **⌘U** | Voice conversation: listen, transcribe, answer, speak, repeat |
| **⌘P** | Export the chat (also Markdown and HTML from File ▸ Export) |
| **⌘-** | Tools pane |
| **⌥↑ / ⌥↓** | Walk the transcript; then **c** copy, **r** regenerate, **p** pin, **s** speak, **e** edit, **Esc** back to the composer |
| **/** in the composer | Saved prompts; Tab inserts |

- **macOS:** menu bar extra with recent chats; Services ▸ "Ask Open WebUI" on
  any selected text; `owui://ask?prompt=…`, `owui://chat/<id>`, `owui://quick`
  links; drag files onto the window; chats indexed in Spotlight; notifications
  you can reply to; Dock badge for running replies; Handoff to the web UI;
  Live Text on pasted images; system, light, dark and midnight themes.
- **Chat:** presets, per-message time-to-first-token, retry with another model,
  continue, a context-pressure meter, compaction, scheduled prompts, a reference
  chat beside the open one, an offline queue that sends when the server
  returns, and auto-routing of short questions to a smaller model.
- **Code:** syntax highlighting; Run (sandboxed - no network, writes confined
  to a scratch dir) and Lint under every code block; Apply to file with a diff
  preview; diffs rendered with per-hunk accept/reject and applied even when
  their hunk headers are wrong; attach a folder (respecting `.gitignore`) or the
  git diff; project mode, which sends a pinned folder's tree and chosen files
  with every turn; explain a pasted build error against the source.
- **Knowledge:** sync a folder into a knowledge base and keep it in sync on
  change; clickable citations; save a web page into a knowledge base; a local
  embedding index (SQLite + `nomic-embed-text`) whose hits can ride along with
  every message.
- **Server:** server profiles with per-server sign-in, a Tools pane with a
  command runner and a benchmark tab that charts models against each other,
  and "fits in memory" guidance plus keep-alive and unload controls in Admin.
- **Composer:** the input is a native macOS text view floated over the chat
  box, so it has IME/CJK input, dictation (press 🎤/Fn), spellcheck, the
  emoji picker (⌃⌘Space), trackpad selection and native undo. It grows with
  the text; Enter sends, Shift+Enter breaks a line (Cmd+Enter sends when
  "Enter sends" is off), Esc hands the keyboard to the transcript, ⌥↑/⌥↓ walk
  it, pasting an image attaches it. Settings ▸ "Native text field" switches
  back to the built-in field.
- **MCP:** connect Model Context Protocol servers (stdio or Streamable HTTP;
  import your Claude Desktop or Cursor config) under Tools ▸ MCP, switch the
  plug icon on in the composer, and the model can call their tools. Each call
  shows as a card above the reply with its arguments, result and timing; the
  first call to a tool asks for approval (allow once / always / deny), and
  the answers are remembered per tool. Tool-enabled turns run through Ollama's
  native tool calling via the server's `/ollama/api/chat` passthrough (or
  Ollama directly), so for those turns the server's own web search, knowledge
  files and Open WebUI tools are skipped. Resources and prompts are listed;
  `roots/list` answers with the project folder. `--mcptest` exercises it all
  against `tools/mcp_test_server.py`.

How the fifty were split and built is in `docs/FEATURE-PLAN.md`; the next
hundred are planned in `docs/FEATURE-PLAN-2.md`.

## Layout

    src/
      main.mm            Cocoa + Metal + Dear ImGui host, menu bar
      app.{h,cpp}        state, auth, chat model, streaming, layout
      sidebar.cpp        conversation list
      chat_pane.cpp      conversation view and composer
      *_pane.cpp         notes, channels, workspace, images, files,
                         memories, admin, console, settings
      api/client.*       typed Open WebUI REST client
      core/              http (libcurl), socket.io, websocket, async pool,
                         settings, keychain, shared helpers
      ui/                theme, vector icons, widgets, markdown, image cache
