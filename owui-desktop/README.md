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
  back to the built-in field. Drafts autosave per chat and come back after a
  relaunch; ↑ in an empty composer walks earlier prompts shell-style (across
  chats, ↓ returns); a paste of 40+ lines becomes an attachment chip instead
  of flooding the box; "Quote in a reply" under any message drops it into the
  composer as a `>` block; and steering chips under the latest answer
  (Shorter, More detail, As a table, As code, Simpler, Check it) send the
  follow-up with one click.
- **Windows:** any chat can open in its own window - from the sidebar row's
  menu, the chat's "More" menu, File ▸ Open Chat in New Window (⇧⌘N) or the
  palette - and as a floating mini window (compact, no sidebar, above other
  apps). View ▸ Float on Top (⇧⌘T) makes any window float. Each window is a
  full copy of the client (its own composer, tools, streams); the server keeps
  them in step, and only the first window owns the global hotkeys, the menu
  bar extra and the settings file.
- **Screen capture to chat:** the camera icon in the composer, File ▸ Capture
  Screen to Chat (⇧⌘2), the palette, or ⌃⌥S from any app runs the system
  capture (drag a region, or Space for a window); the shot is attached and
  any text in it is read with Live Text and dropped into the composer as a
  quoted block, so models without vision can use it too. macOS asks for
  Screen Recording permission the first time.
- **Document column:** click a citation that points at a PDF and the file
  opens in a PDFKit view beside the chat, scrolled to the cited page with the
  passage highlighted (the server's and PDFKit's text extractions differ, so
  the match shortens the passage from the front until it finds it). Page
  controls, "Find passage again", the excerpt popup and "Open in Preview";
  files are cached under the app's cache folder.
- **Prompt variables:** `{{clipboard}}`, `{{selection}}` (the terminal pane's
  selection, or the text handed in through Services / Quick Ask),
  `{{date}}`, `{{time}}`, `{{datetime}}`, `{{weekday}}`, `{{timezone}}`,
  `{{user}}`, `{{model}}`, `{{title}}`, `{{file:path}}` (contents, `~` and
  project-relative paths, 200 KB cap, binaries skipped) and
  `{{input:Label|default}}` are filled in when the prompt is sent. Inputs
  open a fill-in form first, with a live preview of what will go out; the
  prompt history keeps the template, not the expansion. Open WebUI's
  `{{CLIPBOARD}}`, `{{CURRENT_DATE}}`, `{{USER_NAME}}`... work too, so saved
  prompts from the workspace expand the same way. `Open WebUI --varstest`
  runs the parser's offline checks.
- **Stop, edit, continue:** stop a reply mid-stream, fix its text in place
  (pencil in the reply's action row: Save, or Save & continue) and let the
  model carry on from the edited tail - the reply so far is sent back as the
  model's own open turn, so it continues rather than starting over. The play
  button does the same without editing.
- **Conversation tree:** the tree icon in the chat header opens a map of
  every branch beside the transcript - one row per turn, siblings side by
  side, the branch you are reading highlighted. Click a node to jump there
  (switching branches when needed), hover for the full text, right-click to
  delete a message or prune its whole subtree, or prune every dead branch at
  once; pruning removes the messages from the server too.
- **Structured output:** the braces icon in the chat header takes a JSON
  schema for the chat (or a starting point: list of records, classification,
  key facts, or one inferred from an example). Every reply is requested in
  that shape (`response_format`, which Open WebUI maps to Ollama's `format`),
  checked locally against the schema, and shown as a table, tree or JSON with
  a one-click copy; a reply that breaks the schema is sent back to the model
  with the violations, up to three times, before it is shown with them
  listed. `Open WebUI --schematest` runs the validator's offline checks.
- **Sampling panel:** the gear in the chat header sets this chat's generation
  parameters - temperature, top-p, top-k, min-p, repeat penalty, max tokens,
  context size (with the model's reported window), a lockable seed with
  Reroll, stop sequences and the system prompt - with Precise / Balanced /
  Creative / Deterministic quick presets and "Save as preset". Each knob is
  only sent while switched on; the values are stored in the chat under Open
  WebUI's own names, so the web UI sees them too.
- **Fan-out:** the branch icon in the composer sends one prompt to several
  models at once (the set is remembered). Their answers stream side by side
  with timing and tok/s; "Keep" continues the conversation from that answer,
  "Keep, drop others" also deletes the rest. Until one is kept, the columns
  stay; ⌘D still word-diffs two answers.
- **Terminal:** Tools ▸ Terminal runs your login shell on a real pty with a
  built-in VT/xterm emulator - colours, bold/underline/inverse, cursor
  movement, scroll regions and the alternate screen, 5,000 lines of
  scrollback (wheel), mouse selection with Cmd+C, Cmd+V paste, Ctrl keys and
  arrows. "Ask about selection" (or the last 40 lines) drops the text into
  the composer; "Explain as build error" sends it through the build-error
  explainer with the referenced source.
- **Local copy:** every chat is mirrored into a SQLite database per server
  (`~/Library/Application Support/Open WebUI Desktop/mirror-<host>.db`) with
  full-text search. With the server away the sidebar lists the local copy,
  chats open from it, and search runs against it; a change made offline (a
  rating, a rename) is kept and pushed when the server returns, keeping the
  server's newer title if it changed meanwhile. Settings ▸ Local copy shows
  the stats and offers Sync now / Rebuild.
- **Artifacts:** an HTML, SVG or Mermaid block in a reply renders live in a
  WebKit column beside the chat - automatically for HTML and SVG, or from the
  "Preview" button under any such block. The column pages through every
  artifact in the chat, shows the page's console output and errors, has an
  editor with Apply/Revert, and can copy, save or open the artifact in your
  browser. (Mermaid loads its renderer from a CDN, so it needs the network.)
- **Math and diagrams inline:** a `$$ ... $$` paragraph or a `math`/`latex`
  block renders with KaTeX, a `mermaid` block with Mermaid and a `dot`/
  `graphviz` block with Graphviz, right in the transcript as an image in the
  current theme, with Source / Copy buttons above it. Rendering happens in a
  hidden WebKit view that loads the three libraries from jsDelivr, so it
  needs the network; offline (or on a bad block) the source shows as code
  instead. Switch it off from the palette ("Diagrams and math").
- **Recipes:** Tools ▸ Recipes saves chains of prompts. Each step can use
  `{{prev}}` (the previous reply), `{{step:N}}` and the prompt variables;
  `{{input:Label}}` fields are asked once before the run. A run opens a new
  chat and sends the steps one after another as each reply finishes, so the
  whole chain is kept. Run from the tab, the palette ("Run recipe: ...") or
  a daily schedule; three example recipes are one click away.
- **Audit log:** Tools ▸ Audit lists every outbound request the app made -
  host, method and path (query strings, headers and bodies are never
  recorded), status, bytes each way, duration - and every tool-call decision
  (server, tool, allowed/denied and why), for today, 7 or 30 days or all
  time, with a per-host summary, a filter, CSV/JSON export and a clear
  button. Switch it off in the same tab. `Open WebUI --audittest` checks
  the log offline.
- **Repo map:** with a project folder pinned, every message carries a
  compact symbol index of the project - one line per file listing the
  classes, functions, methods and types it defines with line numbers
  (ctags-style regexes for Python, JS/TS, C/C++/ObjC, Go, Rust, Swift, Ruby,
  Java/Kotlin/C#, Markdown headings) - so the model can name the right
  function or ask for a file instead of guessing from the tree. Capped at
  ~8k tokens, trimming detail before dropping files; switch and Reindex in
  the project popover. `Open WebUI --repomaptest` checks the extractors.
- **Test loop:** Tools ▸ Test loop detects the project's runner (pytest or
  unittest, npm test, ctest, swift test, go test, cargo test, make test),
  runs it, and on failure asks the model for fixes with the failure digest
  and the files it names, applies the returned files (backups kept, one Undo
  reverts the round) and runs again - until green or the round budget is
  spent. Turn off auto-apply to review each fix in the apply dialog first.
- **Triggers:** under Tools ▸ Recipes, a watched folder runs a recipe for
  every file that lands in it (the file is attached and its path is the
  recipe's `{{input:File}}`; filter by extension), and the clipboard watcher
  runs a recipe whenever new text is copied (steps read it with
  `{{clipboard}}`; your own copied replies are ignored). Runs queue behind a
  recipe already going.
- **Backup and import:** Tools ▸ Backup exports everything to one zip -
  every chat as JSON, the settings (tokens stay in the keychain), usage log,
  plugins, the offline mirror and, if you tick it, the encrypted private
  chats - and imports ChatGPT and Claude `conversations.json` exports (or
  chat JSON from a backup) as chats on the server, tagged by source; the
  shown branch of a ChatGPT conversation tree is kept.
- **Usage dashboard:** every finished reply is logged locally (model, tokens
  in/out, time to first token, total time, tok/s). Tools ▸ Usage shows 7 /
  30 / 90 days or all time: headline tiles, output tokens per day stacked by
  model with a tok/s trend, a per-model table, cost for models you give a
  per-million price, a rough energy figure for local models (joules per
  token, adjustable), and CSV export. Private chats are logged without their
  chat id. The log can be switched off.
- **Git:** Tools ▸ Git shows the pinned project's repository - branch and
  upstream, the status list with stage/unstage ticks, the staged or
  per-file diff, a commit box whose message the model drafts from the staged
  diff (edit, then Commit), "Explain in chat" for any diff, blame for a
  file or line range, and a PR description drafted from the branch's
  commits against a base branch, ready to copy.
- **Model cards:** hover a model in the picker or the chat header's model
  chip for what Ollama knows about it - family, parameters, quantisation,
  size on disk, trained context, layers, bits per weight, capabilities,
  Modelfile defaults, licence. "Compare models..." (picker or palette) lays
  every Ollama model out in a sortable table so a quant is a look, not a
  guess; click a row to select it.
- **Multi-file apply:** when a reply names the files its code blocks belong
  to (`path=` in the fence, a `// file:` first line, or a heading / bold /
  inline-code path above the block), a folder icon in the reply's action row
  applies them all at once: one dialog with the combined diff, hunks you can
  untick, new files created, unchanged files skipped, `.bak` backups beside
  every edited file, and a write that fails puts the others back. "Undo the
  last apply to files" (palette) restores everything from the backups.
- **Context strategy:** click the `ctx` figure in the chat header to choose
  what happens when a chat outgrows the model's window - drop the oldest
  turns, keep a sliding window of recent turns, or fold the oldest turns into
  a rolling summary the model keeps seeing - and preview exactly which
  messages the next send would leave out. System messages and the newest
  turn are always kept; a toast reports every trimmed send. `Open WebUI
  --contexttest` checks the planner offline.
- **Reading while it streams:** the transcript follows new text only while
  you are at the bottom; scroll up and it stays put, with a "New text ↓" pill
  to jump back. Each chat remembers where you were when you return to it.
- **Private chats:** the lock beside "New chat" starts a conversation that
  never reaches the Open WebUI server: it is stored in an encrypted file
  (AES-256-CBC + HMAC-SHA256 under a key in your keychain) in the support
  folder, replies come straight from Ollama (set the Ollama URL in Settings),
  and the title is written by a local model call. Private chats sit in their
  own sidebar section with a lock; redaction, prompt variables, plugins and
  MCP tools work in them as anywhere. `Open WebUI --privatetest` checks the
  store offline.
- **Documents read on this Mac:** attach a PDF, Word, PowerPoint, Excel, RTF
  or text file and its text is extracted locally - PDFKit for PDFs, the
  zipped XML for Office files, textutil for the rest - into pages, slides or
  sheets, and goes with the prompt instead of being uploaded (a chip says
  "3 pages, read on this Mac"). Big documents are trimmed to the parts that
  best match your question. Replies cite "[name p.N]"; those citations
  become buttons under the reply that open the PDF column on that page.
  Private chats always read locally; the setting under Chat switches it for
  server chats, which fall back to an upload for a file with no text (a
  scan). `owui://attach?path=/file` attaches from a script or Shortcut.
  `Open WebUI --doctest` checks the extraction offline.
- **Direct backends:** Settings ▸ Direct backends registers model servers the
  app talks to with no Open WebUI in between - Ollama, anything
  OpenAI-compatible (OpenAI, LM Studio, llama.cpp server, vLLM, OpenRouter,
  Groq...) or Anthropic - with API keys kept in the macOS keychain. The
  Ollama URL is always the first backend. A private chat picks its backend
  and model from the button in its header; the reply, its tool calls,
  reasoning and token counts all come straight from that backend. "Test"
  lists a backend's models. `Open WebUI --backendstest` checks the
  OpenAI/Anthropic conversions offline.
- **Stream recovery:** every reply in flight is snapshotted to a local file as
  it arrives. After a crash or a quit mid-reply, the next launch checks the
  server: a reply it finished meanwhile is reported, one its background task
  is still writing is re-attached and followed to the end, and one only this
  client was driving (Continue, tool or agent loops) keeps the text that had
  arrived with a note and the Continue button to finish it.
- **Redaction:** every prompt is checked before it leaves the Mac for API
  keys, tokens, private keys, passwords, card numbers (Luhn-checked), SSNs,
  IBANs, emails, phone numbers and IP addresses. A review sheet lists the
  findings with previews; masked items go to the model as placeholders
  (`[EMAIL_1]`) and the mapping stays in a local file, so the chat - and the
  reply - read with the real values here while the server never sees them.
  Per kind: ask, always mask, or never. Settings ▸ "Check prompts for
  secrets and personal data". `Open WebUI --redacttest` runs the scanner's
  offline checks.
- **Plugins:** JavaScript files in `~/Library/Application Support/Open WebUI
  Desktop/plugins/` run in JavaScriptCore and register palette commands,
  tools the model can call (next to MCP tools, same approvals), before-send
  prompt rewrites and code-block renderers through a small `owui` API
  (toast, notify, insert, send, openArtifact, clipboard, fetch, files,
  per-plugin storage). They hot-reload on save; Tools ▸ Plugins lists them
  with errors and logs, writes a documented example and can switch network
  or file access off. API reference: [docs/PLUGINS.md](docs/PLUGINS.md).
  `Open WebUI --plugintest` runs the host's offline checks.
- **Agent mode:** the brain icon in the composer turns the prompt into a
  goal: the model plans it into steps (JSON, capped), carries each step out
  with the MCP tool loop, judges its own outcome and retries with the
  feedback, then writes the answer. A step card shows the plan live with
  Pause (between steps) and Stop; each step opens to its outcome and the
  checker's objection; tool calls show as the usual approval cards. The run is
  saved with the message. Right-click the icon for the step cap, attempts per
  step and the outcome check. `Open WebUI --agenttest` checks the plan and
  verdict parsers offline.
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
