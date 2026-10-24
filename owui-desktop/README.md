# Open WebUI Desktop

A native macOS client for [Open WebUI](http://davids-macbook-pro.tailba9313.ts.net:3000),
written in C++20. No Electron, no browser: a Cocoa window, a Metal renderer and
Dear ImGui, talking to the server over its own REST API and socket.io stream.

## Building

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build -j8
    open "build/Open WebUI.app"

Requirements: macOS 13+, Xcode command line tools and CMake. Everything else is
vendored under `third_party/` (Dear ImGui, nlohmann/json, stb_image) or comes
from the system (libcurl, SQLite, Metal, AVFoundation, Security).

The first launch asks once for permission to read the session token back out of
the keychain — the bundle is ad-hoc signed, so macOS treats each rebuild as a
new program. Choose "Always Allow" to stop the prompt coming back.

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

## Self-test

    OWUI_SERVER=http://localhost:3000 OWUI_EMAIL=you@example.com \
      OWUI_PASSWORD=... "build/Open WebUI.app/Contents/MacOS/Open WebUI" --selftest

Exercises ~50 endpoints end to end — sign-in, models, chat lifecycle, both
streaming transports, files, knowledge, prompts, memories, notes, admin and
Ollama passthrough — and prints a pass/fail line for each.

`OWUI_TOKEN` can be used instead of email/password, and `OWUI_SERVER` /
`OWUI_TOKEN` also override the saved settings when running the GUI, which is
handy for pointing a build at a scratch server.

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
