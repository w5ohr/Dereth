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
  apps). View ▸ Float on Top (⇧⌘T) makes any window float. All windows live
  in one process and share one chat list, connection and settings, while each
  has its own chat, composer, overlays and streams. They are native macOS
  windows, so they can be tabs: File ▸ New Tab (⌘T), the tab bar's +, "Open in
  new tab" in the same menus, Window ▸ Merge All Windows. A tab can be dragged
  out into a window of its own. Dragging a chat out of the sidebar past the
  window's edge opens it in a window where you drop it. Closing the main
  window while chat windows are open hides it rather than quitting. The
  artifact preview and the PDF viewer stay in the main window. `Open WebUI
  --windowstest` covers the view switching, the callbacks and the shared fonts.
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
- **Browser tool:** a built-in tool server named `browser` sits beside the
  MCP ones whenever MCP tools are on: `fetch` downloads a page (or PDF, JSON,
  text) and hands the model readable Markdown with the title; `render` loads
  it in a headless WebKit view so scripts run, then returns the visible text
  and links; `links` lists a page's links; `screenshot` saves a PNG of the
  first screen; `search` queries DuckDuckGo's HTML page (no key) and returns
  titles, URLs and snippets. Every call goes through the usual approval card.
  Only http(s) to public hosts unless "allow local hosts" is on; results are
  clipped to a size you set. The card under Tools ▸ MCP has Try fetch / Try
  render. `Open WebUI --browsertest` checks the URL policy and parsers.
- **Script tools:** drop a shell, Python, AppleScript, JXA, Ruby or Perl
  script (or any executable) into the support folder's `scripts/` with a
  short comment header - `owui-tool: name`, `description:`, one `param:`
  line per argument (`name type [required] - what it is`), optional
  `timeout:` and `sandbox: on|off` - and it becomes a tool under the built-in
  `scripts` server. Arguments arrive as JSON (last argument and `$OWUI_ARGS`)
  and as `$OWUI_ARG_NAME`; stdout is the result, a non-zero exit is an error.
  Sandboxed by default like Run: no network, writes only inside the working
  folder (the pinned project, else a scratch folder). The card under Tools ▸
  MCP lists what was found, flags broken headers, writes three examples and
  tries any tool with arguments. `Open WebUI --scripttest` covers it.
- **Search chats by meaning:** every chat in the local mirror is chunked and
  embedded on this Mac (nomic-embed-text through the Ollama URL) into a small
  SQLite index that fills a few seconds after launch and keeps up with
  changes once a minute, re-embedding only passages whose text changed. Tick
  "By meaning" under the sidebar search and type what a conversation was
  about; hits show the chat, who said it and the passage, and a click opens
  the chat on that message (switching branch if needed). Tools ▸ Local index
  shows the index, with Index now / Rebuild / Clear. Private chats stay out.
  `Open WebUI --chatindextest` covers the index with a fake embedder.
- **Memory proposals:** after a server reply the same model is asked, quietly,
  for durable facts you stated in that turn - preferences, work, projects,
  constraints. Up to three appear in a "Remember this?" card under the reply
  with Save / Edit / Skip; only Save sends anything to your Memories, skipped
  texts are remembered locally so they are not proposed again, and anything
  already in Memories is left out. Off under Settings ▸ Chat; "Propose
  memories from this reply" in the palette asks on demand. Private chats are
  never scanned. `Open WebUI --memorytest` covers the parsing and filtering.
- **Vault sync (Obsidian):** Settings ▸ Vault sync points at a folder,
  typically inside an Obsidian vault. Every chat in the local copy is
  written to `Chats/<title> (<id>).md` with YAML front matter (id, title,
  created, updated, models, tags, a link back to the server) and the current
  branch as Markdown, refreshed after each chat sync. Notes go both ways:
  `Notes/<title>.md` is written from the server, an edit made in Obsidian is
  sent back within seconds (a folder watch), a new `.md` dropped into Notes
  becomes a note, and when both sides changed the server's text wins while
  your edit is kept as a "(conflict ...)" copy. Private chats are never
  written. `Open WebUI --vaulttest` covers the file formats.
- **Health:** Tools ▸ Health shows CPU, GPU utilisation and GPU memory
  (IOKit's accelerator statistics), memory with the compressed and swapped
  amounts, Metal's working set, thermal state, power source and the free
  space where Ollama keeps its models, each sampled every second with a
  five-minute sparkline. Below, the models Ollama has in memory with size,
  how much sits in GPU memory, context length and when each unloads, an
  Unload button per model, and a residency timeline. A heavy-swap warning
  says when a model is being paged. `Open WebUI --healthtest` checks the
  sampler.
- **Model library:** Tools ▸ Models searches the Ollama library or Hugging
  Face GGUF repositories, lists each model's variants with their sizes
  (tags for Ollama, quantisations for Hugging Face, pulled as
  `hf.co/<repo>:<QUANT>`), and queues pulls that run one after another
  through the server's Ollama with a progress bar and Cancel. Before a pull
  is queued the free space where Ollama keeps models is checked, with
  headroom. The Ollama pages are scraped, so that side can break when the
  site changes; the Hugging Face side uses the Hub API. `Open WebUI
  --modellibtest` covers the parsers.
- **Budgets:** Tools ▸ Usage ▸ Budgets sets caps on tokens per chat, tokens
  per day and dollars per day (from the per-model prices entered there),
  with a warning threshold. Near a cap a toast warns once; past it, each
  send stops at a "Budget reached" sheet with Send anyway / Hold it / Open
  budgets, and the text goes back into the composer. While a cap is set the
  chat header shows a "budget NN%" pill whose tooltip has the numbers.
  `Open WebUI --budgettest` covers the arithmetic.
- **Warmup and residency:** Settings ▸ Warmup loads the selected Ollama
  model a few seconds after launch and renews its keep-alive at half the
  interval you set while the app is in front, so the first token comes at
  once; it is released before the Mac sleeps and warmed again on wake, and
  optionally released on quit. Off by default, since pinning a model on a
  shared Ollama holds its memory for everyone. Warm now / Release now
  buttons and palette commands; Tools ▸ Health shows what is resident.
  `Open WebUI --warmuptest` covers the schedule.
- **Message selection:** Shift-click a message header to select it, Shift-
  click another to take the whole range between them, or run "Select
  messages" from the palette for a checkbox beside every message. A bar at
  the bottom of the transcript copies the selection as Markdown, exports it
  to a file, moves it into a new chat (a fresh chat with just those
  messages, removed from here) or deletes it - the messages on either side
  of a deleted one are joined up so the conversation keeps its thread.
  Escape clears. `Open WebUI --selectiontest` covers the range, splice and
  export logic.
- **Low-power mode:** on by default, Settings ▸ Low power drops the window's
  frame rate to a trickle once there has been no input for a few seconds
  (a streaming reply, a toast or a download keeps it high), caps it on
  battery even while active, and on battery holds chat indexing, vault and
  chat syncs and model warmup until the Mac is plugged in. The card shows
  the live state and frame rate. `Open WebUI --lowpowertest` covers the
  policy.
- **Editor bridge:** a file reference in a reply - `src/app.cpp:120`,
  `/abs/path.h:3:4`, `~/notes.md:9`, in prose or backticks - is a link that
  opens that line in your editor: VS Code, Cursor, Zed, Xcode, Sublime Text
  or a custom command with `{file}` `{line}` `{col}` placeholders (Settings
  ▸ Editor). Relative paths resolve against the pinned project folder, then
  home. The other way, an editor task or keybinding sends a selection to a
  new chat with `owui://explain?file=...&line=...&text=...`;
  `docs/EDITOR-BRIDGE.md` has VS Code, Zed and Xcode recipes. `Open WebUI
  --editortest` covers the parsing and the commands.
- **Cited export:** a reply that cites sources - the server's web-search or
  knowledge citations, or locally read documents cited as `[name p.N]` -
  gains a book button beside Copy: copy the answer as Markdown with
  footnotes (`[1]` becomes `[^1]`, definitions with title, URL, page and the
  retrieved passage appended), copy the sources as BibTeX `@misc` entries
  with an accessed date, or copy a plain source list. The palette has the
  same for the last reply. `Open WebUI --citetest` covers the conversions.
- **Local text-to-speech fallback:** Read aloud works without a server TTS
  engine: in Auto the app asks the server once and, when it has none, reads
  with the Mac's own voices (AVSpeechSynthesizer) for the rest of the session.
  Settings ▸ Voice and read-aloud picks the engine (server then Mac, server
  only, Mac only), the Mac voice (enhanced or premium voices are preferred
  over the plain ones; the system voice otherwise), rate and pitch, and
  whether code blocks are announced instead of read. Markdown is turned into
  something worth hearing first: headings, emphasis, list and table markup,
  citation markers and link URLs go, bare URLs become "link to host".
  Read-aloud requests queue per message (the button turns into Stop while a
  reply is queued or playing, the status line under the composer shows what
  is left), and the palette has "Read the last reply aloud", "Read this chat
  aloud" and "Stop reading aloud". `Open WebUI --ttstest` covers the
  conversion, chunking, voice choice and the queue.
- **Traffic inspector:** Tools ▸ Traffic is a live, HAR-style log of every
  request the app makes and every socket.io packet - time, method, host,
  path or event, status, size, latency and a waterfall bar - with pause,
  filter, a heartbeat toggle and a summary line (failures, p50/p95, bytes,
  slowest call). Selecting an entry shows its request and response bodies
  (the first 16 KB each, tokens, keys and passwords masked; headers are
  never kept) with Replay (same server), Open in API console (prefilled),
  Copy as curl (token as `$OWUI_TOKEN`), Copy request/response. Export HAR...
  and Copy HAR write a HAR 1.2 file that Chrome, Firefox and Charles open,
  socket packets included as a websocket entry. The newest 1000 entries
  stay in memory; Capture bodies can be turned off. `Open WebUI
  --traffictest` covers masking, packet naming, filtering, the summary, the
  waterfall geometry, HAR and curl.
- **Retrieval inspector:** a reply that came with retrieved chunks gets a
  layers button under it that opens a side column listing every chunk the
  server retrieved - source, page, the server's relevance score as a bar,
  the passage - ranked best first, with a mean/best summary. Pin the
  passages the answer should rest on or exclude the noise (marks are saved
  with the chat), open a chunk in the document view, copy the passages, and
  Re-ask: the question goes back with only the chosen passages quoted and
  the server's own retrieval is skipped for that turn. The palette opens it
  for the last reply. `Open WebUI --retrievaltest` covers the parsing,
  ranking, marks, stats and the re-ask prompt.
- **Playgrounds:** a regex, SQL or jq block in a reply (tagged, or
  recognisable by shape) gets a "Try it" button that opens a playground: the
  code on the left, your input on the right, and the result below - all run
  on this Mac. Regex (std::regex, bare or `/pattern/flags`) lists every match
  with its span and capture groups and applies an optional replacement; SQL
  runs in an in-memory SQLite that holds your CSV as a typed table named
  after the file (or read-only against a .sqlite/.db file you load), showing
  each statement's rows, the schema, timing and changes; jq runs the system
  or Homebrew `jq` in a sandbox over the JSON you paste or load. Sample data
  is one click away, Cmd+Enter runs, and the result can be copied as text or
  Markdown or sent to the composer with "Ask about the result". The palette
  opens an empty playground. `Open WebUI --playgroundtest` covers kind
  detection, the regex and SQL runners, CSV parsing, the jq command line and
  the formatters.
- **Model evaluation:** Tools ▸ Evaluate runs a saved prompt set across the
  models you tick and grades every answer 0-10 with a judge model (any model
  on the server; the reference answer or rubric you wrote for each prompt
  goes to the judge), or by hand with Good/Bad thumbs that override the
  grade. The latest run shows as a prompt × model matrix with grade, time and
  the judge's reason on hover; a cell opens the full answer. Trends plot each
  model's per-run mean so you can see a model, quantisation or prompt change
  move the score. Sets and scores stay local (support/eval); CSV export and
  copy. A starter set of five prompts comes built in. `Open WebUI --evaltest`
  covers the set and score formats, the judge prompt and its parser, the
  matrix, trends and CSV.
- **Saved answers library:** the archive button under a reply saves it to a
  global shelf on this Mac (support/library.json) - pins stay per chat, this
  is across every chat. The Library pane in the rail searches text, notes,
  chat titles and models (a `#tag` word narrows to a tag), shows tag chips
  with counts, lets you add a note and tags to each entry, reopen the chat
  at that message, copy, or remove, and exports the shown entries as
  Markdown (file or clipboard). The palette has "Save the last reply to the
  library" and "Go to Library". `Open WebUI --librarytest` covers the store,
  search, tags and the export.
- **Smart folders and tags:** Settings ▸ Smart folders and tags holds rules
  that file chats into a folder (created when missing) and/or add a tag when
  the title, a model name or the first message contains something, when the
  chat carries a tag, or when it is older or newer than N days. Apply them by
  hand, or every ten minutes while signed in. "Auto-tag untagged chats" asks
  the small model (the auto-routing one) for one to three topic tags per chat
  that has none, fifteen at a time. Folders and tags are the server's, so the
  web UI sees them too. The palette has "Apply smart folder rules now" and
  "Auto-tag untagged chats now". `Open WebUI --foldertest` covers rules,
  matching, the plan, and the tagging prompt and parser.
- **Hybrid local search:** the local index now keeps an SQLite FTS5 table
  beside the vectors (filled by triggers, so existing indexes backfill on
  first open) and every search fuses the BM25 text ranking with the vector
  ranking by reciprocal rank, with a bonus when a chunk contains every query
  token - so an exact identifier like `recalibrate_foghorn_compressor` is
  found even when the embedding misses it, and search keeps working when
  Ollama is down (text matches only). Each hit says whether it came from
  both lists, the vectors or the text; the Hybrid checkbox in Tools ▸ Local
  index turns it off. `Open WebUI --hybridtest` covers tokenising, the FTS
  table and triggers, BM25 search and the fusion.
- **Shortcut editor:** Settings ▸ Keyboard shortcuts lists every binding -
  palette, Quick Ask, panes, sidebar, find, compare, voice, stop, export,
  capture and more. Click one and press the new keys (Escape cancels,
  Backspace unbinds); the native menu items follow, two actions on the same
  keys are flagged red with the other named on hover, and Reset puts a
  binding or all of them back. ⌘/ opens a cheat sheet. Vim keys for the
  transcript (optional): j / k move between messages, gg / G jump to the
  ends, / finds, y copies, i returns to the composer, with r, p, s and e as
  before. Overrides live in settings (`keymap`). `Open WebUI --keymaptest`
  covers chord parsing, defaults, overrides, conflicts, the menu key
  equivalents and the Vim state machine.
- **Theme editor:** Settings ▸ Theme editor shows the fifteen palette
  colours of the current theme (dark, light or midnight; "system" edits the
  one in use) as colour wells that apply as you drag, with the hex beside
  each, Reset per colour and Reset all. Import a VS Code theme (JSON with
  comments) or a Base16 scheme (YAML or JSON) to fill the slots at once;
  Copy as JSON exports them. A server accent gives each server its own
  accent colour so the header, buttons and highlights tell you which
  instance you are on. Overrides live in settings (`themeOverrides`,
  `serverAccents`). `Open WebUI --themetest` covers hex parsing, the slot
  table, overrides, the importers and the export.
- **Onboarding:** the first sign-in starts an eight-step tour - a card
  beside a spotlight on the rail, the chat list, the composer, the header,
  the palette, Tools and Settings - with Back, Next and Skip, and your place
  is kept if you quit midway. After an update a "what's new" sheet lists the
  release notes you have not seen yet, once. Settings ▸ Getting started brings
  the tour, the release notes and the ⌘/ cheat sheet back any time, and the
  palette has "Take the tour" and "What's new". The app version is now 1.1.
  `Open WebUI --onboardtest` covers the steps, the saved state, version
  comparison and which release notes are pending.
- **Reading mode:** the book button in the chat header (or ⌘⇧R, or the
  palette) narrows the transcript to a reading measure, enlarges the text,
  adds line spacing, tucks the sidebar away and hides the buttons, chips
  and follow-ups under each message until the pointer rests on it (or the
  message is focused). Settings ▸ Reading mode sets the measure, text size,
  line spacing and the two behaviours. `Open WebUI --readingtest` covers the
  preferences and the layout decisions.
- **Virtualised transcript:** a chat with thousands of messages scrolls
  like a short one. Each message's height is remembered once it has been
  laid out, and any message well outside the view is replaced by a spacer
  of that height instead of being rendered (streaming, focused, edited,
  selected and search-hit messages are always drawn). Scroll positions,
  the "new text" pill and jump-to-message keep working, and images in
  far-away messages are not decoded until you scroll near them. Off switch
  in Settings ▸ Chat. `Open WebUI --virtualtest` covers the height memory
  and the skip decision; with OWUI_DEBUG the log reports "transcript: drew
  N of M messages in X ms" every two seconds.
- **Menu-bar-only mode:** Settings ▸ Mac integration ▸ "Menu-bar only"
  drops the Dock icon and app-switcher entry; the window still opens, and
  closing it keeps the app running in the menu bar extra (Quick Ask, New
  Chat, recent chats, Show Open WebUI, Quit), so the Option-Space hotkey and
  scheduled prompts keep working with no window. "Open at login" registers
  the app as a login item through the system's service manager (macOS 13+,
  approval may be asked for in System Settings ▸ Login Items) so the extra
  is there before any window. The palette toggles the mode. `Open WebUI
  --menubartest` covers the preferences and the status words.
- **Local reranker:** with "Re-rank hits with the small model" on (Tools ▸
  Local index), the hits that would go along with a message - from the
  local index's hybrid search - are first scored 0-10 by the auto-routing
  small model for how well they answer the question; the top few (Keep)
  above a floor (Drop below) are what reaches the prompt, in that order.
  The Local index tab shows each hit's reranked score and a line with the
  scores, model and time of the last pass; on any failure the search order
  is kept. `Open WebUI --reranktest` covers the prompt, the parser and the
  ordering.
- **Accessibility options:** Settings ▸ Accessibility has reduced motion
  (toasts stop fading, spinners stop spinning), high contrast
  (black-and-white surfaces with strong borders, text and accent laid over
  whichever theme is on), a UI font picker over the fonts on this Mac -
  ~/Library/Fonts, /Library/Fonts and the system extras, with dyslexia- and
  low-vision-friendly faces such as OpenDyslexic, Atkinson Hyperlegible,
  Lexend, Verdana, Tahoma or Comic Sans listed first when installed, bold
  partners paired - and a UI scale slider (0.8x to 1.6x) that scales the
  whole interface, applied live. `Open WebUI --a11ytest` covers the
  preferences, the font list and the contrast sets.
- **Multi-server pool:** Settings ▸ Backend pool gathers any of the direct
  backends (the Ollama URL, LM Studio, vLLM, a second Ollama box, ...) behind
  one "Pool (N servers)" entry that private chats pick like any backend. Every
  30 s (5-600) the app probes each member - `/api/ps` for Ollama, `/models`
  for OpenAI-compatible - and the card shows a health table: alive with the
  loaded-model count, this app's in-flight requests and the probe latency, or
  down with the reason. Each request goes to the least-loaded alive member
  (in-flight first, then loaded models, then latency), or round-robin, or the
  first alive in the configured order; when a member fails before producing
  any output the request fails over to the next member, the member is marked
  down and the debug log says so. The pool's model list is the union of its
  members'. "Check now" re-probes; the card shows where the last request went.
  `Open WebUI --pooltest` covers the preferences, the probe parser, the three
  routing orders and the health text.
- **Grammar and tool-choice controls:** the sampling panel (header gear)
  ends with "Output and tools" for the chat: Free text, JSON object or a
  GBNF grammar, and a tool choice of Auto, None, Required or "Force <tool>"
  for every connected MCP tool. JSON mode sends Ollama `format: json` /
  `response_format: json_object` plus a system line for models that only
  follow the prompt; the grammar box validates as you type (every rule
  `name ::= ...`, a `root`, no undefined references, balanced quotes and
  brackets) and goes out as `grammar`, which llama.cpp-style servers honour
  and Ollama ignores; the tool choice goes out as `tool_choice` to OpenAI-
  and Anthropic-style backends, while on Ollama the model is told to call
  the tool in a system line and the request after the call runs free. None
  leaves tools out altogether. The choice is saved in the chat and shows in
  the gear's highlight. `Open WebUI --outputtest` covers the record, the
  validator, the wire shapes and the backend mappers.
- **Token confidence (logprobs):** Settings ▸ Chat "Ask for token
  probabilities" adds `logprobs` and `top_logprobs` (1-8 alternatives) to
  every request. When a backend answers with them - Ollama 0.12+ and
  OpenAI-style servers do, directly or through the server for OpenAI-style
  models - the reply's action row gains a layers button that swaps the
  rendered text for the token stream, each token tinted green (sure), amber
  (unsure) or red (under 50 %), with a summary line (mean confidence, tokens
  under 50 %, the least confident token) and, on hover, the alternatives the
  model weighed with their own percentages. Thinking tokens are left out,
  the probabilities are saved with the message (`desktop_logprobs`) so the
  view survives a relaunch, and the view notes when the tokens no longer
  match the text shown. `Open WebUI --logprobtest` covers the parsers, the
  compact storage, confidence, tints, stats, tooltips and the request
  passthrough.
- **File editor:** a real editor window for the files Apply touches. "Edit"
  beside each file in the Apply dialog, "Open a file in the editor..." and
  "Open the last applied file in the editor" from the palette open files in
  tabs with syntax highlighting (the highlighter painted over a native
  multiline input, so typing, selection and ImGui's own undo/redo - Cmd+Z /
  Cmd+Shift+Z - stay intact), a line-number gutter, find (Cmd+F, Enter for
  the next match), go to line, Save (Cmd+S), Revert to disk and a modified
  marker on the tab. Every apply records the file before and after it, and
  every save records it too, in a per-file history (support/editor-history)
  shown beside the editor: each snapshot with its label, line count and
  age, the first line where it differs from the editor's text, "Go to" and
  "Restore" (the current text is snapshotted first), so you can step back
  across applies, not just the last one. `Open WebUI --fileeditortest`
  covers the history store, language detection and the line / search
  helpers.
- **Layout presets:** Settings ▸ Layouts, the palette ("Layout: ...") and
  Cmd+Option+1 to 4 apply a named arrangement of the chat screen: the
  sidebar and its width, which side column shows (reference chat, pins,
  conversation tree, artifacts) and whether it sits beside the transcript or
  below it as a horizontal split, reading mode, float-on-top and the window
  size. Five built-ins - Zen (no sidebar, reading), Wide chat, Three columns
  (sidebar, chat, reference), Split (reference below), Review (pins) - plus
  your own: "Save current layout" captures the arrangement under a name,
  saved ones can replace a built-in by reusing its name, and Delete removes
  them. A reference layout with no chat remembered opens the most recent
  other chat. `Open WebUI --layouttest` covers the built-ins, the record,
  saving and the summaries.
- **Image gallery with generation metadata:** every image the Images pane
  generates is saved under support/images with its prompt, negative prompt,
  model, size, seed (when the backend reports one) and engine written into
  the PNG as text chunks, and listed in a gallery under the generator: a
  grid of thumbnails with a size slider, search over prompts / models /
  seeds, favourites (a star on the tile, "Favourites only"), Reuse (puts the
  prompt, negative prompt, model and size back into the generator), Open,
  Reveal in Finder, Copy parameters (an Automatic1111-style line) and
  delete, plus a details card for the selected image. Drop PNGs from
  Automatic1111 (its `parameters` chunk) or ComfyUI (its `prompt` graph:
  seed, checkpoint, size, positive and negative text) into the folder and
  "Rescan folder" reads their metadata. `Open WebUI --imagemetatest` covers
  the PNG chunk reader / writer (CRCs checked), both foreign formats, the
  index and the filter.
- **Knowledge graph:** Tools ▸ Graph (or the palette) pulls names, terms,
  #tags and `code` out of your memories, notes and knowledge-base files
  (capitalised runs of up to four words, stop words and sentence-starts
  dropped), links each entity to the sources that mention it and to the
  entities it shares a source with, and draws the result as a node graph
  laid out by a small force simulation: drag nodes (they stay put), drag the
  background to pan, hover for mention counts, click an entity for its
  sources and companions or a source for its text, find a node by name, set
  the minimum mentions and the entity cap, switch the entity-entity links
  off, Shake to relayout, Export JSON. `Open WebUI --graphtest` covers the
  extraction, the graph build, the options and the layout.
- **Sketch input:** the pencil in the composer (or "Sketch input" in the
  palette) opens a drawing canvas - seven colours, pen and eraser, a width
  slider, Undo / Redo (Cmd+Z / Cmd+Shift+Z), Clear and a stroke count. Attach
  renders the drawing at 1.5x with anti-aliased round strokes, trims it to
  what was drawn (switchable), encodes a PNG and adds it to the message as an
  image, optionally with a question; pick a vision model to send it to. The
  drawing stays in the window until cleared. `Open WebUI --sketchtest`
  covers the strokes, undo / redo, the rasteriser (including joints), the
  trim and the PNG encoder, decoded back with stb_image.
- **LSP context:** with a project folder set (the composer's Project
  button) and Settings ▸ LSP context on, identifiers a question names -
  `backticked`, `a::b`, `a.b`, snake_case, camelCase and `f()` calls - are
  looked up with the folder's language servers before the message goes out,
  and each match's signature, doc comment (hover) and definition source (up
  to 40 lines) are added as a system message. clangd, pyright,
  typescript-language-server, gopls and rust-analyzer are used when
  installed; the card shows which were found and has a "try a question" box
  that shows the exact block. Servers start on the first lookup and stay up;
  clangd runs without its background index so nothing is written into the
  project (a `compile_commands.json` in `build/` gives it real include
  paths). `Open WebUI --lsptest` covers the framing, the identifier scan,
  symbol flattening, hover text and the block, plus a live clangd round
  trip when clangd is installed.
- **Notebook mode:** Tools ▸ Notebook (or the palette) holds code, prompt
  and text cells over a persistent Python kernel - the system python3
  running a small driver, so no Jupyter install is needed. Shift+Enter runs
  a cell; stdout, stderr, the last expression's value, tracebacks (driver
  frames hidden) and matplotlib figures (as PNG, when matplotlib is
  installed) show inline with `In [n]` counts. Prompt cells go to the
  selected model with the notebook so far as context, and "Insert code
  below" turns the code in an answer into the next cell. Run all (stops at
  the first error), Interrupt, Restart kernel, a two-minute cell timeout,
  markdown cells rendered until double-clicked. Notebooks save as .ipynb
  (nbformat 4.5) under support/notebooks, reopen where you left off, and
  import / export as files Jupyter reads; prompt cells export as readable
  markdown with the prompt and answer kept in metadata. `Open WebUI
  --notebooktest` covers the format, the context and a live kernel.
- **Emoji and wide characters:** text is decoded as full 32-bit Unicode, so
  emoji and mathematical alphanumerics (𝑥, 𝔽) no longer collapse to `?`
  boxes. Colour emoji are drawn by Core Text from Apple Color Emoji into the
  font atlas as coloured glyphs, in every face (chat, bold, code, titles);
  characters Unicode shows as emoji by default (✅ ⭐ ☕ ⏳ and the heart)
  prefer the emoji font, text-default symbols (⚠ ✔ →) stay monochrome, CJK
  comes from Arial Unicode and maths letters from STIX Two Math. Layout is
  one codepoint at a time, so sequences are approximated: variation
  selectors and joiners take no space, skin-tone modifiers are dropped (the
  base emoji shows), a family shows its members side by side and a flag
  shows its two letters (GB). `Open WebUI --emojitest` builds a real atlas
  and checks which font each character comes from and its colour.
- **CI:** `.github/workflows/owui-desktop.yml` builds the app on a GitHub
  macOS runner for every push to main and every pull request that touches
  `owui-desktop/`, then runs `scripts/selftests.sh`, which finds every
  `--<name>test` flag in `src/main.mm` and runs each in a throwaway HOME with
  the keychain off and a time limit, printing a table (and writing it to
  the job summary). Run it locally the same way after a build:
  `scripts/selftests.sh` (add `--only name,name` or `--skip name`). The
  integration suite `--selftest` needs a live Open WebUI, so it runs only
  when `OWUI_SERVER` (and `OWUI_TOKEN`, or `OWUI_EMAIL` + `OWUI_PASSWORD`)
  is set; otherwise a check that it exits cleanly without a server runs in
  its place.
- **Crash reports and packaging:** if the app crashes it writes a report
  (signal, the faulting address or uncaught exception, a demangled stack and
  the last few log lines) to the support folder's `crashes/`, then dies as
  before so macOS keeps its own report too. On the next launch new reports -
  the app's and macOS's `.ips` for it - open in a sheet with the reason and
  the top of the stack: View full report, Copy, Save as, Show in Finder and
  Report on GitHub, which only opens a prefilled new-issue page (version,
  signal, function names; no activity lines, home paths shown as ~). Settings
  ▸ Crash reports lists past ones. `scripts/make-dmg.sh` builds
  `build/dist/Open-WebUI-<version>.dmg` (the app plus an Applications link,
  compressed, verified, mounted and run to check it) and a filled-in Homebrew
  cask from `packaging/homebrew/`. `Open WebUI --crashtest` crashes child
  processes four ways and reads the reports back.
- **Auto-update:** Settings ▸ Updates checks an appcast (Sparkle's RSS
  format; by default the latest GitHub release's `appcast.xml`) once a day
  or on demand, shows the newest version with its size and Markdown release
  notes, and Install and relaunch downloads the DMG, checks its size and
  SHA-256 against the feed, mounts it, checks the app inside is this same
  app at the promised version with a valid signature, swaps it into place
  and relaunches. The version it replaced is kept, so "Roll back to ..."
  (in the card and on the "Updated to ..." sheet shown after relaunching)
  puts it back, and rolling back is itself reversible. Skip this version
  hides one release. Feeds must be HTTPS (or this Mac), and every item needs
  a SHA-256. `scripts/make-dmg.sh` writes the `appcast.xml` beside the DMG
  and cask. `Open WebUI --updatetest` parses feeds and does a real install
  and rollback from a DMG.
- **Audio transcription:** Tools ▸ Transcribe (or the palette) takes an
  audio file - opened, or dropped on the tab: wav, m4a, mp3, aiff, flac,
  caf, mp4 audio. It is decoded on this Mac with AVFoundation (no
  microphone, no permission), split into phrases at the pauses, and each
  phrase goes to the server's speech-to-text in turn, so every line carries
  its start time. A player plays, pauses and seeks; clicking a line plays
  from it, and the line being heard is highlighted and kept in view. Copy
  as Markdown, save .srt, .vtt or .md, or Send to chat. Transcripts are
  kept per file (by content hash), so reopening a file shows it at once;
  Transcribe again redoes it. `Open WebUI --transcribetest` covers the
  splitting, the WAV cutting, the exports, and decoding real files made
  with `say`.
- **Language and region:** Settings ▸ Language and region picks System,
  English, Deutsch, Español, Français or 日本語 - or a pseudo-locale that
  brackets and accents every translated string so what is still English
  stands out. The translation covers the app's frame: the rail, pane
  headers, sidebar, sign-in, composer, the empty chat with its suggestions,
  and every section header (103 strings); deeper text is English for now.
  Dates, relative times ("vor 2 Minuten", "2 分前"), numbers and file sizes
  follow the locale everywhere, using this Mac's region by default or the
  language's own formats when that box is ticked. Paragraphs that start in
  Hebrew, Arabic or another right-to-left script are right-aligned and put
  in reading order, with English words and numbers inside them kept
  left-to-right. `Open WebUI --i18ntest` covers the table, lookup, the
  formats and the reordering.
- **Image editing:** Edit on any gallery tile (or "Edit an image..." in the
  palette) opens an editor working against the ComfyUI at Settings ▸
  Connection ▸ ComfyUI URL. Inpaint: paint over what should change (Option
  erases the mask, Cmd+Z undoes), describe what goes there, and only the
  painted area changes - the result is pasted back through the mask, so the
  rest stays pixel-exact. Img2img with a strength slider, Variations (up to
  eight from one seed), and Upscale 2x / 4x (lanczos, optionally with a light
  detail pass by the model). The model list comes from ComfyUI (SDXL is
  preferred for inpainting; Flux gets its own steps and cfg); images run at
  the model's working size, about one megapixel for SDXL and Flux. Results
  can be saved to the gallery with prompt, model, seed and mode in the PNG,
  edited further, or saved anywhere. Uploads and results use ComfyUI's temp
  folder only. `Open WebUI --imgedittest` covers the workflows, presets,
  mask, sizes and ComfyUI's replies.
- **Smooth streaming:** words arriving in a streamed reply fade in over a
  quarter of a second instead of popping in, so the reply reads as it grows.
  Each message keeps a timeline of when each character first appeared;
  finished messages draw at full strength with no extra cost. Settings ▸
  General ▸ "Fade in streamed text" turns it off, and Reduced motion turns it
  off too. Auto-scroll that stops when you scroll up, the "New text" pill and
  the per-chat scroll position were already there. `Open WebUI --fadetest`
  covers the timeline: arrivals, the smoothstep curve, settling, regenerating
  and forgetting.
- **VoiceOver:** the window now has a real accessibility tree. Every button,
  check box, text field, tab, menu item, disclosure and pop-up the UI draws is
  an element with its name, state and position, and icon-only buttons use
  their tooltip as their name. The window is split into regions
  (Navigation, Chat list, Conversation or the open pane, the message field).
  Each message is a group named by who wrote it and when, holding its plain
  text, so VoiceOver can read a whole conversation, scrolled-away messages
  included. Pressing an element clicks it. A modal hides the rest of the
  window. Finished replies and toasts are announced. Keyboard: Cmd+Alt+] and
  Cmd+Alt+[ move between regions and say where you landed, Cmd+L returns to
  the message field, and arrow keys walk the messages, which VoiceOver
  follows. `Open WebUI --voiceovertest` covers the tree, the ImGui hooks and
  the Cocoa elements.
- **Mac data tools:** with tools on, the model can list and add Calendar
  events, list and add Reminders, search Contacts by name or email, and open a
  Mail draft for you to review and send. It never sends mail itself. Each kind
  is off until you switch it on under Tools > MCP > Mac data. macOS asks for
  access once, from that card's Allow button, and never from a chat. If access
  is missing, the model is told where to grant it. Every call still goes
  through the approval card. `Open WebUI --personaltest` covers the tools on a
  fake calendar and contact list.
- **Personal corpus:** an opt-in index of your Notes, Mail (the last year by
  default), Safari Reading List and Messages (the last 180 days by default),
  under Tools > Local index. It's embedded with nomic-embed-text on your
  Ollama, stored in the app's folder and never uploaded. Messages are indexed
  one conversation-day at a time, and junk and trash are skipped. The model
  can search it with the corpus__search tool. The passages it finds go to the
  model you're chatting with. Reading these files needs Full Disk Access for
  Open WebUI, and nothing is read until you press Index now. Re-indexing
  embeds only what changed and drops what's gone. `Open WebUI --corpustest`
  checks the readers against a synthetic Library.
- **Screen watch:** under Tools > Screen watch (or "Watch a window" in the
  palette), pick a window and ask a standing question, such as "Has the build
  finished?". Every 10 seconds to 10 minutes the window is captured. Either
  its text, read with Live Text and sent to any model, or the screenshot,
  sent to a vision model, goes to the model with the question. A YES gives a
  toast, plus a macOS notification if you turn that on, and stops the watch.
  An unchanged window isn't asked about again until a limit passes. Other
  apps' windows need Screen Recording, and the app's own windows don't.
  `Open WebUI --screenwatchtest` covers the prompt, verdicts, change
  detection and a real capture of the test's own window.
- **Camera:** File > Take Photo..., or "Take a photo" in the palette, opens a
  camera sheet. It shows a live preview from the Mac's camera, an external
  one, or an iPhone through Continuity Camera, listed first when it's near.
  Take a photo, then attach it to the message for a vision model, or retake
  it. Photos go out upright at most 1568 pixels on the long side. The camera
  runs only while the sheet is open, and macOS asks for access from the
  sheet's Allow button. `Open WebUI --cameratest` covers device choice and
  the image pipeline on synthetic frames, without opening a camera.
- **Meeting notes:** Tools > Meeting notes, or the palette, takes what your
  Mac plays, such as a call, webinar or video. Your microphone can be added
  if you like. Recordings work too. Speech goes to your Open WebUI server's
  speech-to-text in pieces cut at pauses. You get a live, timestamped
  transcript, and every couple of minutes the model updates the notes: a
  summary, decisions and action items. Stopping writes the final notes.
  "Copy as Markdown" copies everything, and "Save as a note" puts it in the
  Notes pane. System audio needs Screen Recording, and the app's own sound
  is left out. `Open WebUI --meetingtest` covers the resampling, the pause
  cutting and the notes.
- **Push-to-talk and voice detection:** switch on "Hold Control-Option-Space
  to talk" under Settings > Voice and read-aloud. Then hold the keys from any
  app, speak, and let go. What you said is transcribed and sent, or put in
  the message field if you prefer, and a quick tap is ignored. Voice
  conversation ends your turn with a detector that learns the room's noise
  and waits out a pause you set. An optional wake phrase ("hey open webui")
  is recognised on this Mac, so nothing leaves it until the phrase is heard.
  Then the question after it is sent, or the app listens for one.
  `Open WebUI --voicetest` covers the detector, the hold rules and the
  phrase matching.
- **Focus-aware notifications:** under Settings > Notifications. While a
  Focus is on, or you're in a meeting, finished-reply, screen-watch and
  plugin notifications are held. A meeting means another app is using the
  microphone or camera, or a calendar event is happening now. Once you've
  been free for a minute, they arrive as one digest, such as "While you were
  in a meeting: 2 replies finished: ...". Screen-watch alerts can still come
  through during meetings. macOS asks for notification and Focus-status
  access only from that card's buttons. `Open WebUI --notifytest` covers
  what's held, the settle time and the digest.
- **Auto mode for tool calls:** like Claude Code's. Next to the tools button
  in the composer, a chip shows how tool calls are approved: ask, auto or
  all. Click it to cycle, or use the palette or Tools > MCP. In auto mode,
  calls that only read run on their own. Clearly destructive ones wait for
  you, as do calls that don't fit what you asked for. That covers deleting,
  rm -rf, sudo, force-pushing, dropping tables, touching passwords or keys,
  writing outside the project or into system folders, and a tool its server
  marks destructive. You can choose to block those outright instead. Any
  other call goes to a short check by a model, the judge. It weighs the call
  against your own request, so a web page or tool result can't talk the
  assistant into something. Each tool card says what auto mode decided and
  why. Private chats use the rules only, so nothing leaves the Mac.
  "Always allow" and "never" per tool still come first. `Open WebUI
  --automodetest` covers the rules, the danger scan and the judge.

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
