# Feature plan 2: the next hundred

Goal: take the client from "a native front end with fifty extras" to the best
desktop AI client there is. Everything in `README.md` and `FEATURE-PLAN.md`
(the base client and features 1-50) is shipped and is NOT repeated here; each
item below is new. Numbering continues from the first plan (51-150).

Conventions, verification recipe, scratch-server setup and the one-owner-per-
file rule are unchanged from `FEATURE-PLAN.md`. Two additions:

- **Swift extension target.** Items marked `[swift]` need an app extension or
  App Intents, which cannot be written in C++/ObjC. They share one small Swift
  target (`extensions/`) built by the same CMake tree; it is the only place
  Swift is allowed.
- **Still no third-party dependencies** beyond `third_party/`. SQLite FTS5,
  PDFKit, Vision, ScreenCaptureKit, EventKit, JavaScriptCore, WebKit and
  IOKit are all system frameworks and are fair game.

Legend: ★ flagship (defines "best client"), S/M/L rough size.

## The list

### I - Conversation power tools (51-60)

| # | feature | size |
|---|---|---|
| 51 | ★ **Fan-out**: send one prompt to N models at once, answers in columns, keep the one you like as the canonical branch | M |
| 52 | **Quote-reply**: select text in any message -> "Reply to this"; the quote rides along with the next prompt and renders as a callout | S |
| 53 | **Branch tree view**: a graph of the whole conversation tree (sibling nav exists; the map does not), click a node to jump, prune dead branches | M |
| 54 | **Drafts**: composer text and attachments autosave per chat and survive relaunch; sidebar shows a pencil on chats with a draft | S |
| 55 | **Prompt variables**: `{{clipboard}}`, `{{selection}}`, `{{date}}`, `{{file:path}}`, `{{input:Label}}`; a fill-in form pops before send | S |
| 56 | **Prompt history ring**: ↑ in an empty composer recalls earlier prompts shell-style, across chats, with ⌃R fuzzy search | S |
| 57 | **Smart paste**: a paste over N lines becomes an attachment chip with preview; a pasted URL offers fetch-and-summarise or capture-to-knowledge | S |
| 58 | ★ **Structured output mode**: supply a JSON schema, validate the reply, render it as a table or tree, auto re-ask on violation | M |
| 59 | **Stop, edit, continue**: pause a stream, edit the partial answer, resume generation from the edited tail (assistant prefill) | M |
| 60 | **Steering chips** under every answer: shorter, more detail, as a table, as code, translate, ELI5 - one click, no typing | S |

### II - Rendering and reading (61-70)

| # | feature | size |
|---|---|---|
| 61 | ★ **Artifact pane**: HTML/SVG/Mermaid/React-less JS in a reply renders live in a side WKWebView; edit, re-run, save, open in browser | L |
| 62 | **Math and diagrams**: LaTeX via KaTeX in the artifact view, Mermaid and Graphviz-dot blocks rendered inline as images | M |
| 63 | **Reading mode**: centred, measure-limited transcript; font, size, line-height controls; hides every control until hover | S |
| 64 | **Virtualised transcript**: 10k-message chats scroll at 120 fps - render only visible messages, lazy image decode, incremental markdown | M |
| 65 | **Smooth streaming**: token fade-in, auto-scroll that stops when you scroll up and shows a "new text ↓" pill, scroll position remembered per chat | S |
| 66 | **Native composer**: an NSTextView overlay for the input - proper IME/CJK, dictation, spellcheck, autocorrect, emoji picker, trackpad text selection | L |
| 67 | **Text rendering**: FreeType rasteriser, emoji and CJK font fallback, user font choice, Retina-correct hinting | M |
| 68 | **Theme editor**: edit any colour live, import VS Code / Base16 themes, per-server accent colour so you always know which instance you are on | S |
| 69 | **Layout presets**: zen, 3-column (sidebar/chat/reference), horizontal split, detachable panes; save and recall layouts | M |
| 70 | **Message selection**: shift-click a range of messages -> bulk copy, delete, export, or "move to new chat" | S |

### III - Models and inference control (71-80)

| # | feature | size |
|---|---|---|
| 71 | ★ **Sampling panel** per chat: temperature, top_p, top_k, min_p, seed, num_ctx, repeat penalty, stop sequences; presets; seed lock for reproducible runs | S |
| 72 | **Model cards**: pull Ollama `show` metadata (family, params, quant, context, template, licence) into picker tooltips; quant comparison table | S |
| 73 | **Model library browser**: search ollama.com and Hugging Face GGUF from the app, queue pulls, disk-space check before pulling | M |
| 74 | **Direct backends**: talk to Ollama, llama.cpp server, LM Studio or an OpenAI/Anthropic-compatible API directly (keys in the keychain), no Open WebUI required | L |
| 75 | **Multi-server pool**: several Ollama/OWUI servers with health checks, failover and least-loaded routing (profiles switch; a pool routes) | M |
| 76 | **Context strategy picker**: drop-oldest, rolling summary, or sliding window when a chat outgrows `num_ctx`; preview what will be cut | M |
| 77 | **Warmup and residency**: preload the default model on launch, keep alive while the app is frontmost, release on sleep | S |
| 78 | **Logprob view**: when the backend returns logprobs, colour tokens by confidence and show alternatives on hover | M |
| 79 | **Token budgets**: per-chat and per-day token/cost caps with warnings; cost estimate for paid APIs, energy estimate for local | S |
| 80 | **Grammar / tool-choice controls**: force a tool, JSON mode, or a GBNF grammar on backends that support it | S |

### IV - Agents and automation (81-90)

| # | feature | size |
|---|---|---|
| 81 | ★ **MCP client**: connect to Model Context Protocol servers (stdio and HTTP), expose their tools, resources and prompts to the model | L |
| 82 | ★ **Tool-call cards**: collapsible call/args/result cards with timings; approval prompts (allow once / always / never per tool) | M |
| 83 | **Agent loop**: "do this until done" - plan, act with tools, verify; a step list with pause/resume, step budget and a stop button | L |
| 84 | **Recipes**: chain prompts (A's output feeds B), save as a recipe, run from the palette, a hotkey or a schedule | M |
| 85 | **Watch folder actions**: a file landing in a folder triggers a recipe (summarise the PDF, rename, tag, file it) | S |
| 86 | **Clipboard watcher**: optional mode from the menu bar extra that runs a recipe on every copy (translate, explain, clean up) | S |
| 87 | **Script tools**: user-defined shell / AppleScript / JXA scripts with argument schemas the model can call, sandboxed like Run | M |
| 88 | **Browser tool**: a headless WKWebView the model can ask to fetch, render, screenshot and extract a page | M |
| 89 | **Personal-data tools**: Calendar, Reminders, Contacts and Mail-draft tools through EventKit/Contacts, each gated by its own permission | M |
| 90 | `[swift]` **Shortcuts App Intents**: real Shortcuts actions (Ask, Summarise, Run Recipe) with typed inputs and outputs | M |

### V - Developer workflow (91-100)

| # | feature | size |
|---|---|---|
| 91 | ★ **Terminal pane**: a real PTY (forkpty) with ANSI rendering so commands are interactive; select output -> "Ask about this" | L |
| 92 | **Git pane**: status, stage, AI-written commit messages, explain a diff, blame a line, draft a PR description | M |
| 93 | **Repo map**: a compact symbol index (ctags-style regexes per language) sent instead of whole files - project context at a tenth of the tokens | M |
| 94 | **Multi-file apply**: a reply with several file blocks applies as one transaction with rollback and a combined diff | M |
| 95 | **Test loop**: detect pytest / ctest / npm test / swift test, run, feed failures back as a "fix these" prompt until green or budget spent | M |
| 96 | **Editor bridge**: open `file:line` in Xcode / VS Code / Zed; editor extensions send "explain selection" through `owui://` | S |
| 97 | **LSP context**: launch clangd / pyright / typescript-language-server, pull definitions and hover text for symbols named in the question | L |
| 98 | **Notebook mode**: prompt and code cells with a persistent Python kernel, outputs and plots inline, export to .ipynb | L |
| 99 | **Playgrounds**: run the model's regex against sample text, its SQL against an attached SQLite/CSV, its jq against JSON - instantly, locally | M |
| 100 | **File editor**: a real editor for Apply targets with syntax highlighting, an undo stack across applies, and a per-file apply history | M |

### VI - Knowledge and retrieval (101-110)

| # | feature | size |
|---|---|---|
| 101 | ★ **PDF viewer with anchored citations**: PDFKit view beside the chat; click a citation, the page scrolls and the passage highlights | M |
| 102 | **Hybrid local search**: SQLite FTS5 BM25 fused with the vector index (reciprocal rank fusion); exact identifiers stop getting lost | M |
| 103 | **Native document chat**: drop PDF/DOCX/PPTX/XLSX, extract text locally (PDFKit, textutil, zipped XML) with page refs - no upload needed | M |
| 104 | **Retrieval inspector**: which chunks were retrieved, their scores, pin or exclude a chunk, re-ask | S |
| 105 | **Local reranker**: a small local model re-scores retrieved chunks before they reach the prompt | S |
| 106 | **Personal corpus**: opt-in on-device index over Apple Notes, Mail, Safari Reading List and Messages, never uploaded | L |
| 107 | **Memory proposals**: auto-extract candidate long-term memories from chats and offer accept/edit/reject | M |
| 108 | **Knowledge graph**: entities and links across memories, notes and knowledge bases, drawn as an ImGui node graph | M |
| 109 | **Semantic chat search**: embed your own chat history locally and search it by meaning, across every chat | M |
| 110 | **Cited export**: copy an answer with footnoted sources in Markdown or BibTeX | S |

### VII - Multimodal (111-120)

| # | feature | size |
|---|---|---|
| 111 | ★ **Screen capture to chat**: region/window capture from a hotkey, attached with OCR text, straight into the composer | S |
| 112 | **Screen watch**: pick a window; periodic captures go to a vision model with a standing question ("tell me when the build finishes") | M |
| 113 | **Camera capture**: AVFoundation photo capture for vision models, Continuity Camera from an iPhone | S |
| 114 | **Audio transcription**: drop an audio file, get a timestamped transcript, click a line to seek playback | M |
| 115 | **Meeting notes**: ScreenCaptureKit system-audio capture -> live transcript -> running summary and action items | L |
| 116 | **Image editing**: inpaint with a mask brush, img2img, variations, upscale against the configured image backend | M |
| 117 | **Generation metadata**: prompt, seed, model stored in PNG text chunks; "reuse prompt", favourites, grid view | S |
| 118 | **Local TTS fallback**: AVSpeechSynthesizer when the server has no TTS; voice picker, rate, per-message read-aloud queue | S |
| 119 | **Push-to-talk and VAD**: hold a key to talk, voice-activity detection ends the turn, wake word optional | M |
| 120 | **Sketch input**: a drawing canvas whose strokes go to a vision model as PNG (diagrams, maths, UI mock-ups) | S |

### VIII - macOS integration, round two (121-130)

| # | feature | size |
|---|---|---|
| 121 | ★ **True multi-window**: one NSWindow and ImGui context per chat, native tabs, drag a chat out into its own window, always-on-top mini window | L |
| 122 | **Menu-bar-only mode**: hide the Dock icon, live from the extra; background agent (launchd) so the hotkey and schedules work with no window | M |
| 123 | `[swift]` **Share extension and Finder Quick Action**: "Summarise with Open WebUI" from any app's share sheet and Finder's context menu | M |
| 124 | `[swift]` **Widgets**: Notification Center / desktop widgets for recent chats and a quick-ask field | M |
| 125 | **Focus-aware notifications**: respect Focus modes, batch while in a meeting, offer a digest afterwards | S |
| 126 | **Shortcut editor**: every binding customisable with conflict detection; a Vim-keys mode for the transcript | M |
| 127 | **Obsidian / vault sync**: mirror chats and notes to a Markdown folder with front matter, both directions | M |
| 128 | **Developer ID signing and notarisation**: stable identity so the keychain prompt stops returning; hardened runtime | S |
| 129 | **Auto-update**: Sparkle-style appcast with delta updates, release notes sheet, rollback | M |
| 130 | **Distribution**: DMG with background, Homebrew cask, crash reporter that captures a backtrace and offers to save or send it | M |

### IX - Reliability, privacy, observability (131-140)

| # | feature | size |
|---|---|---|
| 131 | ★ **Local-first store**: a SQLite mirror of every chat; full offline reading and search; conflict-safe sync when the server returns | L |
| 132 | **Private chats**: local-only conversations that talk straight to Ollama and never touch the server, encrypted at rest with a keychain key | M |
| 133 | **Redaction filter**: detect secrets and PII (API keys, tokens, emails, card and SSN patterns) in outgoing prompts and offer to mask | S |
| 134 | **Traffic inspector**: live HAR-style log of every request and socket.io event with latency waterfall and replay | M |
| 135 | **Usage dashboard**: tokens per day per model, tok/s trends, cost and energy estimates, CSV export | M |
| 136 | **Health dashboard**: GPU/VRAM/RAM from IOKit and Metal counters, thermal state, model-residency timeline | M |
| 137 | **Stream recovery**: partial streams persisted as they arrive; after a crash or relaunch, resume from the server's background task id | M |
| 138 | **Backup and import**: export everything (chats, settings, local index) to one archive; import ChatGPT and Claude exports as chats | M |
| 139 | **Audit log**: what left the machine, when, to which server - searchable, exportable, with per-tool approval history | S |
| 140 | **Low-power mode**: throttle frame rate when idle or on battery, pause watchers, defer indexing to AC power | S |

### X - Extensibility, quality, reach (141-150)

| # | feature | size |
|---|---|---|
| 141 | ★ **Plugin API**: JavaScriptCore plugins in a plugins folder - custom commands, panes, markdown renderers and tools; hot reload | L |
| 142 | **Accessibility**: NSAccessibility for the transcript and composer so VoiceOver reads the chat; full keyboard operability audit | L |
| 143 | **Accessibility options**: reduced motion, high contrast, dyslexia-friendly font, UI scale slider | S |
| 144 | **Localisation**: a strings table, RTL text, locale-aware dates and numbers | M |
| 145 | **Onboarding**: a first-run tour, interactive shortcut cheat sheet (⌘/), a "what's new" sheet per update | S |
| 146 | **Follow-up chips**: the small model proposes three next questions after each answer | S |
| 147 | **Smart folders and tags**: rules (model, tag, date, contains) that file chats automatically; auto-tagging by the small model | M |
| 148 | **Saved answers library**: cross-chat bookmarks with notes, searchable, exportable (pins are per chat; this is global) | S |
| 149 | **Model evaluation**: run a saved prompt set across models, grade with a judge model or thumbs, trend the scores (benchmark exists for the coding set; this is yours) | M |
| 150 | **CI and UI tests**: GitHub Actions macOS runner builds, runs `--selftest` against a scratch server, drives the GUI via the test-engine hooks, diffs screenshots; fuzz the markdown and diff parsers | M |

## Do these ten first

The flagship ★ items are what reviewers will notice. In the order that unlocks
the most:

1. **81 MCP client** + **82 tool-call cards** - every other agent feature stands on these.
2. **66 native composer** - the single biggest felt-quality jump for an ImGui app.
3. **61 artifact pane** - turns answers into working things.
4. **131 local-first store** - makes the app usable with the server down and search instant.
5. **91 terminal pane** - closes the loop the code tools opened.
6. **51 fan-out** - the local-model use case nobody else does well.
7. **71 sampling panel** - table stakes for a power user, cheap to build.
8. **101 PDF viewer with anchored citations** - makes RAG trustworthy.
9. **111 screen capture to chat** - the macOS-native move.
10. **121 true multi-window** - what people miss most coming from a browser.

## Suggested packages (for parallel agents)

| package | items | owner files |
|---|---|---|
| G agent core | 81, 82, 83, 84, 87, 88, 80 | `src/agents/*`, `src/mcp/*` (new) |
| H composer and rendering | 66, 67, 61, 62, 63, 64, 65, 52, 57 | `src/ui/*`, `src/chat_pane.cpp` |
| I models | 71, 72, 73, 74, 75, 76, 77, 78, 79 | `src/api/*`, `src/models_pane.cpp` (new) |
| J dev workflow | 91, 92, 93, 94, 95, 96, 97, 99, 100 | `src/code_tools.cpp`, `src/terminal_pane.cpp` (new), `src/git_pane.cpp` (new) |
| K knowledge | 101-110 | `src/knowledge_tools.cpp`, `src/pdf_view.mm` (new) |
| L multimodal | 111-120 | `src/capture.mm` (new), `src/images_pane.cpp` |
| M macOS 2 | 121, 122, 125, 126, 127, 128, 129, 130 + `[swift]` 90, 123, 124 | `src/native.mm`, `src/main.mm`, `extensions/` |
| N store and privacy | 131-140 | `src/core/store.*` (new), `src/inspector_pane.cpp` (new) |
| O extensibility and quality | 141-150, 53-56, 58-60, 68-70 | `src/plugins/*` (new), `src/features.cpp`, `.github/workflows/` |

Each package follows the first plan's rules: one owner per file, nothing in a
draw function blocks, build stays green, verify against the scratch server,
append a dated line to the status log below when it lands.

## Status log

(append dated lines here as packages land)

2026-10-24 G (81 MCP client + 82 tool-call cards): `src/mcp/mcp_client.*` (JSON-RPC
over spawned stdio with a PATH that includes Homebrew/npm/nvm, and Streamable HTTP
with JSON or SSE replies, session ids, request timeouts via a cancel timer; answers
`ping` and `roots/list`, declines sampling/elicitation; tools/resources/prompts,
list_changed, stderr log), `src/agents/tool_loop.*` (UI-agnostic loop over Ollama
native tool calling through `/ollama/api/chat`, direct-Ollama fallback, `<think>`
splitting, usage totals, approval + invoke hooks), `src/agents/mcp_integration.cpp`
(registry in `extra["mcpServers"]`, per-tool approvals, `startToolStream`, cards,
Tools ▸ MCP tab with add/edit/import/try, composer plug toggle), `tools/mcp_test_server.py`
and `--mcptest` (65 checks: both transports, importer, helpers, a live add turn and a
denied turn). `net::stream` gained a progress callback so cancel works on a silent
server. Verified in the GUI against a scratch 0.12.3 server with qwen3:8b: approval
card → Allow once → `add` ran, 5555; Always allow persisted (`calc__add: always`) and
the next add ran without asking; Deny → card "denied", model answered without the
tool; cards expand to arguments/result; the saved chat carries `mcpToolCalls`, done
and usage. Found and fixed on the way: Open WebUI 0.12 keeps its stored copy of any
assistant message with done:false, so the client's mid-stream placeholder made every
later update of that message vanish - `ChatDoc::toJson` now leaves streaming messages
out until they finish (this also affected non-MCP replies on the SSE fallback path).
Not done from package G: 83 agent loop, 84 recipes, 87 script tools, 88 browser tool,
80 grammar controls. Known limits: no legacy HTTP+SSE transport, no sampling/
elicitation, tool-enabled turns skip the server pipeline (RAG, web search, OWUI tools).

2026-10-24 H (66 native composer): `native::composer*` in `src/native.mm` - an
`OWUIComposerView` (NSTextView in a borderless NSScrollView) added to the window's
content view above the Metal view; the chat pane positions it each frame from its
ImGui box, grows the box with the laid-out text (46-240 pt) and reserves the room in
the transcript; `App::frame` hides it when the pane isn't drawn or a popup/overlay
is open. Three-way text sync (generation bump / app-side change push, typing pulls).
Enter sends, Shift+Enter inserts a newline, Cmd+Enter sends when "Enter sends" is
off, Esc hands the keyboard to ImGui's KeyEventResponder, Option+Up/Down walk the
transcript (only when there is one), Tab completes a slash prompt, paste: routes an
image on the pasteboard to pasteImageFromClipboard. Spellcheck on; smart quotes,
dashes and text replacement off (prompts carry code); file drags are declined so the
window's drop overlay keeps them. Caret goes to the end on programmatic focus.
Settings ▸ "Native text field" (extra["nativeComposer"], default on) falls back to
the ImGui field. Verified in the GUI on a scratch server: placeholder, typing with
spellcheck underlines, Shift+Enter, auto-grow to four lines with the toolbar moving
down, Enter sent and the reply came back, ⌥↑ highlighted the message and Esc+typing
returned to the composer, image paste attached with a thumbnail, the view hides on
other panes and under the command palette. Not exercised: IME composition,
dictation, the emoji picker (all stock NSTextView behaviour).

2026-10-24 H (61 artifact pane): `src/artifacts.cpp` + `native::artifact*` in
`src/native.mm` - a WKWebView floated over a 540 pt column beside the transcript
(takes precedence over the reference column and pins). `scanArtifacts` walks the
open branch's fenced blocks (html/htm, svg, mermaid, xml containing <svg>), keyed on
a branch fingerprint so it is cheap per frame; `openArtifact` from the "Preview"
code-block action or automatically after a completion with an HTML/SVG block
(extra["artifactAutoOpen"], default on; Mermaid only on request since its renderer
comes from a CDN). `artifactDocument` wraps SVG and fragments in a themed page and
builds the Mermaid page. An injected user script forwards console.log/warn/error,
window errors and unhandled rejections to the app through a WKScriptMessageHandler;
the column shows the last line (all on hover). Header pages through the chat's
artifacts; actions: Edit (ImGui editor with Apply/Revert), Reload, Copy, Save...
(.html/.svg/.mmd), Open in browser (temp file in the cache dir). Verified in the
GUI against a scratch server with qwen3:8b: an HTML reply auto-opened and rendered
(title from <title>), clicking its button produced "console (1): log: clicked", the
editor showed the source, an SVG reply tagged ```xml was recognised and paged to.
Not exercised: Mermaid (network), Save dialog, Open in browser.

2026-10-24 N (131 local-first store): `src/core/store.*` (SQLite per server,
`chats` + FTS5 `chats_fts` + `meta`, WAL, one mutex; putChat/putListItem/putDirty/
dirtyChats/getChat/listChats/search/removeChat/stats/clear) and `src/mirror.cpp`
(`syncMirror`: pushes dirty rows first - fetching the server copy and keeping its
title when it changed after the edit's base updated_at, the server merges histories
itself - then pulls `/api/v1/chats/all`, which 0.12 streams as NDJSON, with a paged
fallback, and drops rows the server no longer has; throttled to 10 min, forced on
"Server is back" and from Settings). Hooks: openChat mirrors on success and opens
the local copy on failure; refreshChats mirrors list rows and falls back to the
mirror with an "Offline - showing the local copy" pill; saveActiveChat mirrors on
success and keeps the body as dirty when the transport fails; sidebar search answers
from the mirror instantly and keeps that when the server search fails; archived list
falls back too; createChat/deleteChat keep the mirror in step; profiles switch the
database. Settings ▸ Local copy: chats cached, unsent edits, size, last sync, Sync
now, Rebuild. `--selftest` gained 11 mirror checks (63 total). Verified in the GUI:
mirror filled at launch (3 chats), server stopped → chat opened from the local copy
with a toast, "empire" found Roman history locally, archived toggle showed the pill
and the cached list; a thumbs-up offline produced a dirty row; after `docker start`
the row was pushed and the server's message carried `annotation.rating = 1`.
Gotcha found: ⌘R (refresh) is not a menu item, so it is swallowed by the native
composer when that has focus; use the sidebar toggles or wait for the reach check.

2026-10-24 J (91 terminal pane): `src/core/pty.*` (forkpty + reader thread; login
shell `$SHELL -l -i` with TERM=xterm-256color in the project folder or home),
`src/ui/terminal.*` (VT/xterm emulator: UTF-8, C0, CSI cursor/erase/insert/delete/
scroll-region, SGR 16/256/truecolor + bold/dim/underline/inverse, DECSC/DECRC,
DECCKM, DECAWM with pending wrap, alt screen 47/1047/1049, OSC title, 5000-line
scrollback) and `src/terminal_pane.cpp` (Tools ▸ Terminal: grid drawn with ImDrawList
runs, cursor block, wheel scrollback with a "N lines back" badge, drag selection,
keys via InputQueueCharacters plus escape sequences for arrows/home/end/delete/page/
enter/backspace/tab/esc and Ctrl+letter, Cmd+C copy / Cmd+V paste, ImGui nav keys
disabled while the grid has focus; Restart, Clear, Copy, "Ask about selection"/"last
40 lines" → composer, "Explain as build error" → explainBuildError). Verified in the
GUI: zsh prompt, `ls --color` directory colours, printf SGR sample (red bold, green,
underlined, inverse), `seq 1 60` scrollback with the badge, drag selection of five
lines. Fixed on the way: the wheel scrolled the Tools pane as well (SetItemKeyOwner on
the grid). Not exercised: full-screen TUIs (vim/htop use the alt screen, which the
emulator implements but was not driven), mouse reporting (unsupported by design).

2026-10-24 (51 fan-out): `src/fanout.cpp` - a composer button (branch icon) opens a
picker of models (persisted as extra["fanoutModels"]); "Send to N models" swaps the
selection in for one sendMessage, which already creates one sibling answer per model.
Sibling answers under a user turn render as columns (`drawFanoutColumns`, min 300 pt,
horizontal scroll beyond that) with model, first-token/total time, tok/s, reasoning,
tool cards and the markdown body; "Keep" sets the branch, "Keep, drop others" also
removes the other answers (disabled while any streams). Settled turns are recorded in
chat.raw["fanout_kept"] so the columns collapse to the normal view; ⌘D compare mode
still word-diffs two answers. Verified in the GUI against a scratch server: the picker
ticked qwen3-coder:30b and qwen3:8b (persisted), "Send to 2 models" produced two columns
("Red Green Blue" in 0.7 s at 36 tok/s, "red blue green" in 6.3 s at 50 tok/s with its
reasoning), and "Keep, drop others" collapsed to the kept answer - which exposed that the
server keeps messages that merely vanish from a chat update (its merge is a union), so
dropping now also calls `DELETE /api/v1/chats/{id}/messages/{mid}`; `--selftest` covers
that route (65 checks). Found and fixed on the way, a bug that had been biting every ImGui click made right after typing in the native
composer: NSTextView handles a mouse-down with its own tracking loop, which pulls the
mouse-up straight from the event queue, so the ImGui backend's event monitor saw the
down but never the up and treated the button as still held - the next click anywhere
was lost (the picker opened only on a second click; Send needed two). `OWUIComposerView
mouseDown:` now feeds ImGui a release after the loop returns. Also: mouse-moved events
are kept on while a native view is first responder, ImGui keyboard navigation is off
while the composer has focus (the monitor feeds it the typed keys too), and
`drawInMTKView` refreshes the pointer position from the window each frame.

2026-10-24 I (71 sampling panel): `src/sampling.cpp` replaces the small chat-settings
popup. Per-chat `chat.params` under Open WebUI's key names (temperature, top_p, top_k,
min_p, repeat_penalty, max_tokens, num_ctx, seed, stop, system); each knob has a switch
so untouched keys stay absent and the server keeps its defaults; context size hints the
model's reported window; the seed switch is the lock (Reroll draws a new one); stop
sequences one per line; quick presets Precise / Balanced / Creative / Deterministic
(temperature 0 + seed 42); "Save as preset" feeds the existing presets; changes save the
chat as they are made. The gear in the header is tinted when anything besides the system
prompt is set. The tool loop's `ollamaOptionsFromParams` already maps these keys for
MCP turns. Verified in the GUI: the panel opened and rendered every control. Not
driven: a send with the knobs set (the params ride in the completion payload exactly
as before this change).

2026-10-24 K (101 PDF viewer with anchored citations): `native::pdf*` in `src/native.mm`
(a PDFView floated over the column like the composer and artifact views; load from
bytes, page count/current/goto, `pdfHighlight` finds the cited passage with
`PDFDocument findString:` trying the leading 60/40/28/20/14/10/7/5/4 words of the
whitespace-collapsed chunk, preferring a hit on the cited page, adds a highlight
annotation and scrolls to it) and `src/document.cpp` (560 pt column: file name, page
pager, "Find passage again", "Excerpt" (the old popup), "Open in Preview"; downloads
`/api/v1/files/{id}/content` once into supportDir()/cache/doc-<id>.pdf). Hook:
`openCitation` routes sources whose name or content_type says PDF to the column
(server page numbers are 0-based, +1), everything else keeps the popup; the column
takes precedence after artifacts. Verified against a scratch server: a generated
three-page PDF uploaded through the API, a RAG completion run with it (the answer
cited "[1]" with `file_id`, `content_type: application/pdf`), clicking "1.
poodle-handbook.pdf" under Sources opened the column at page 1/3 with the cited
passage highlighted; a second, precomposed page-2 citation opened at page 2/3 with the
whole three-line passage highlighted. Fixed on the way: `scrollSelectionToVisible:`
right after assigning a document landed on the last page (layout not done) -
`layoutDocumentView` + `goToRect:onPage:` instead; and `findString:` never matches
across a line break, so after the first hit the following words are searched from
that selection and unioned while they stay nearby on the page. Not exercised: a PDF
whose extraction disagrees with PDFKit's beyond the first words (falls back to the
cited page), "Open in Preview".

2026-10-24 L (111 screen capture to chat): `src/capture.cpp` + `native::captureScreen`
(runs `/usr/sbin/screencapture -x -t png [-i]` from a worker and reads the PNG back;
Escape = cancelled) and a second Carbon hotkey (`registerCaptureHotkey`, id 2,
Control-Option-S, `Callbacks::captureScreen`; the handler now reads the hot-key id).
Entry points: camera button in the composer, File ▸ Capture Screen to Chat (Shift-Cmd-2),
palette "Capture screen region / whole screen to chat", the hotkey. The shot is attached
through `attachBytes` and Live Text (`recognizeText`) output goes into the composer as
a fenced block (extra["captureOcr"], default on; extra["captureHotkey"], default on;
both in Settings). Verified in the GUI via the palette's whole-screen command (status
below). Not driven: the interactive region picker and the global hotkey (both are the
OS's own UI; the hotkey registration returns success).

2026-10-24 M (121 multi-window), scoped: a window per chat is a second *instance* of
the app (`src/windows.cpp`, `native::openNewInstance` via NSWorkspace with
`createsNewApplicationInstance`, inheriting the environment, `--chat <id>` and
`--mini`). The App object is one ImGui context with per-view state spread across its
members, so one context per NSWindow would be a refactor of most of the UI code; a
second process gives every window the whole client today. Secondary instances skip
the global hotkeys and the menu bar extra, never write settings, cascade their window,
and open the chat once signed in; `--mini` is 560x760, sidebar hidden, floating
(NSFloatingWindowLevel, joins all Spaces). Entry points: sidebar row menu, chat "More"
menu, File ▸ Open Chat in New Window (Shift-Cmd-N) / as Floating Mini Window, View ▸
Float on Top (Shift-Cmd-T), palette. Not delivered from the item's wording: native
window tabs and dragging a chat out of a window (both need one process); a
single-process, context-per-window version stays on the list. Verified against a
scratch server: "Open this chat in a new window" from the palette started a second
instance (`--chat <id>`) that signed in through the inherited environment and opened
the same chat in a cascaded full window; "floating mini window" started a third at
560x760 without the sidebar. Known cosmetic: at 560 pt the chat header's title and
action icons overlap. A launch from a bundle under ~/Documents makes macOS ask for
Documents access (LaunchServices-launched process); the /Applications copy does not.

2026-10-24 composer conveniences (54 drafts, 56 history ring, 57 smart paste, 52
quote-reply, 60 steering chips): `src/compose_tools.cpp`. Drafts live in
extra["drafts"] keyed by chat id ("new" for a fresh chat), written 1.2 s after typing
pauses (`trackDraft` from the composer every frame) and on switch/quit, restored in
`newChat`/`openChat`, cleared on send. History is extra["promptHistory"] (50 entries,
deduplicated, newest last); the native composer hands plain Up/Down at the first/last
line to `recallHistory`, which only starts from an empty composer and stashes it for
Down past the newest. Smart paste: the composer's paste hook offers the text to
`smartPaste` first - 40+ lines or 4000+ chars become `attachBytes("pasted.txt|json|
html|cpp|py")` with a toast, shorter pastes go into the text view as before.
Quote-reply is a button on every message (`quoteReply`: up to 600 chars as `> `
lines appended to the composer). Steering chips draw under the latest answer while
nothing streams (`steer` = `sendMessage(instruction)`). Verified in the GUI against a
scratch server: a draft typed in a new chat survived opening "Greetings" and coming
back; two ↑ presses recalled the seeded history newest-first; the chips row drew under
the answer; "Quote in a reply" put `> Hello, Bonjour, Hola.` in the composer; a
60-line ⌘V produced a `pasted.txt` chip and the "61 lines" toast with the composer
untouched. Also in this change: secondary instances no longer set the window frame
autosave name - the mini window had been writing the shared frame, so every launch
opened at 560x760. Not delivered from the items' wording: a sidebar pencil on chats
with a draft, ⌃R fuzzy history search, a rendered callout for the quote (it is a
plain `>` block), URL paste offers (fetch/summarise), the Translate/ELI5 chips
(Simpler covers ELI5).

2026-10-24 I (58 structured output mode): `src/core/jsonschema.{h,cpp}` (a draft
2020-12 subset validator with path-qualified messages, `check` for the schema itself,
`infer` from an example, `extractJson` for bare / fenced / embedded JSON) and
`src/structured.cpp` (panel, verdict, re-ask, table/tree/JSON card). The schema lives
in the chat document (`chat.raw["structuredOutput"]`: enabled, name, schema text,
maxRetries) so it rides along with the chat on the server. The request carries
`response_format: {type: json_schema, json_schema: {name, schema}}` - Open WebUI
passes it to OpenAI-style backends and converts it to Ollama's `format` - plus a
system message spelling the schema out; after a violation the errors are appended
to that note and the reply regenerated as a sibling (the retry becomes the branch via
`fanout_kept`, the rejected one stays reachable with the sibling arrows). The card
replaces the markdown body: records -> table (union of keys, 200 rows / 12 columns
cap), flat object -> field/value table, scalars -> numbered list, nested -> tree;
JSON view and "Copy JSON" always. `--schematest` runs 50 offline checks (all pass).
Verified against a scratch server with qwen3:8b: the Classification starter produced
pure JSON rendered as a field/value table with "Matches schema"; List of records gave
a four-row table, Tree and JSON views switched; a uniqueItems schema with the prompt
"Set nums to [7, 7, 7] exactly" was rejected ("$.nums[1]: duplicate item"), re-asked
once and came back [7, 8, 9] as 2/2; the configuration round-tripped through
GET /chats/{id}. Fixed on the way: a regenerated reply (any regenerate, not only the
re-ask) lost its parentId on reload - the server creates the reply's record itself
when the task starts, unlinked, and keeps its own done:false copy over the client's
later saves - so `regenerate` and `sendMessage` now save the placeholder with its
parentId first (`Message::serverStream`, `saveActiveChat(then)`) and start the
stream from the save's continuation; the header's right-hand group was 37 pt too
narrow for the extra icon. Not exercised in the GUI: the violation card on the
rejected sibling (the user had the machine, so the last screenshots were skipped) -
its renderer is the same `drawStructuredValue` path. Not delivered: a schema library
across chats, `pattern`-constrained generation (Ollama's grammar ignores some
keywords - the validator catches them and re-asks), the MCP tool-loop path (direct
Ollama, no `format` yet).

2026-10-24 I (53 branch tree view): `src/tree_view.cpp`, a side column (380 pt, header
tree icon / More menu / palette "Toggle conversation tree") drawing the whole message
tree on an ImDrawList canvas: tidy top-down layout (subtree widths summed, siblings in
creation order, roots by timestamp), bezier edges, two-line nodes (who + sibling
position, opening words with ellipsis), the current branch and leaf highlighted,
tooltips with 400 chars, zoom -/+ / Fit / Current, drag-to-pan, and a stats line
(messages, leaves, dead ends). Click = `jumpToNode` (on the current branch it scrolls
the transcript; off it the branch through that node becomes current down to its newest
leaf). Right-click = jump / copy / delete this message or prune this branch;
"Prune N dead branches" (confirm popup) removes every subtree off the current path.
Pruning deletes each message on the server (`DELETE /chats/{id}/messages/{mid}`, since
the server's body merge keeps vanished messages) and saves the chat only after those
deletes, because a server-side delete moves currentId to the deleted message's parent.
The zoom fits the column whenever the node count changes (0.5-1.0), nodes keep two
text lines at any zoom (ImGui 1.92 sized PushFont). Verified against a scratch server
with a seeded ten-message, four-leaf Lisbon chat: the tree drew fitted with the stats
"10 messages - 4 leaves - 3 dead ends"; clicking the dead "A slower version" leaf
switched the transcript to that branch and re-highlighted the path; the context menu
showed Jump / Copy / Delete; "Delete this message" dropped the count to 9 with a toast
and the server lost exactly that message. "Prune N dead branches" first crashed
(`std::out_of_range`: it deleted messages inside the tree column's own frame, after the
layout had been built, so the node loop looked them up again) - the prune now ends the
frame before anything else is drawn; rerun on a fresh seed it left the four messages of
the current branch locally and on the server, with the server's currentId still on the
leaf (the delete-before-save ordering), and one summary toast.
Observation, not changed: any prompt with two or more answers renders as fan-out
columns in the transcript until one is kept (the fan-out item's design), so a seeded
or regenerated branch shows "2 answers - keep one" even when it was never a fan-out.

2026-10-24 I (59 stop, edit, continue): `continueReply` is now a real continuation
instead of a "continue where you left off" prompt. `buildCompletionPayload` appends the
reply so far as the final assistant message (prefill) when `continueFromId` names it,
`startStream` seeds the stream with the existing text so every delta extends it and
forces the HTTP path (as a server task the message would be restarted from the new
tokens) and skips the tool loop; `afterCompletion` saves as usual. The reply's action
row gained a pencil: the reply opens in an editor sized to its text with Save (in
place, `saveEditedReply`) and Save & continue (save, then continue from the edited
tail); stopping a stream was already there, so stop -> edit -> continue is the whole
loop. Found on the way: a thinking model given a bare assistant prefix takes it for its
own thoughts - qwen3:8b continued correctly, then wrote `</think>` and answered again
from scratch, with or without Ollama's `think:false`. Sending the prefill with a
`thinking` field (the reply's reasoning, or a single space) makes Ollama's template
render a closed think block ahead of the prefix and the model continues cleanly
(checked directly against Ollama with think on and off); Open WebUI passes `thinking` /
`reasoning_content` through to Ollama. Verified against a scratch server with qwen3:8b:
a seeded reply cut at "3. Earth - the only" was continued to the full eight planets by
the play button (first with the `</think>` artefact, which drove the fix); the pencil
opened the editor, the text was cut back to the three-line tail, and Save & continue
produced the clean eight-line list, saved on the server without any restart or think
tag. Not exercised: stopping a live stream first (Stop is the existing button; a
stopped reply is just a non-streaming message, the same state the seed put it in), and
OpenAI-style backends (they get the same prefill message; many ignore `reasoning_content`
and some reject an assistant-last conversation).
