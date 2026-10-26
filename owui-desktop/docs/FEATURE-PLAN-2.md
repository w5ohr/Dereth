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

2026-10-24 I (55 prompt variables): `src/core/promptvars.{h,cpp}` (parse / inputs /
expand with a Context of callbacks, app-free) and `src/prompt_vars.cpp` (the app's
context, the send-time intercept, the fill-in form). `sendMessage` hands any prompt
with a known `{{...}}` to `interceptPromptVariables`: with no inputs it expands and
sends at once (a toast lists what went in: "clipboard: 9 B - file x: 3.1 KB"); with
`{{input:Label|default}}` fields it opens a modal form - one field per distinct label,
defaults prefilled and selected, a live "Will send" preview, Enter sends, Esc cancels
(the template stays in the composer). The history ring records the template, not the
expansion (`historyOverride_`). Names: clipboard, selection (terminal selection, else
the last Services / Quick Ask text), date, time, datetime, weekday, timezone, user,
model, title, file:path (~, project-relative, 200 KB cap, binaries replaced by a
note), input; Open WebUI's upper-case CURRENT_DATE / USER_NAME / CLIPBOARD aliases map
onto them; unknown names stay as written; text that came out of the clipboard or a
file is never re-scanned. `--varstest` runs 22 offline checks (all pass). Verified
against a scratch server with qwen3:8b: a prompt with {{clipboard}}, {{input:Mood|
cheerful}}, {{date}} and {{weekday}} opened the form with "cheerful" selected and the
preview reading "...says PINEAPPLE, my mood is cheerful and today is 2026-10-24
(Saturday)"; typing "grumpy" and Enter sent the expanded text (saved on the server as
such), the model answered, and Up in the empty composer recalled the template with its
braces intact. Not delivered from the item's wording: `{{selection}}` of another app's
text (macOS gives no public way to read it without Accessibility; Services covers the
"select text, ask" flow), and a picker for inserting variables (they are typed).

2026-10-24 IV (83 agent loop): `src/agents/agent_loop.{h,cpp}` (UI-agnostic, built on
the tool loop: plan -> act -> verify -> retry -> answer, with onSteps / onStatus /
waitWhilePaused hooks and the tool hooks forwarded) and `src/agents/agent_integration.cpp`
(composer brain toggle, extra["agentMode"]; `startAgentStream` takes the last user turn
as the goal and the rest as history; the run is a `Stream::agentRun` snapshot copied into
`Message::agentRun` and saved as `agentRun`; `drawAgentCard` shows the steps with
running ring / tick / red ring, attempts and tool-call counts, click-to-expand outcomes
and the checker's objection, Pause / Resume / Stop while live; right-click popover for
agentMaxSteps 1-12, agentMaxAttempts 1-4, agentVerify). The planner and verifier ask
Ollama for JSON through a new `LoopConfig::format`; `parsePlan` also accepts arrays,
object items and numbered prose, `parseVerdict` accepts yes/no prose. `baseToolConfig` and
`makeToolHooks` were extracted from `startToolStream` so both loops share model
unwrapping, server instructions, approvals and tool invocation; palette "Toggle agent
mode". `--agenttest` runs 14 offline checks. Verified against a scratch server with
qwen3:8b and the calc test MCP server: a multiply/divide goal with only an `add` tool was
planned into 3 steps, steps 1-2 were judged incomplete, retried once and marked not
completed, step 3 done, and the answer said how the figures were reached ("1/3 steps
done, 2 not completed"); an add-only goal was planned into 2 steps, paused on the
existing approval card, ran 3 `add` calls after Always allow, both steps done, answer
7911, all persisted. Fixed on the way: each tool loop numbers calls from `call_1`, so the
cards of step 2 overwrote step 1's (2 cards, wrong results, for 3 calls) - the agent
prefixes ids per call (`s2_call_1`). Not exercised: Pause (the button is wired to the
flag the loop polls between steps) and a run without any MCP tools (text-only prefix).
Known: the "Step n/m" status shows both in the card header and the message's status
row while running.

2026-10-24 X (141 plugin API): `src/plugins/plugin_host.{h,mm}` - one JSContext per
`*.js` file in the plugins folder, all on one serial dispatch queue; the `owui` object
offers plugin({name, version, description}), registerCommand, registerTool ({name,
description, parameters}), onBeforeSend, registerRenderer, toast / notify / insert /
send / openArtifact, clipboard.read/write, fetch (synchronous, via net::perform),
readFile / writeFile (~ expands), storage.get/set (plugins/.data/<id>.json), log and
console.*. Exceptions go to the plugin's error list and log (and back to the model as
the tool's error); promises that settle without timers are awaited; hooks (3 s), tools
(60 s) and renderers (5 s) run through a semaphore with a timeout so a stuck plugin
cannot hang the app; files starting with `_` or `.` are skipped. `src/plugins/
plugin_integration.cpp` wires it in: callbacks posted to the UI thread, FSEvents hot
reload keyed off the names/mtimes of the .js files (the first version reloaded on the
plugins' own storage writes under .data/ and looped: six "1 plugin loaded" toasts),
palette entries ("plugin: <name>"), plugin tools merged into `mcpToolSpecs()` with
serverId "plugin:<id>" and routed in `makeToolHooks`, the before-send hook in
`sendMessage` after prompt variables, renderer buttons in `drawCodeBlockActions`, and a
Tools > Plugins tab (enable, network, file access, reload, open folder, write example,
per-plugin card with counts, errors, log). `docs/PLUGINS.md` is the reference;
`--plugintest` loads plugins from a scratch folder and checks 24 things (metadata,
counts, errors captured with line numbers, console log, fetch refused when off,
commands with ctx, object / string / async / throwing tools, unknown tool, before-send
rewrite, renderer, reload, storage survival, unload). Fixed on the way: runCommand's
dispatch block captured the caller's string references and ran after they were gone
(commands silently did nothing) - it copies them now; JS errors are reported through
`toString` (an Error's own properties are not enumerable, so JSON gave "{}"). Verified
against a scratch server with qwen3:8b: Tools > Plugins wrote the example plugin and
listed "2 commands - 1 tool - 1 before-send hook - 1 renderer"; the palette showed
"Word count of the clipboard - plugin: Example" and running it toasted "4 words on the
clipboard"; "!shout hello there..." went out as "HELLO THERE, ..."; a seeded ```csv
block got a "Table" button that opened the plugin's HTML table in the artifact pane;
appending a comment to example.js produced exactly one "1 plugin loaded" toast; with the
MCP toggle on, "use the word_count tool" produced an approval card "Example wants to run
word_count", Allow once ran it ({"words": 9}) and the answer said 9 words, all saved.
Not delivered from the item's wording: plugin-defined panes (an ImGui pane from JS has
no good shape yet; openArtifact gives plugins an HTML surface instead) and markdown
renderers beyond code blocks.

2026-10-24 IX (133 redaction filter): `src/core/redact.{h,cpp}` (app-free scanner:
vendor key shapes for AWS / OpenAI / Anthropic / Google / Stripe / GitHub / Slack /
Hugging Face / JWT / bearer, generic `api_key=` / `token:` / `password=` assignments,
PEM private-key blocks, Luhn-checked card numbers, SSNs, IBANs, emails, phones, IPs;
overlaps resolved longest / most specific first; `apply` gives stable placeholders per
value and continues numbering across a chat; `restore` for display) and
`src/redaction.cpp` (the intercept in `sendMessage` after prompt variables and plugin
hooks, the "Before this leaves your Mac" modal with mask checkboxes, kind labels
coloured secret / personal, previews or full values, per-kind next-time policy ask /
always / never, Send masked / Send as is / Cancel; the placeholder map in
support/redactions.json keyed by chat id, carried from "new" to the real id once the
chat exists; `unredact` applied to the markdown body, raw text and Copy). All-"always"
findings mask without a sheet (toast "Masked N items before sending - shown unmasked
here only"); "never" kinds are not findings. Settings toggle extra["redactScan"].
`--redacttest` runs 19 offline checks. Fixed on the way: std::regex has no lookbehind
(the first rules aborted the whole scan with regex_error - the crash report the user
pasted); the card rule swallowed a trailing space; the Enter that sent the prompt was
still down when the sheet opened and confirmed it at once (Enter now counts only after
it was seen released). Verified against a scratch server with qwen3:8b: a prompt with
a fake OpenAI key and an email was sent (both kinds on "ask" the first run went through
because of the Enter bug - masked anyway), the server stored "My key is [API_KEY_1] ...
[EMAIL_1]" and redactions.json held the values under the chat id; the reply "Your email
[EMAIL_1] has been received" rendered as "Your email ann.lee@corp.io ..." both live and
after a relaunch; a second prompt with a phone, a card and an IP opened the sheet with
three rows, and Send masked produced placeholders on the server. Not covered:
attachments and pasted files are not scanned (only the prompt text), and a secret the
model needs verbatim (an API key for a tool) has to be sent "as is".

2026-10-24 IX (137 stream recovery): `src/recovery.cpp`. `pollStreams` writes every
live stream (chat, message, model, task ids, content and reasoning so far, whether it is
a server task) to support/inflight.json at most once a second and removes the file when
nothing is streaming (`stopAllStreams` too). On launch, once signed in, `recoverInflight`
reads the records (younger than a day), fetches each chat and looks at the message: done
on the server -> toast "A reply in '...' finished while the app was closed" (and the chat
opens when none is); not done and it was a server task -> the chat opens and
`tickRecovery` attaches a resumed Stream that polls the message every 1.5 s until the
server marks it done (or the task list is empty and the text stopped growing, or 15
minutes), so the usual pollStreams / afterCompletion path finishes it (save, title,
chips); not done and client-driven -> the saved partial is kept in the message with a
note ("the text so far was kept - use Continue to let the model finish it") and saved.
New route GET /api/tasks/chat/{id}. Verified against a scratch server with qwen3:8b: a
1200-word story was killed with `kill -9` 11 s in (inflight.json: socket task, 3975
chars of content, 1452 of reasoning); on relaunch the chat opened on its own, the
resumed stream polled (snapshots logged), the full 972-word reply showed and the title
("Desert Caravan") was generated after it finished; a first attempt with a 400-word
story finished in 15 s, before the kill, and correctly left no record. Not exercised:
the client-driven branch (a Continue run finished within the 6 s before the kill) and
the "finished while closed" toast on its own (the resumed poll covered that case
because the server finished during the relaunch gap).

2026-10-24 IX (132 private chats): `src/core/privatestore.{h,mm}` (seal / open with
AES-256-CBC + HMAC-SHA256 over derived keys, "OWP1" + IV + ciphertext + tag, constant-time
tag check; save writes a 0600 temp file and renames; load / list / remove; `local-` ids)
and `src/private_chats.cpp` (the 32-byte key from the keychain, made on first use and
never touched at launch; `OWUI_NO_KEYCHAIN=1` or a missing keychain falls back to a 0600
`.key` file with a toast; new / save / open / delete; `startPrivateStream` runs the tool
loop straight at `settings().ollamaUrl` - MCP tools only when the composer toggle is on -
and refuses with a note when no URL is set; `maybeGeneratePrivateTitle` asks the local
model for 3-6 words; a PRIVATE sidebar section with lock rows and a Delete menu; a lock
badge in the chat header; palette "New private chat"). Hooks: `saveActiveChat`,
`startStream`, `afterCompletion` (no chatCompleted / server title / follow-ups),
`openChat`, `deleteChat`, `serverDrivenStream`, `updateHandoffAndSpotlight` and the
in-flight snapshot (private chats are never written there in the clear) all branch on
`ChatDoc::isPrivate` / the id prefix. `--privatetest` runs 19 offline checks. Verified
against a scratch server with qwen3:8b: the lock button made "Private chat" under a
PRIVATE header with the badge in the header; a prompt was answered by Ollama directly
(23 in / 122 out), the title became "Capital of France Answered", support/private held
one `.enc` file and `.key` (both 0600), the server's chat list stayed at zero, the chat
reopened from the file after a relaunch and a second reply left no in-flight snapshot.
Found on the way: the first run called the keychain at launch under the scratch HOME and
macOS put up "Keychain Not Found" - a system dialog this harness must not touch - so the
key is now lazy with the file fallback. Not covered: moving a private chat between Macs
(the key is per keychain; an export is the way), and agent mode inside a private chat
(it runs as a plain tool-loop reply).

2026-10-24 II (65 smooth streaming), partial: follow-tail scrolling and per-chat scroll
memory in `chat_pane.cpp` / `app.cpp`. `followTail` is cleared by a wheel-up over the
transcript or a scrollbar drag away from the bottom, and set again whenever the view is
within 8 px of the bottom; a streamed delta only requests the tail while following
(otherwise it sets `newTextPending`), explicit jumps (send, regenerate, edit-and-resend,
open, new) resume following, and a same-frame wheel-up beats a pending delta. The
"New text ↓" pill is its own borderless ImGui window anchored to the bottom centre of
the transcript (drawing it with SetCursorScreenPos in the parent put it under the child's
text and tripped ImGui's bounds assert), and clicking it scrolls to the end.
`rememberScroll` / `restoreScrollFor` keep a scroll offset per chat id and apply it two
frames after a chat opens (when the content has a size); a chat without a memory opens
at its end as before. Verified against a scratch server with qwen3:8b: a wheel-up over
a 1500-word reply mid-stream parked the view - the text kept arriving below the fold
without moving it, the pill appeared above the text, and clicking it jumped to the
live end; scrolling a finished chat to the top, opening another chat and coming back
restored the top instead of the end. Not done from the item's wording: the per-token
fade-in (the markdown renderer has no per-glyph alpha yet) - the item stays open for it.

2026-10-24 III (76 context strategy picker): `src/core/contextfit.{h,cpp}` (an app-free
planner: drop-oldest, sliding window of N user turns, or a rolling summary standing in for
a covered prefix; system messages and the newest user turn are never cut; a summary
larger than what it replaces is ignored) and `src/context_strategy.cpp` (per-chat
strategy in chat.raw["desktop_context"] with a settings default; the budget is the model
window minus max_tokens (or 1024) minus the system prompt; `applyContextPlan` runs inside
`buildCompletionPayload` on the message ids, so every path - server task, HTTP,
continue, private - gets the trimmed list and a toast; `maybeSummariseContext` runs after
a turn that cut six or more messages under the summary strategy, asks the chat's model
(non-streaming, /api/chat/completions) for a 300-word digest of the old summary plus the
newly cut turns, strips a <think> block, and stores {upto, text, tokens} in
chat.raw["desktop_context_summary"]; the ctx label in the header is now a button that
opens a popover: three radios, the window's turn slider, the summary on file, "use as
default", and a preview table of the branch with the messages the next send would cut
greyed and prefixed "-"). `--contexttest` runs 13 offline checks. Verified against a
scratch server with qwen3:8b: a seeded 16-message chat at ~56k tokens against a 40k
window showed "6 of 16 messages would be left out" in the preview; choosing the summary
and asking about turn 1 was answered wrongly from the remaining turns (expected - no
summary yet) while the first overflow produced a 301-token summary that named every
folded topic and year; the next question went out with "6 of 19 messages folded into the
summary" - but the reply still said "granite quarries, 1591" (the earliest turn left in
the window) rather than the summary's "lighthouses, 1507": the summary carried the fact,
the 8B model preferred the explicit turn text. The summary's framing was reworded to say
it is part of the history and to answer from it (not re-verified live); the trim toast
appeared on each send. Fixed on the way: the sliding window counted the newest user message as a
turn boundary one step early (kept one turn too few).

2026-10-24 V (94 multi-file apply): `src/core/fileblocks.{h,cpp}` finds the file blocks
in a reply - the path from the fence info string (`path=` / `title=` / `file=` or a
bare path token), from a first-line comment (`// file: x`, `# path: x`, `<!-- x -->`,
stripped from the body), or from the nearest non-empty line above the fence when it is a
heading / bold / inline-code path or an "Update `x`:" label; version numbers, URLs and
prose are not paths, Makefile-style bare names are, diff blocks are left to the diff
path, a later block for the same path wins. In `code_tools.cpp`: `applyFileBlocks`
resolves each block against the project folder (asks for one when none is pinned),
refuses paths that escape it, skips files that already match, generates a diff per file
(new files as a pure add) and hands the set to the existing apply dialog; `confirmApply`
now stops at the first failed write and rolls the earlier files back from their `.bak`,
journals every apply to support/apply-history.json and keeps the last one for
`undoLastApply` (palette "Undo the last apply to files"). A folder icon appears in the
action row of any reply with named blocks; the dialog says "N files, applied together".
`--applytest` runs 12 offline checks. Verified against a scratch server and a scratch
project: a seeded reply with `### src/app.py` + a block, a `path=tests/test_app.py`
block and an unchanged `**README.md**` block showed the icon; clicking it toasted "1
file(s) already match and were skipped" and opened the dialog with both diffs ("@@ -1,2
+1,6 @@" and the new file's "+1,9"); Apply wrote app.py (with app.py.bak), created
tests/test_app.py, and journalled both; the palette's Undo restored app.py's original
text, removed the .bak and deleted the created test file. Not exercised: a write failure
mid-transaction (the rollback path is the same `rollback` the Undo used).

2026-10-24 III (72 model cards): `src/model_cards.cpp`. A card per model is seeded from
the model list's `ollama` block (size, modified, details) and completed once through the
server's /ollama/api/show passthrough (a workspace model unwraps to its base): family,
parameter size and count, quantisation, architecture, trained context length, embedding
width, layer count, capabilities, Modelfile PARAMETER defaults (stop lines skipped),
system prompt, licence (general.license or the first line of the text). Shown as a
tooltip on the picker rows (short delay) and on the header's model chip when one model
is selected (the old "Choose one or more models" tip stays for multi-select); "Compare
models..." in the picker and the palette opens a sortable table - model, family, params,
quant, size, bits per weight (file size x 8 / parameter count), context, layers,
capabilities - of every Ollama model, click a row to select it. Verified against a
scratch server: the chip tooltip read "qwen3 - 8.2B - Q4_K_M - 4.9 GB / Context 40k
tokens / 36 layers, 4096-wide embeddings, 5.1 bits/weight / Capabilities: completion,
tools, thinking / Defaults: repeat_penalty=1 temperature=0.6 top_k=20 top_p=0.95 /
Licence: apache-2.0"; the picker tooltip showed llama3.3:70b at 39.6 GB, 131k context,
80 layers, 4.8 bits/weight; the table listed all eight models with the two qwen3-coder
quants side by side (Q4_K_M 17.3 GB at 4.9 bits vs Q8_0 30.3 GB at 8.5). Fixed on the
way: opening the table from inside the picker modal closed it at once (two root modals)
- the picker now closes first; the table opens centred. Not done from the item's
wording: nothing - the "pull metadata into picker tooltips; quant comparison table" is
all there.

2026-10-24 V (92 git pane): `src/git_pane.cpp`, a Tools > Git tab on the pinned project
folder. Every git call runs through `native::runProcess` on a worker (`git -c
color.ui=never ...`): `status --porcelain=v1 --branch` for the branch / upstream / ahead-
behind and the file list (index and worktree states, untracked, renames), `add` / `reset`
per file or for everything, `diff --cached` (or a file's working-tree diff; an untracked
file is shown as all additions) rendered with the existing `diffview::render`, `commit
-m`, `blame --date=short -w [-L range]`, and `log base..HEAD` + `diff base...HEAD` for
the PR. Two model calls through `api_.completion` (non-streaming, think block and fence
stripped): a commit message from the staged diff plus the last eight subjects (72-char
imperative summary, short body), and a PR description (title, Summary, Changes, Testing)
from the branch's commits; both land in editable boxes - nothing is committed or copied
without a click. "Explain in chat" sends the shown diff as a prompt. The pane refreshes
every 10 s while visible. Verified against a scratch server and a scratch repo (one
commit, a modified and an untracked file): the list showed M src/app.py and ? src/cli.py;
Stage all turned both ticks on and the diff view showed both files' hunks; Draft message
produced "Update greeting format and add CLI to display arguments" with a two-line body;
Commit made 823f9df and the tree read clean; blame on src/app.py attributed line 1 to the
first commit and the rest to the new one; with base set to a branch at the old HEAD,
Draft PR description produced a titled Markdown description with Summary / Changes /
Testing and a Copy button appeared. Not exercised: Unstage, per-file toggles (the same
`gitStage` with the other flag) and a commit failure (hooks, empty identity). Not done
from the item's wording: nothing beyond push/pull, which are left to the terminal.

2026-10-24 IX (135 usage dashboard): `src/core/usagelog.{h,cpp}` (append / load of
support/usage.jsonl, roll-ups by day, by model, day x model matrix, totals with cost from
per-model $/1M prices and energy from a joules-per-token figure for local models,
completion-token-weighted tok/s, CSV) and `src/usage_pane.cpp` (`recordUsage` from
`afterCompletion` for every reply without an error - usage from the backend, tokens
estimated from the text when none came, timings from the message's MessageTiming, tok/s
derived when the backend gave none, local vs API from the model list; Tools > Usage with
the on/off switch (extra["usageLog"]), a 7/30/90/all range, Export CSV (save panel),
Reveal log, six headline tiles (+ cost when any model is priced), a stacked bar chart of
output tokens per day by model with a tok/s line and per-day hover tooltips, a by-model
table, a price editor (extra["usagePrices"]) and the joules slider
(extra["usageJoulesPerToken"], default 0.5 J); palette "Usage dashboard"). `--usagetest`
runs 11 offline checks. Verified against a scratch server: 37 seeded records over six
days plus one live qwen3:8b reply (logged as 4305 in / 96 out, 53.1 tok/s, 7.9 s to
first token) gave "38 replies - 130.5k in - 34.6k out - 32.6 min - 31.9 tok/s - 22.93
Wh", seven bars stacked in three model colours with the tok/s line, and the by-model
table (qwen3:8b 19 replies at 51.3 tok/s, llama3.3:70b 11 at 8.9, qwen3:32b 8 at 18.1).
Not exercised in the GUI: Export CSV (a save panel) and the price editor (the cost column
is covered by the offline test). Honest limits: energy is tokens x a constant, not a
measurement; prompt tokens for server-task replies are what Open WebUI reports.

2026-10-24 IX (138 backup and import): `src/core/importers.{h,cpp}` (detect + convert:
ChatGPT's conversations.json mapping tree followed from current_node - or a leaf when it
is missing - up to the root, hidden system nodes and empty parts skipped, code parts
fenced, model_slug kept as modelName; Claude's conversations.json chat_messages with
`text` or the content[] text parts, ISO times parsed; both assembled into the chat body
`POST /chats/new` takes with a linked history, tags imported-chatgpt / imported-claude)
and `src/backup.cpp` (Tools > Backup: Export everything builds a work folder - chats/
one JSON per chat from the id list with bodies from the mirror or fetched, settings.json,
usage.jsonl, redactions.json, apply-history.json, plugins/, optionally private/ and the
mirror-*.db - plus a README, zips it with `ditto -c -k --norsrc --noextattr` and reveals
it; Import chats takes one or more files, converts recognised exports or accepts a
backup's chat JSON, creates each chat on the server with a progress counter and
refreshes the list; palette commands for both). `--importtest` runs 11 offline checks.
Verified against a scratch server: the sample ChatGPT export (two conversations, one
with a two-way branch) imported as two chats with 4 and 2 messages, the current branch
kept and the code part fenced; the Claude sample imported as "Haiku about autumn"; Export
everything wrote a 411 kB zip holding chats/ (all three), settings.json, usage.jsonl,
redactions.json, apply-history.json, plugins/ and the mirror database. The first zip
carried __MACOSX resource-fork entries - ditto now runs with --norsrc --noextattr (not
re-run). Not covered: importing a backup zip whole (unzip it and pick the chats/ files),
restoring settings or the mirror from a backup (copy them into the support folder), and
ChatGPT exports with images or tool nodes (text parts only).

2026-10-24 IV (84 recipes): `src/core/recipes.{h,cpp}` (Recipe {id, name, description,
model, steps[{prompt, model}], newChatPerRun} with JSON round-trip; `inputs` collects the
{{input:}} fields across steps; `expandStep` splices {{prev}} / {{step:N}} from the
replies so far behind markers so their text is never re-scanned, then runs the prompt
variables; three example recipes) and `src/recipes_pane.cpp` (settings extra["recipes"];
Tools > Recipes with a card per recipe - Run / Edit / Schedule daily 08:00 / Delete - an
editor modal with per-step prompts and optional per-step models, "Add the examples"; the
runner: inputs form once, a fresh chat titled "Recipe: <name>", each step sent with
`sendMessage` (prompt-variable form skipped since the step is already expanded; redaction
still applies) and the next sent from `tickRecipe` once the awaited reply finished without
an error; Stop; palette entries "Run recipe: <name>" per recipe and "Stop the running
recipe"; a schedule whose prompt is "recipe:<id>" runs the recipe in `tickScheduler`).
`--recipetest` runs 8 offline checks. Verified against a scratch server with qwen3:8b:
"Add the examples" listed three recipes; Run on "Explain, then quiz me" opened the inputs
form with the Topic field focused, "tides" + Enter started a chat "Recipe: Explain, then
quiz me", step 1 explained tides, step 2 went out with the explanation spliced in for
{{prev}} and came back as three questions with answers, and the finish toast fired; the
chat on the server held both turns. Not exercised: a global hotkey per recipe (the item
mentions one; the palette is the keyboard route), the scheduler path (same code as
scheduled prompts, which the earlier sweep covered), per-step model switches.

2026-10-24 IV (85 watch folder actions + 86 clipboard watcher): `src/triggers.cpp`.
Configuration in settings extra["triggers"] ({folders: [{path, recipe, enabled,
extensions}], clipboard: {enabled, recipe, minChars}}), edited in a TRIGGERS section at
the foot of Tools > Recipes (recipe combos, extension filter, "Watch a folder..." via
the folder panel, the clipboard switch with a warning line, last trigger, files
waiting). Folder watches use `native::watchFolder` (FSEvents); a path that is a regular
non-hidden file with an allowed extension and not yet seen this session is queued; the
tick runs it once its mtime is over a second old (settling) and no recipe is running -
`runRecipeWith` attaches the file, sets {{input:File}} to the path, fills other inputs
from their defaults, and `tickRecipe` sends step 1 once the upload finished. The
clipboard watcher polls the pasteboard once a second, primes itself on the first read,
and runs its recipe for new text of at least minChars that does not match a message in
the open chat (so copying a reply does not loop). Verified against a scratch server with
qwen3:8b: a watched inbox with "txt,md" and a one-step "Summarise the attached file"
recipe: dropping agenda.txt started "Recipe: Summarise a dropped file" with the file
attached, the path in the prompt and a three-bullet summary citing the file; the first
drop sat at "1 file(s) waiting" forever because the settle test compared a wall-clock
mtime with the steady-clock nowMillis - fixed with core::nowSeconds. Clipboard watcher: with it on and the
pasteboard primed before launch, copying a paragraph about the Antikythera mechanism
started "Recipe: Explain copied text" whose step carried the copied text through
{{clipboard}} and came back with a two-sentence explanation (the primer text did not
fire; a copied reply would not either). Not done from the items' wording: a mode from
the menu bar extra (the switch lives in the pane; the extra's menu is unchanged), and
rename / tag / file actions beyond what a recipe's steps do in the chat.

2026-10-24 V (95 test loop): `src/core/testloop.{h,cpp}` (runner detection from the
project's files - pytest when `python3 -m pytest --version` works, else unittest
discovery, plus npm test, cargo, go, swift, ctest with a build dir, make test; the
files a failure output names that exist under the project; a head+tail digest; a green
test per runner) and `src/test_loop.cpp` (Tools > Test loop: runner combo, round budget
1-8, Auto-apply switch, an extra instruction; "Run tests" once or "Run until green";
each round runs the tests on a worker with a 5-minute cap, on failure asks the model
through `gitAsk` - fences kept - for the COMPLETE contents of every changed file as
`path=` blocks, writes the named blocks directly with .bak backups and records them as
the last apply (journal + Undo), then runs again; stops at green, at the budget, when
the reply names no file or changes nothing, or on Stop; with auto-apply off a round
ends in the apply dialog). Rounds show the output digest, the files changed and the
model's reply. `--testlooptest` runs 11 offline checks. Verified against a scratch
server with qwen3:8b on the scratch repo (unittest, `shout()` missing `.upper()`): round
1 failed on test_shout and the model's first file was written (backup beside it), round
2 still failed and a second fix was written, round 3 ran green and the status read
"Green after 3 rounds"; the tests pass from the shell too, and the final shout() is
`f"HELLO, {name.upper()}!"`. Found on the way: `gitAsk` strips an outer code fence from replies
(right for commit messages), which turned the fixer's single `path=` block into a bare
snippet with no file - a `keepFences` flag now leaves the loop's replies alone. Not
exercised: the review (auto-apply off) path and runners other than unittest (pytest is
not installed on this machine).

2026-10-24 V (93 repo map): `src/core/repomap.{h,cpp}` (languageOf; per-language regex
rules for class / func / method / type / const / export / section, one symbol per line,
keyword and assignment lines filtered for C-like code; `render` writes one line per file
- "path: name() L12, class X L3, [ns] L1" - and when over budget first drops methods /
constants / exports, then caps symbols per file, and only then leaves files out, always
with a note) and the wiring in `code_tools.cpp` (`buildRepoMap` walks the project on a
worker, 512 kB per file, up to 3000 files; `repoMapBlock` renders within 32k chars; the
block goes into `projectContextBlock` after the tree; the project popover has the switch
extra["projectRepoMap"] (default on), Reindex and a status line "N symbols in M files
(~Xk tokens sent)"; the map is built when the project folder is known). `--repomaptest`
runs 11 offline checks. Verified against a scratch server with qwen3:8b and this app's
own src/ as the project (2538 symbols in 133 files, ~7.2k tokens): "which file defines
recoverInflight, name three other functions in that file" was answered from the map -
recovery.cpp with saveInflight, clearInflight, tickRecovery, all correct with line
numbers in the reasoning - where the earlier verbose renderer (24k chars, whole files
dropped) had produced invented names. Found on the way: the first two runs on a cold
model (30 s prompt evaluation) ended with the reasoning complete and the content empty,
on the server too; the same messages answered correctly both straight to Ollama and
through the server's REST endpoint, and the third run through the app was fine - so the
client's socket path lost the text and its save wrote the blank back. A chat:completion
event whose `content` is "" was treated as a replace and could wipe the stream; it no
longer does. That is the likely cause, not a confirmed one: the failing event stream was
not captured. Known: the header's ctx figure does not count the project block (16k in,
"~47" shown).

2026-10-24 IX (139 audit log): `src/core/auditlog.{h,cpp}` (JSONL records of kind http -
host, method, path with query/fragment/credentials stripped, status, bytes out/in, ms,
ok, error text - or kind tool - server, tool, qualified name, decision; thread-safe
append; load by range; trim to the newest N lines; byHost; CSV and JSON exports) fed by
a new `net::setObserver` in the HTTP layer: `finish()` now takes the request and reports
a `net::Transfer` (CURLINFO_SIZE_UPLOAD_T / SIZE_DOWNLOAD_T / TOTAL_TIME_T) for every
perform / stream / sse / download, so server calls, direct Ollama, plugin `owui.fetch`
and MCP-over-HTTP are all covered from one place; MCP / plugin tool approvals are
recorded in `makeToolHooks` with how they were decided (never / auto-approve / always /
user / stopped). `src/audit_pane.cpp` is Tools > Audit: on/off (extra["auditLog"]),
Today / 7 / 30 / All, All / Requests / Tool calls, a text filter, Export CSV / JSON,
Clear with confirm, a headline ("22 requests to 1 host - 11.4 KB sent, 35.8 KB received
- 1 tool call") with the top hosts, and a table newest first (time, host, what, status,
out, in, ms; tool rows in green/red). `--audittest` runs 9 offline checks. Verified
against a scratch server with qwen3:8b and the calc MCP server: one prompt that called
the add tool produced 22 request rows (config, auths, chats, models, two /ollama/api/chat
rounds with 1.7-1.9 KB out / 13-15 KB in, the chat save, title generation) and one tool
row "tool add - allowed (always)"; the file matched (22 http + 1 tool). Not covered by
design: the socket.io WebSocket (not curl - the server's push channel, inbound) and MCP
stdio servers (local processes; their tool decisions are logged). Not exercised: the
filter box and the export save panels.

2026-10-25 H (62 math and diagrams): `src/diagrams.cpp` builds a self-contained page
per block - KaTeX 0.16.11 (`$$...$$` paragraphs and math/latex/tex fences), Mermaid 11
(mermaid/mmd) or Viz.js 3.11 (dot/graphviz), all from jsDelivr, in the current palette's
colours - and `native::renderHtmlToPng` (new, `src/native.mm`) renders it in an offscreen
WKWebView: one job at a time on the main thread, the page posts its body height to a
script message handler when fonts and scripts settle, the view is resized to it and
snapshotted to PNG; 15 s timeout. `markdown.cpp` routes those fences and `$$` paragraphs
to `renderDiagramBlock` (image through the async `images::` cache keyed by kind + code +
width + theme; header with Source / Hide source and Copy; failure note plus the code
block when the render fails or the CDN is unreachable). Setting `extra["renderDiagrams"]`
(default on) with a palette toggle. Verified on a scratch server with a seeded reply
holding all three kinds: quadratic formula, a 5-node flowchart and a 4-node digraph
each render as crisp images sized to their content; Source toggles the code block
underneath; a one-line `$$e^{i\pi}+1=0$$` inside prose stays as text by design (only
whole `$$` lines are display math - inline math is not rendered). Lessons: a transparent
(alpha 0) offscreen window stops WebKit painting and requestAnimationFrame never fires
off-screen (use a timer); a JSON string literal cannot live in an `onload` attribute
(quotes end the attribute - that was the "Script error."). Not covered: light theme,
very tall diagrams (image is capped to the column width only), offline fallback path
beyond the earlier failing runs, and the artifact-view KaTeX half of the item (the
inline image is what shipped).

2026-10-25 III (74 direct backends), scoped to private chats: `src/core/backends.{h,cpp}`
(app-free; Backend {id, name, kind ollama|openai|anthropic, url}; normalizeUrl adds /v1
for OpenAI-compatible bases; listModels for /api/tags, /models and /v1/models; streamChat
speaks the Ollama /api/chat NDJSON dialect outward whichever backend answers - OpenAI
chat completions and Anthropic Messages requests are built from the Ollama-shaped payload
(tool_calls get ids the following tool messages reuse, images become data URLs / base64
blocks, options map to max_tokens / stop / temperature, format json|schema maps to
response_format) and their SSE streams are decoded back into Ollama chunks, tool-call
arguments reassembled, usage mapped to prompt_eval_count / eval_count, error bodies kept
for the message; its own SSE splitter so a non-SSE error body is not lost).
`agents::LoopConfig::transport` is the hook: when set the tool loop never touches the
server passthrough or the Ollama URL. `src/backends_pane.cpp`: Settings > Direct backends
(rows with kind, URL, key set / no key, Test, Remove, password field + Save key / Forget
key; add form with kind combo; keys in the keychain under backend:<id>, or a 0600 file
under OWUI_NO_KEYCHAIN), `backendList()` (the Ollama URL first as the implicit "ollama"
backend, then extra["backends"]), per-backend model cache, and the header button of a
private chat (backend list + filtered model list; `desktop_backend` on the chat; the send
path uses the chat's own model for such chats, not the server selection). Private chats
and their title call now always go through the transport - before this they preferred
the server's /ollama/api/chat passthrough and only fell back to the Ollama URL, which
contradicted "never on the server". Palette "Direct backends...". `--backendstest` runs 16
offline checks. Verified on a scratch server: an OpenAI-compatible backend pointing at the
local Ollama's /v1 lists 8 models (Test and the header popup), a private chat switched to
it with qwen3:8b streamed a reply with reasoning and "434 in / 415 out", and the audit log
shows only POST localhost:11434 /v1/chat/completions (reply + title) - nothing to the
server's passthrough. Not covered: a real OpenAI / Anthropic key (the Anthropic dialect
is self-test only), tool calls through the OpenAI dialect live (converted and unit-tested,
not exercised against a model), server chats on a direct backend (out of scope: those are
the server's), and the keychain path itself (the scratch run uses the file fallback).

2026-10-25 VI (103 native document chat): `src/core/doctext.{h,mm}` (app-free: kindFor;
extract -> pages with labels - PDFKit page strings for PDF, a zlib-backed zip reader plus
an XML text walker for docx (pages at Word's lastRenderedPageBreak, else parts of ~3500
chars) / pptx (one page per slide, numeric order) / xlsx (shared strings resolved, rows as
tab lines, one page per sheet with its name), textutil via a 0600 temp file for rtf / doc
/ odt / webarchive, plain text and code as parts, HTML stripped; render fits a document
into a character budget - whole when it fits, else ~1000-char chunks scored lexically
against the question (tf x idf over crude 6-letter stems) with the opening chunk always
kept and a note saying how many parts were shown). `src/local_documents.cpp`: attachBytes
routes a supported file to attachDocument (worker extraction, cache under
support/documents/<fnv-hash>.json plus the .pdf bytes for the viewer; message file
{type: local_document, name, hash, kind, pages, chars}; private chats never upload, server
chats upload when extraction fails) - the setting extra["localDocuments"] (Chat card,
default on); buildCompletionPayload prepends one system message with every attached
document on the branch, each given a share of ~40% of the model's window; user bubbles
show "name - 3 pages, read on this Mac" (PDF: click opens the column); drawLocalDocumentRefs
turns [name p.N] / [name slide N] / [p.N] in a reply into a "Cited:" row whose PDF entries
open the document column at that page ("local:<hash>" in openDocument); pdfGoToPage now
re-applies the jump 150 ms after a load because the first one was lost to layout.
`owui://attach?path=` added for scripts and the test harness. `--doctest` runs 14 offline
checks including a Quartz-drawn two-page PDF. Verified on a scratch server in a private
chat: a generated 3-page Word report and Apple's 16-page License.pdf attached by the URL
scheme, chips read "3 pages" / "16 pages", the audit log shows zero file uploads and only
the Ollama /api/chat calls (35 KB prompt), and the reply cited "[lantern-report.docx p.2]"
with the right budget figure and "[license.pdf p.7]"; both chips rendered, the PDF one
opens the column on page 7; the chat reloads with chips intact. Honest notes: in the two-document
run the model's license citation was off (it said p.7; a stray "terminate" is on p.6 and
the Termination clause on p.8) - the 80k-char PDF went in as excerpts and "termination"
did not match "terminate" lexically, so 6-letter stemming was added; a re-run with the
license alone (49k chars of it sent, every page represented, logged per block under
OWUI_DEBUG) then quoted "7. Termination. This License is effective until terminated."
with [license p.8], and the short name still resolved to the chip. docx without
Word's page-break markers (most generated files) gets "part N" labels not real pages,
Excel numbers are raw (no formats), textutil formats and the server-chat upload fallback
were self-test / code-path only.

2026-10-25 IV (88 browser tool): `src/browser_tool.{h,cpp}` - a built-in tool server
"browser" (serverId builtin:browser) whose five tools join mcpToolSpecs() whenever MCP
tools are on and extra["browserTool"] (default on) allows: fetch (libcurl GET capped at
8 MB; HTML -> Markdown through the URL-capture converter, now exported as
app::htmlToMarkdownText in `src/html_markdown.h`; PDF -> pages via doctext; text / JSON
raw; binary refused), render / links / screenshot (the offscreen WKWebView from #62
gained a URL mode: `native::browseUrl` loads the page, waits `wait_ms`, evaluates a script
that returns title, main/article/body innerText and up to 150 links as JSON, snapshots
the viewport when asked; a timeout reads what the page shows by then and says so;
didFailNavigation after a partial load still extracts) and search (DuckDuckGo's HTML
endpoint, parsed for result__a / result__snippet with the uddg target decoded, ads
dropped). URL policy: http(s) only; localhost, loopback, 10/8, 172.16/12, 192.168/16,
169.254/16, 100.64/10, ::1, .local / .localhost / .internal blocked unless
extra["browserToolAllowLocal"]; results clipped to extra["browserToolMaxChars"] (default
12000) with a truncation note. Every call runs through the existing approval card and is
audited like any tool. `drawBrowserToolCard` in the MCP tab: on/off, allow-local, size
slider, Try fetch / Try render with the result in a box. `--browsertest` runs 12 offline
checks (policy, clip, percent-decoding, result-page parsing, specs, refusals, the
Markdown wrapper). Verified on a scratch server in a private chat with qwen3:8b: "fetch
example.com" raised the approval card, Allow once ran it in 87 ms and the model then
asked for links (WKWebView path, 1060 ms, "url loaded, settling 800 ms" in the log) and
answered "Example Domain" with the iana.org link; a second prompt ran search (783 ms) and
listed three real DuckDuckGo results (open-webui/desktop on GitHub, another desktop repo,
openwebui.com); the audit log shows example.com GET / and html.duckduckgo.com GET /html/
plus the three tool decisions. Try render on the MCP-tab card returned example.com's title, final URL and visible text
(its Arabic and Chinese paragraphs included). Not covered: screenshot (code path only)
and render as a model call (links exercised the same WKWebView path), a PDF or JSON fetch,
a JavaScript-only page, and DuckDuckGo's layout is a scrape that can break without
notice (the tool then says so and suggests fetching a likely URL).

2026-10-25 IV (87 script tools): `src/core/scripttools.{h,cpp}` (app-free: parse reads a
comment header - owui-tool / description / param "name type [required] [enum(a|b)] -
desc" / timeout / sandbox / runner - with #, //, --, ; or * leaders; the runner comes from
the extension (zsh, bash, python3, osascript, osascript -l JavaScript, ruby, perl, swift,
php; a shebang file runs itself and must be executable); schema() turns params into a
JSON schema; scan lists a folder's header-bearing files sorted, flagging duplicate names;
validateArgs checks required and enum; argv builds "/usr/bin/env OWUI_ARGS=<json>
OWUI_TOOL=<name> OWUI_ARG_<NAME>=<value>... <runner> <path> <json>" so it works both
through sandbox-exec and plain env; three examples). `src/script_tools.cpp`: the folder
support/scripts, a 2 s scan cache, the built-in "scripts" server (serverId
builtin:scripts, extra["scriptTools"] default on) added to mcpToolSpecs(), callScriptTool
(native::runProcess with the tool's timeout and sandbox flag; working folder = the pinned
project, else cache/script-run for sandboxed scripts or home otherwise; stdout capped at
24k; non-zero exit and timeouts come back as tool errors), an invoke branch in
makeToolHooks (which now captures `this` and the project folder), and a card in Tools >
MCP: on/off, folder path, Reveal in Finder / Rescan / Write the examples, every tool with
file, runner, sandboxed/UNSANDBOXED, params and header errors in red, and Try with an
editable JSON argument box. `--scripttest` runs 16 offline checks including a real
sandboxed zsh run (args by env, argv and OWUI_ARGS), the Python example, a 1 s timeout
killing sleep 4, a non-zero exit with stderr, and sandbox-exec refusing a touch in $HOME.
Verified on a scratch server: Write the examples created disk_usage.sh, word_count.py and
frontmost_app.js (0700) and the card listed them; in a private chat qwen3:8b called
scripts__word_count with the exact text, the approval card showed the arguments, Allow
once ran it in 45 ms, the card showed {"words": 9, "lines": 1, "chars": 44} and the reply
reported those numbers; the audit log has the tool row. Not covered in chat: disk_usage
and the JXA example (frontmost_app is marked sandbox: off because System Events needs
it, and the first run may raise macOS's automation prompt), the Try box in the card, and
scripts on a pinned project folder.

2026-10-25 VI (109 semantic chat search): `src/core/chatindex.{h,cpp}` (app-free SQLite index
chat-index.db: chats(id, title, updated_at) + chunks(chat_id, message_id, n, role, text,
hash, vector); indexChat chunks every user/assistant message of a chat at ~900 chars,
embeds only chunks whose hash changed - "search_document: <title>\n<text>" in batches of
16 through a caller-supplied Embed - drops chunks of removed messages, returns -1 and
keeps rows on an embedding failure; needsIndex compares updated_at; search embeds
"search_query: ..." and dot-products every stored unit vector, one hit per message, best
first; stats / clear / removeChat / chatIds). `src/local_embed.h` exports the local
index's embed() as app::embedTexts (nomic-embed-text at the Ollama URL). `src/chat_index.cpp`:
tickChatIndex runs a pass ~8 s after launch and then once a minute (40 changed chats per
pass, "more to do" reschedules in 10 s; an embedding failure pauses for 5 minutes with the
reason in the status), reading chats from the local mirror (store::listChats + getChat,
every branch; private chats are not in the mirror so never indexed) and dropping index
rows for chats the mirror no longer has; the sidebar search gained a "By meaning"
checkbox (extra-free, per session) - with it on the field searches the index (350 ms
debounce, 25 hits) and the list shows title, You:/AI: snippet and a similarity percent,
tooltip with the passage; a click opens the chat and focusMessageInChat puts the accent
bar and scroll on that message, switching to its branch when it is not the current one
(pendingFocusMessage is consumed in openChat's post handler); card "CHAT INDEX" under
Tools > Local index (on/off extra["chatIndex"], counts and size, Index now / Rebuild /
Clear, status line); palette "Search chats by meaning". `--chatindextest` runs 14 checks
with a fake bag-of-words embedder (incremental re-embedding counts, removal, ranking, k,
failure paths). Verified on a scratch server with three API-seeded chats (sourdough,
sailing, Rust): 28 s after launch the card read "3 chats, 6 passages indexed - 3 chats
updated (6 passages embedded)" with no click; the query "bread that came out heavy and
wet inside" (none of the chat's words) ranked both sourdough passages first (67% / 61%)
over sailing (49%) and Rust (45%); clicking the top hit opened Sourdough troubleshooting
with the assistant reply focused. Not covered: large histories (the scan is a linear
dot product over every vector - fine to tens of thousands of passages, no ANN), a chat
edited after indexing (the incremental path is self-test only), the Rebuild / Clear
buttons, and the branch switch for a hit off the current branch (self-test does not
cover focusMessageInChat).

2026-10-25 VI (107 memory proposals): `src/core/memoryprop.{h,cpp}` (app-free: the system
prompt asking for 0-3 durable facts about the user in their own voice as {"memories":
[...]}, the user prompt carrying both sides of the turn and the existing memories; parse
handles the object form, a bare array, fenced JSON, a <think> prefix, object items and
bullet lines; normalize strips filler and punctuation; similar = containment or >= 75%
word overlap; filter drops empties, over/under-long, near-duplicates of existing /
skipped / each other, caps at 3). `src/memory_proposals.cpp`: maybeProposeMemories runs
from afterCompletion for server chats (never private ones) when the user's message has
40+ characters - a quiet non-streaming call to the reply's own model, existing memories
fetched once for the dedupe, candidates kept per assistant message id; the card under the
reply ("Remember this?" with Skip all) shows each with Save / Edit (inline field, Enter
saves) / Skip; Save posts /api/v1/memories/add and appends to the pane's list; Skip adds
the text to extra["memorySkipped"] (capped at 200) so it is not proposed again; setting
extra["memoryProposals"] in the Chat card; palette "Propose memories from this reply"
re-asks for the last reply. `--memorytest` runs 11 offline checks. Verified on a scratch
server with qwen3:8b: a message stating a studio in Austin, a bullet-point preference and
a peanut allergy produced exactly those three proposals (model call 24.5 s after the
reply); Save on the first showed "Saved: ..." in green, toasted "Remembered: ...", and the
server's memory list plus the Memories pane show it; Skip on the third removed the row and
settings carry it under memorySkipped. Honest notes: the first attempt sent the message
with MCP tools on, and the reply hung for ~5 minutes in the server's /ollama/api/chat
passthrough (the known flaky passthrough path, not this feature) - the verified run had
MCP tools off; Edit, Skip all, the dedupe against existing memories with a live list, and
the palette command were not exercised in the GUI.

2026-10-25 VIII (127 Obsidian / vault sync): `src/core/vault.{h,cpp}` (app-free: safeName
for file stems, chatFileName "<title> (<id8>).md", chatMarkdown = YAML front matter (id,
title, created, updated, models, tags, type, source link) + the current branch walked
from currentId with "## You" / "## <model>" sections, noteFileName / noteMarkdown with
id + title + updated front matter, parseNoteFile reading a file back (a file without
front matter is a new note titled after the file name), readFrontMatter with quoted
values and CRLF; note timestamps normalised from Open WebUI's nanoseconds). `src/vault_sync.cpp`:
extra["vaultFolder"] / vaultChats / vaultNotes; syncVault on a worker writes changed chats
from the local mirror (state in <root>/.owui-vault.json remembers file + updated_at per
id, renames drop the old file, chats gone from the server lose their file) and syncs
notes both ways against /api/v1/notes (server newer -> rewrite the file; file mtime newer
than the recorded one -> parse and updateNote; both -> server wins and the local text is
kept as "<title> (conflict <time>).md"; a new .md without a known id -> createNote and the
file is rewritten with front matter; notes deleted on the server lose their file);
tickVaultSync watches <root>/Notes with FSEvents (2 s debounce), runs a first pass ~45 s
after launch and every ten minutes, and syncMirror's post handler runs it after every
chat sync; card under Settings > Vault sync (folder field + Choose... + Reveal, the two
checkboxes, status, Sync now / Rewrite everything); palette "Sync the vault now".
`--vaulttest` runs 10 offline checks (a null parentId on the root message crashed the
first run - value() throws on a present-but-null key - fixed with a null-safe getter).
Verified on a scratch server with two seeded chats and one note, the vault folder set in
settings: 30 s after launch Chats/ held both files with correct front matter and
transcripts and Notes/ the note; appending a line to "Boat maintenance.md" and dropping a
new "Newsletter ideas.md" (no front matter) were picked up by the watcher within ~12 s -
the server note gained the line and a new server note "Newsletter ideas" appeared, whose
file was rewritten with id/title/updated front matter; the Settings card rendered. Not
covered: the conflict path, a note deleted on the server, a chat renamed after export,
Choose... (a system dialog), and a real Obsidian vault (any folder behaves the same).

2026-10-25 IX (136 health dashboard): `src/core/sysmetrics.{h,mm}` (app-free: one Sample
per call - CPU from host_processor_info tick deltas, memory from host_statistics64
(active + wired + compressed) and hw.memsize, swap from vm.swapusage, GPU utilisation and
"In use system memory" from the IOAccelerator PerformanceStatistics registry property,
Metal currentAllocatedSize / recommendedMaxWorkingSetSize, NSProcessInfo thermal state,
IOPowerSources battery and source, statvfs free space on the Ollama models folder
(OLLAMA_MODELS or ~/.ollama/models); a Ring of 300 samples with series() for plotting).
`src/health_pane.cpp`: Tools > Health - four tiles (CPU, GPU, Memory, Metal) each with a
value, a detail line and a 5-minute PlotLines sparkline; pills for thermal (coloured),
power/battery, free disk; Pause; a heavy-swap warning above 4 GB; "Models in memory
(Ollama)" table from /ollama/api/ps (direct /api/ps at the Ollama URL when the passthrough
fails) with size, GPU share, context, unload countdown parsed from expires_at, and an
Unload button (ollamaUnload); a residency timeline with one lane per model filled while it
was resident, built from the polls (15 min at 5 s). Sampling runs every second whenever
signed in; Ollama is polled only while the tab is drawn (healthTabHidden clears the flag).
IOKit linked. `--healthtest` runs 9 checks against the live machine. Verified on this
M3 Max: tiles read CPU 12%, GPU 18% with 9.5 GB in use, memory 39.3 / 68.7 GB with
compressed 1.0 GB and swap 31.5 GB (the warning showed), Metal 55.7 GB working set,
thermal nominal, on power 100%, 524.5 GB free; the table listed qwen3:8b (7.2 GB, 100% in
GPU memory, context 16384, 9 min) and nomic-embed-text; Unload on nomic-embed-text
removed it from the table and Ollama's /api/ps confirmed only qwen3:8b remained; the
residency lanes drew. Not covered: Intel Macs / machines where IOKit has no utilisation
key (the tile says n/a), a battery-powered run, thermal states other than nominal, and
the passthrough-down fallback to the direct Ollama URL.

2026-10-25 III (73 model library browser): `src/core/modellib.{h,cpp}` (app-free: URLs for
the Hub search / tree API and ollama.com search / tags pages; parseHfSearch (id,
downloads, likes, pipeline); parseHfTree (single-file .gguf entries with sizes, split
files skipped, sorted by size, quant from the file name -> pull name
"hf.co/<repo>:<QUANT>"); parseOllamaSearch (library links deduped, tag links skipped,
description from the <p title>); parseOllamaTags (tags deduped, the first "<n>GB|MB" after
each link as its size); quantOf, parseSize / humanSize, enoughSpace (size x 1.1 + 512 MB),
parseProgress for pull lines). `src/model_library.cpp`: Tools > Models - source combo
(Ollama library / Hugging Face GGUF), query, Search; results as cards with a Variants
button that loads tags or GGUF files into a table (variant, pulls-as, size, Pull - disabled
with a tooltip when the disk cannot take it); a pull queue card at the top where jobs run
one after another through api_.ollamaPull with a progress bar, Cancel, Clear / Remove /
Drop, and refreshModels() after a success; free space from statvfs on the Ollama models
folder. Palette "Find and pull models". `--modellibtest` runs 9 offline checks.
Verified on a scratch server against the live sites: "qwen3" on the Ollama library listed
12 models with descriptions; Variants on qwen3 listed latest / 0.6b / ... / 32b with sizes
(523 MB to 20 GB) - after a fix: ollama.com answers a JSON Accept with a bare tag list and
no sizes, so its pages are now asked for as HTML; the Hugging Face search listed 25 GGUF
repos with downloads and likes; Variants on Qwen/Qwen2.5-0.5B-Instruct-GGUF listed nine
quantisations from Q2_K 415 MB to FP16 1.3 GB; Pull on Q2_K queued it, the bar showed the
download, and `ollama list` then held hf.co/Qwen/Qwen2.5-0.5B-Instruct-GGUF:Q2_K (415 MB;
removed again after the test with `ollama rm`). Not covered: Cancel mid-pull, the
not-enough-space branch (this disk has 524 GB free), split-file GGUFs (skipped by
design), and the Ollama pages' HTML is a scrape that can break when the site changes.

2026-10-25 III (79 token budgets): `src/core/budgets.{h,cpp}` (app-free: Limits
{chatTokens, dayTokens, dayUsd, warnAt} <-> settings extra["budgets"]; spend() sums
prompt+completion tokens and priced cost from the usage log for today (local date) and
for one chat across all days; check() returns Ok / Warn (past warnAt of any cap) / Over
with a message naming each cap crossed and the numbers, counting the send about to go
out; summary() for tooltips). `src/budgets_pane.cpp`: interceptBudget runs in
sendMessage after the redaction intercept - a Warn toasts once per hour per cap, an Over
holds the text behind a "Budget reached" modal with Send anyway (sendingOverBudget_
bypass) / Hold it (text back into the composer) / Open budgets; the chat header shows a
"budget NN%" pill (worst cap, yellow past warnAt, red past 100%) with the summary as a
tooltip; Tools > Usage gained a BUDGETS card (three cap fields, a warn-at slider, Save
caps, and a gauge per cap set). The usage log is reloaded at most every 10 s. Palette
"Budgets (token and cost caps)". `--budgettest` runs 8 offline checks. Verified on a
scratch server with a 300-token day cap seeded in settings and a pretend price on qwen3:8b:
the first send stopped at the sheet "today's tokens: 42.0k of 300 used" with the header
pill red at 13990%; Send anyway sent it and the reply came; the Usage tab's card showed
the caps and a red gauge "46.3k / 300 today, tokens"; a slider that printed 1% instead of
80% was fixed and re-checked. Not covered: the Warn toast path (the log was already far
past the cap), the per-chat cap and the dollar cap in the GUI (same code path, self-test
only), Hold it and Open budgets.

2026-10-25 III (77 warmup and residency): `src/core/warmup.{h,cpp}` (app-free: Plan
{enabled (default off), keepAliveMinutes 30, onlyWhileFrontmost, releaseOnSleep,
releaseOnQuit} <-> extra["warmup"]; loadPayload = /api/generate with an empty prompt and
keep_alive "<n>m", releasePayload with "0"; shouldRenew at half the keep-alive, never
hidden when frontmost-only, at least a minute apart). `src/warmup_pane.cpp`: tickWarmup
warms the selected Ollama model ~5 s after launch (a workspace model maps to its base;
non-Ollama models are skipped) and renews while the app is in front; api_.ollamaGenerate
(new, POST /ollama/api/generate, 300 s) with a direct fallback to the Ollama URL;
onSleepWake from a new native sleepWake callback (NSWorkspace will-sleep / did-wake
observers in native::install) releases before sleep and re-warms after; warmupOnQuit
releases on shutdown when asked; Settings > Warmup and residency card (enable, keep-alive
slider, the three checkboxes, status, Warm now / Release now); palette "Warm the model
now" / "Release the model". `--warmuptest` runs 6 offline checks. Verified on a scratch
server with warmup on and a 2-minute keep-alive: qwen3:8b, unloaded beforehand, was
resident 25 s after launch with expiry two minutes out; 70 s later the expiry had moved
on by the renewal; Release now first appeared to do nothing - the unload ran (869 ms)
but the next tick re-warmed at once because a release reset the "never warmed" state -
fixed with a hold flag that Warm now or a wake clears; after that Release now left Ollama
with nothing resident ten seconds later and the status read "released". Not covered:
real sleep / wake (the observers are registered; the handler is the same code the
Release / Warm buttons use), release on quit, and the frontmost gate (the app was in
front throughout).

2026-10-25 II (70 message selection): `src/core/msgselect.{h,cpp}` (app-free over plain
nodes: rangeBetween on the visible path; spliceOut removes a node and re-attaches its
children to its parent in place - or makes them roots - and moves currentId to the last
child or the parent when it was the removed one; markdown of a selection with "## You"
/ "## <model>" headings; linearChain copies a selection as a fresh chain with new ids).
`src/message_selection.cpp`: Shift-click on a message header toggles / extends a range
from the anchor; the palette's "Select messages" turns on selection mode with a checkbox
in the margin of every message; selected headers get a tinted band and an accent bar; a
floating bar at the bottom of the transcript shows "N selected" with Copy (clipboard as
Markdown), Export... (save panel), Move to new chat (createChat with the copied chain -
or a new private chat - then the originals are removed here), Delete (splice out, pins
and memory proposals dropped) and Clear / Done; Escape clears. A server chat's update
merges history.messages, so deleting goes through DELETE /chats/{id}/messages/{mid} for
each removed id before the re-linked tree is saved - without that the server kept the
removed nodes as a dead sibling branch (found in the first run). `--selectiontest` runs 8
offline checks. Verified on a scratch server with API-seeded four-turn chats (the harness
grew a `shiftclick` helper that presses a real Shift key around a CGEvent click - a Shift
flag on the mouse event alone never reaches ImGui's modifier state): Shift-clicking two
headers selected the three messages between them with the band and the bar; Delete
removed them, the transcript read Madrid -> You are welcome, and after the fix the
server held 6 of 8 messages in one joined branch; selecting two and Move to new chat
produced "Moved: A yellow one?" with exactly those two on the server and left Fruits with
four in one thread. Not covered: Copy and Export (clipboard / save panel), selection mode's
checkboxes, a private chat move, and deleting a message that still streams (refused).

2026-10-25 IX (140 low-power mode): `src/core/lowpower.{h,cpp}` (app-free Policy {enabled
on, idleAfterSec 8, idleFps 8, batteryFps 30, deferOnBattery} <-> extra["lowPower"];
targetFps = 60, capped to batteryFps on battery, to idleFps once idle and not busy;
deferHeavyWork = on battery with the switch on; stateName). `src/low_power.cpp`:
tickLowPower at the top of every frame - any mouse movement, wheel, button, typed
character, active item or held key counts as input; a reply streaming or a toast on
screen (new widgets::toastsVisible) counts as busy; the power source is read every 30 s
through a cheap sysmetrics::onBatteryNow; the resulting rate goes to the Metal view via
native::setPreferredFrameRate (MTKView.preferredFramesPerSecond, implemented beside the
view in main.mm). deferHeavyWork() now gates the chat index pass, the vault sync passes
(the folder watcher stays), the 10-minute mirror sync and the warmup renewals. Settings >
Low power card with the sliders, the battery switch and a live line ("idle, on battery -
drawing at 8 fps - last input 12 s ago"). Palette entry. `--lowpowertest` runs 9 offline
checks. Measured on this M3 Max with the app idle in front of a scratch server, CPU time
over 20 s: 10% with the mode off, 2% with it on (the same build, only the setting
differed). The card then read "idle, on battery - drawing at 8 fps" - the MacBook was
unplugged at that point, so the battery branch ran for real. Not covered: the on-battery
30 fps ceiling while active (not measured separately), the deferral of the ticks (the
gate is a one-line check per tick, exercised by the self-test's policy only), and whether
8 fps feels right while scrolling after an idle spell (the first input restores 60 fps on
that same frame).

2026-10-25 V (96 editor bridge): `src/core/editorbridge.{h,cpp}` (app-free: parseFileRef
accepts "path:line[:col]" when the path has a slash or an extension and rejects clock
times, URLs, bare words and line 0; linkFor / parseLink in an "editor://" scheme;
resolvePath expands ~ and joins relative paths to the project root; openCommand for VS
Code / Cursor / Zed (file URL schemes with line and column), Xcode (xed -l), Sublime
(subl file:line:col) and a custom template with {file} {line} {col} where {file} is quoted
and splitArgs is quote-aware; explainPrompt). `src/ui/markdown.cpp`: file references in
prose (at a word start) and whole code spans that parse as a reference become underlined
links carrying the editor:// URL; `src/chat_pane.cpp` sets options.onLink so editor://
goes to App::openInEditor and everything else to the browser. `src/editor_bridge.cpp`:
openInEditor resolves against the pinned project folder, then home, checks the file
exists, builds the command for extra["editor"] (default vscode) / extra["editorCommand"],
runs it unsandboxed on a worker and toasts the result; Settings > Editor card (combo,
custom command field, "Test: open settings.json at line 1"); `owui://explain?file=&line=
&language=&text=` in handleOpenUrl reads text= to the end of the query so a selection with
'&' survives, builds the explain prompt and sends it (send=0 fills the composer instead).
`docs/EDITOR-BRIDGE.md` has VS Code / Cursor tasks + keybinding, a Zed task, and the
Services / Automator route for Xcode. `--editortest` runs 12 offline checks. Verified on a
scratch server with a seeded reply naming `src/app.cpp:1519`, src/chat_pane.cpp:42,
README.md:1 and "12:30", the project folder pinned to this repo and a custom editor
command pointing at a shell script that logs its arguments: the three references
rendered as links and the time stayed text; clicking two of them logged the absolute path
with line 42 / line 1 and column 1; `open -a <app> "owui://explain?...&send=0"` opened a
new chat with "Explain this code from src/app.cpp:1519 ..." and the fenced snippet in the
composer. Also fixed on the way, from a crash report: pinning the project folder made
repomap::extract throw std::regex_error (error_complexity) on a long line of this repo
and the uncaught exception killed the app one second after launch - lines over 2000
characters are now skipped and regex_search is wrapped, with a self-test. Not covered:
the real VS Code / Zed / Xcode commands (self-test only - no editor was launched), the
Test button, a reference whose file is missing (toast path), and the Sublime `subl`
command, which relies on it being on the GUI PATH.

2026-10-25 VI (110 cited export): `src/core/citeexport.{h,cpp}` (app-free: fromServerSources
reads Open WebUI's sources array - name / url / type, the first retrieved passage as a
one-line snippet, metadata page made 1-based, strings accepted, duplicates by url or title
collapsed; fromLocalCitations finds "[name p.N]" / slide / part / sheet citations of the
chat's locally read documents by name or stem; markdownWithFootnotes turns "[n]" into
"[^n]" only when such a server source exists, numbers local citations after them and
appends the definitions with title, URL, page and passage; bibtex writes @misc entries
with a title-derived key, \url, page / attached-document notes, accessed date and year,
braces and ampersands escaped; plainList). `src/cited_export.cpp`: a book button beside
Copy under any reply that has sources, with a popup - Markdown with footnotes, sources as
BibTeX, plain list - and palette commands for the last reply. `--citetest` runs 7
offline checks. Verified on a scratch server with a seeded reply citing [1] (a web source
with a passage) and [2] (a file source with page index 1): the button and its three-item
menu appeared; "Copy as Markdown with footnotes" put the reply with [^1] / [^2] and the two
definitions (URL, page 2, passages) on the clipboard; "Copy sources as BibTeX" put two
@misc entries with the accessed date on it. Not covered: a reply mixing server and local
document citations (the numbering continues after the server ones - self-test only), the
plain list item, and the palette commands.

2026-10-25 118 (local TTS fallback): `src/core/tts.*` (Prefs <-> extra["tts"]: mode
auto/server/local, Mac voice, rate, pitch, skipCode, serverChunkChars; `choose` - auto
takes the server until it fails once, then this Mac; `speakable` turns markdown into
hearable text - fenced code announced or unwrapped, links to their text, bare URLs to
"link to <host>", headings/bullets/quotes/table markup, emphasis and [n]/[^n] markers
dropped; `chunk` packs sentences into pieces; `appleRate` maps 0.5x-2x onto
AVSpeechUtterance's scale; `pickVoice` prefers an installed preference, then an
enhanced/premium voice for the language, else the system voice; `Queue` holds pieces
per message with status text). `src/platform.mm` gained AVSpeechSynthesizer
(speechVoices, speechLanguage, speakLocal, stopSpeaking, speaking; stopAudio /
audioPlaying now cover speech too, so voice mode waits for it). `src/local_tts.cpp`:
the queue tick in frame(), server request with fallback and a one-time toast, the
per-message Read aloud / Stop button, the "Reading aloud, N more queued" status line,
the Settings "VOICE AND READ-ALOUD" card (engine, server voice, Mac voice, rate, pitch,
skip code, Test/Stop) and palette commands; "Read replies aloud" and voice mode use the
same path. `--ttstest` runs 13 offline checks. Verified on a scratch server (no TTS
configured, speech endpoint 404) with a seeded markdown reply: the Read aloud button
toasted "The server has no text-to-speech - reading with this Mac's voice", the status
line showed "Reading aloud", the button became Stop, the log recorded the local voice
(the system voice after a fix - the first cut picked "Albert", a novelty voice, because
every plain en-US voice ranks the same), and status and button reverted when speech
ended. Not covered: the server-TTS path itself (no engine on the scratch server), a
multi-part reply, the Mac-voice picker and rate/pitch sliders beyond rendering, and
the palette commands.

2026-10-25 134 (traffic inspector): `src/core/traffic.*` (Entry; thread-safe ring `Log`
of the newest 1000 with pause and a dropped count; `redact` masks JSON secrets of any
case, bearer values, sk- keys and form fields; `sioEvent` names Engine.IO/Socket.IO
packets; `cap` keeps 16 KB on a UTF-8 boundary; `filter`, `summarize` (failures,
p50/p95, bytes, slowest), `waterfall` fractions over the entries' window, `har` 1.2
with query strings split, redacted bodies, timings and one websocket entry carrying
Chrome's _webSocketMessages, `curlCommand`, `isoTime`/`clockTime`, `pretty`).
`src/core/http.*`: Transfer gained startedAtMs (wall clock), contentType, requestBody
and responseBody (capped; streams captured through the chunk sink; "<multipart: ...>"
for uploads), streamed; observers are now a list (`addObserver`; `setObserver` replaces
all; the audit log moved to addObserver) and `setCaptureBodies` switches capture.
`src/core/socketio.*`: `sio::setObserver` sees every packet sent or received,
`Client::url()`. `src/traffic_pane.cpp`: observers into the log at init, the Tools
"Traffic" tab (Pause/Clear, kind, heartbeats, follow, capture bodies, filter, Export
HAR..., Copy HAR, summary, table with waterfall column, detail with Replay / Open in API
console / Copy as curl / Copy request / Copy response and read-only bodies);
`App::consoleLoad` prefills the API console. `--traffictest` runs 12 offline checks.
Verified on a scratch server: the tab listed the sign-in, config, chats, folders and
models requests and the socket open/connect/user-join packets with waterfall bars;
Copy HAR put a HAR 1.2 with 12 requests and a websocket entry of 4 messages on the
clipboard, the connect frame's token masked; selecting /api/models showed its detail,
Replay produced a new row and "Replayed: HTTP 200", Copy as curl produced the command
with the $OWUI_TOKEN placeholder, Open in API console switched to the console with GET
/api/models filled in. Not covered: Export HAR... (save panel), Pause/filter/heartbeat
controls beyond rendering, a streamed completion's body capture, and the Capture
bodies switch. Note: during this run two macOS dialogs (a keychain access prompt for
com.davidcochran.owui-desktop and an Automation prompt) stayed on screen after the app
exited; no code path from the Tools pane reads the keychain and OWUI_NO_KEYCHAIN=1 was
set, so their origin is unconfirmed - they were left for the user.

2026-10-25 104 (retrieval inspector): `src/core/retrieval.*` (Chunk; `fromSources`
flattens Open WebUI's sources - document/metadata/distances aligned by index, names from
source or metadata, pages 1-based, file ids, URLs, relevance clamped to 0..1 or -1 when
absent, stable ids; `ranked`; `Marks` pin/exclude sets that are mutually exclusive and
round-trip JSON; `stats`; `passagesForReask` - pinned wins, else everything not excluded,
nothing marked = nothing; `reaskPrompt` quotes the passages numbered with page and asks
for an answer from those alone; `copyText`, `relevanceLabel`).
`src/retrieval_inspector.cpp`: a layers button under any reply with chunks opens a
440 px side column (ranked chunks with relevance bars, More/Less, Pin, Exclude, Open via
openCitation, Copy passages, Clear marks, Re-ask); marks live in
chat.raw["desktop_retrieval"][messageId] and save with the chat; `reaskWithPassages`
sends the parent question wrapped by reaskPrompt with `retrievalBypassNext_` set, which
sendMessage turns into the new assistant ids in `retrievalBypass_`, and
buildCompletionPayload drops `files` for those ids. Palette: "Retrieval inspector for the
last reply". `--retrievaltest` runs 9 offline checks. Verified on a scratch server with a
seeded reply carrying four chunks from two sources (scores 0.91/0.38/0.62/0.74): the
column listed them ranked 91/74/62/38 with bars, mean 66% / best 91%; Pin and Exclude
toggled, highlighted and were saved on the server's chat record; Re-ask sent the
question with the one pinned passage quoted, and the traffic inspector's HAR showed the
POST /api/chat/completions body without a `files` key. Not covered: Open (document
view), Copy passages, Clear marks, the palette command, and the re-ask reply itself: the seeded chat's
model (qwen2.5-coder:3b-base) answered with the server error "does not support tools"
(Ollama refused the server's tool-enabled request - a model limitation, not the
re-ask path), so a real answer from pinned passages is still to be seen.

2026-10-25 99 (playgrounds): `src/core/playground.*` (`kindFor` from the language tag or
the code's shape - SELECT/WITH/..., /.../flags, a leading jq path; `runRegex` with
std::regex ECMAScript, slash-form flags i/m, every match with span and groups, optional
replacement, errors explained incl. the lookbehind gap; `parseCsv` with quotes and
tab/semicolon detection; `identifier`; `runSql` in an in-memory SQLite that loads the CSV
as a typed table (INTEGER/REAL/TEXT inferred, empty cells NULL) or opens a SQLite file
read-only, runs every statement, caps rows, reports schema/changes/ms; `jqPath`,
`jqAvailable`, `jqArgv`; `tableMarkdown`, `tableCsv`, `matchesText`; sample inputs and
code per kind). `src/playground_pane.cpp`: "Try it" under matching code blocks (before
Run), a modal with kind combo, Load CSV/SQLite/JSON/text, Sample data, Run (Cmd+Enter),
code and input editors (replacement field for regex, -r for jq), results as tables or
mono text, Copy result / Copy as Markdown / Ask about the result (fills the composer) /
Close; jq runs through native::runProcess sandboxed with the input written to
support/playground/input.json. Palette: "Playground (try a regex, SQL or jq locally)".
`--playgroundtest` runs 15 offline checks. Verified on a scratch server with a seeded
reply holding sql, regex and jq blocks: every block showed "Try it"; the SQL one opened
the modal with the sample CSV and Run produced 3 rows in 0.6 ms with the inferred schema
line, Copy as Markdown put the table on the clipboard; switching the kind to Regex and
running listed 3 matches with spans and $1-$3; jq produced the two filtered objects
from the sandboxed jq. Not covered: Load file (open panel), a SQLite file in the GUI
(self-test only), the replacement field, Ask about the result, and the palette command.
Also in this commit: `core/keychain.mm` logs every load/save/erase under OWUI_DEBUG (and
the call stack with OWUI_KEYCHAIN_TRACE=1) - added while chasing the keychain prompt seen
in test runs; an instrumented run made no keychain call at all, so that prompt was a
stale one from an earlier instance.

2026-10-25 149 (model evaluation): `src/core/eval.*` (Prompt/PromptSet <-> JSON with a
five-prompt `defaultSet`; Score <-> JSON and JSONL with `effectiveGrade` where thumbs
override the judge; `judgeSystem`/`judgeUser` asking for a JSON verdict with the rubric;
`parseJudge` reads JSON anywhere - fenced, after a <think> block, string scores clamped -
or "7/10" / "Score: 7" prose; `stripThinking`; `latest` = newest score per prompt x model
for a set; `trends` = per-run means per model, best mean first, with history, last and
mean seconds; `csv`). `src/eval_pane.cpp`: Tools "Evaluate" tab - set combo, add/delete
set, prompt list with remove and an add-prompt form (name, text, rubric), model
checkboxes, judge checkbox + combo (defaults to the auto-route small model or the first
model), Run the set (a worker runs prompt x model non-streaming completions then the
judge, appending each score to support/eval/scores.jsonl; Stop cancels), Export/Copy
CSV, the latest-grades matrix (coloured grades with seconds and thumbs marks, reason as
tooltip, click for the answer with Good/Bad thumbs, copy), and per-model trend bars.
Palette: "Model evaluation". `--evaltest` runs 12 offline checks. Verified on a scratch
server with a seeded two-prompt set and qwen3:8b as both model and judge: the run
progressed "Running 0 / 2", finished with a toast, the matrix showed 10/10 at 28.3 s and
10/10 at 2.8 s (judge reasons recorded in scores.jsonl), and the trend line read
"mean 10.0 - last 10.0 - 1 run" with its bar. Not covered: thumbs overrides and the cell
detail beyond rendering, add/delete set and prompt forms, Export CSV..., Stop, and runs
across several models. Note: the Tools tab bar now overflows at 1380 px and scrolls.

2026-10-25 148 (saved answers library): `src/core/library.*` (Entry <-> JSON; `add`
refreshes an already-saved message in place keeping its note and merging tags; `remove`,
`find`; `parseTags` with comma/space/#, lower-case, unique; `search` - every word in
text, note, title or model, "#tag" words against tags only (prefix ok), newest first;
`tagCounts`; `snippet` on UTF-8 boundaries; `markdown` export with a heading per entry,
meta line, note as a quote). `src/library_pane.cpp`: Pane::Library in the rail (archive
icon) with search, tag chips, entry cards (snippet, note, tags, Open chat which focuses
the message, Copy, Add/Edit note or tags inline, Remove), Export Markdown... and Copy
Markdown in the header; the archive button under each reply toggles saving (accent
when saved); palette "Save the last reply to the library" and "Go to Library".
`--librarytest` runs 9 offline checks. Verified on a scratch server: the archive button
under the seeded reply toasted "Saved to the library (1 answer)" and turned accent with
a "Remove from the library" tooltip, library.json held the entry, the Library pane
listed it with its snippet and buttons, the note/tags editor opened, and Copy Markdown
put the export on the clipboard, and Open chat returned to the chat with the saved reply's
archive button lit. Not covered: typing a note or tags, tag chips and the #tag search
(self-test only), Export Markdown... (save panel).

2026-10-25 147 (smart folders and tags): `src/core/smartfolders.*` (Rule <-> JSON with
normalized tags and default names, rules without a folder or tag dropped; `needsChatBody`;
`describe`; `matches` on title / first message / model / tag / older- or newer-than days
(unknown age never matches an age rule); `plan` skips chats already filed or tagged, the
first moving rule wins the move and every matching rule may add its tag; `summary`;
`tagSystemPrompt` / `tagUserPrompt` asking the small model for a JSON array of 1-3 tags;
`normalizeTag`; `parseTags` from a JSON array or a "Tags: a, b" line, think blocks and
generic words dropped). `src/smart_folders.cpp`: Settings "SMART FOLDERS AND TAGS" card
(rule list with enable/Delete, add-rule form, auto-apply and auto-tag switches, Apply
rules now, Auto-tag untagged chats now, status line); `applySmartFolders` fetches chat
bodies only when a rule needs models or text (150 max), creates missing folders, moves
and tags on the server, then refreshes the list; `autoTagChats` tags up to 15 untagged
chats with the auto-routing small model; `tickSmartFolders` runs either every 10
minutes when switched on. Palette: apply, auto-tag, settings. `--foldertest` runs 10
offline checks. Verified on a scratch server with three seeded chats and two seeded
rules (title contains "lighthouse" -> Maintenance + #lighthouse; model contains "base"
-> Base models + #base-model): Apply rules now created both folders, moved Lighthouse
notes and Winch greasing into them and tagged each (confirmed through the chat and
folder endpoints); Auto-tag untagged chats now with qwen3:8b tagged the remaining chat
"Order summaries" with order-summarization, sql and jq, the status line read "tagged 1
chat(s)". Not covered: the add-rule form (typing), the 10-minute automatic pass, the
age and first-message conditions beyond the self-test, and the palette commands.

2026-10-25 102 (hybrid local search): `src/core/hybrid.*` (`ensureFts` creates an FTS5
table chunks_fts (unicode61, '_' and '.' kept in tokens) with insert/delete/update
triggers on the app's chunks table and backfills rows indexed before it existed;
`tokens` keeps identifiers whole; `ftsQuery` = quoted prefix terms OR-ed; `lexicalSearch`
by bm25(); `fuse` = reciprocal rank fusion (1/(60+rank)) of the vector and text lists
plus a first-place-sized bonus when the chunk holds every query token; `sourceName`).
`src/knowledge_tools.cpp`: IndexDb calls ensureFts on open; `searchIndex` takes a wide
vector top list and a BM25 list and fuses them (extra["hybridSearch"], default on),
falls back to text matches alone when the embedder is unreachable and says so in the
error line; hits carry source and exact flags shown beside the score; a Hybrid checkbox
under the search box. `--hybridtest` runs 9 offline checks (an in-memory chunks table
with the triggers). Verified on a scratch home with a three-file corpus (a .py defining
recalibrate_foghorn_compressor and two .md notes): Index reported 3 files / 3 chunks and
local-index.db held 3 chunks and 3 FTS rows; searching "recalibrate_foghorn_compressor"
listed foghorn.py first as "both - exact match" (0.049) with the two notes after it as
"vector" (0.016). Not covered: the embedder-down fallback in the GUI, turning Hybrid off,
and a backfill over a pre-existing index (self-test only).

2026-10-25 126 (shortcut editor): `src/core/keymap.*` (Chord parse/format/pretty with
word and ⌘⇧⌥⌃ forms and key aliases; 28 defaults across General / Chat / View / Panes
with a `menu` flag for the ones a native menu item owns; overrides as {id: chord} with
unbinding; `conflicts`; `menuEquivalent` -> NSMenuItem key + modifier bits incl. specials
and F-keys; the Vim state machine `vimStep` with gg within a second, G, j/k, /, y, i/a/o,
r/p/s/e, Escape; `cheatSheet`). `src/shortcut_editor.cpp`: keymap loaded from
extra["keymap"]; `shortcutsApplyMenus` pushes key equivalents into the native menu items
(main.mm now tags every item by shortcut id, `native::setMenuShortcut` /
`suspendMenuShortcuts`); `shortcutPressed` reads modifiers the way ImGui's macOS mode
reports them (io.KeyCtrl is Cmd); handleShortcuts rewritten over the keymap (pane jumps
incl. Library and Tools, new chat, sidebar, refresh, stop, cheat sheet); `handleVimKeys`
hooked into the focused-message keys; Settings "KEYBOARD SHORTCUTS" card with click-to-
capture (menu equivalents suspended while capturing, a modifier required unless an F-key,
Escape cancels, Backspace unbinds), conflicts in red with the other action on hover,
per-row Reset and Reset all, Vim keys switch, Cheat sheet button; the ⌘/ cheat-sheet
modal with Copy / Edit shortcuts. `--keymaptest` runs 10 offline checks. Verified on a
scratch server: the card listed every binding; capturing ⌘⇧B for "Toggle the sidebar"
and ⌘N for "Refresh the chat list" saved {"view.sidebar": "shift+cmd+b", "chat.refresh":
"cmd+n"}, both ⌘N rows turned red with "Also bound to: New chat" on hover and Reset
appeared; in the chat pane the rebound ⌘⇧B hid the sidebar (through the updated menu
item) and ⌘/ opened the cheat sheet showing the new binding. Two defects fixed on the
way: a bare key could be captured (it would have stolen typing), and the modifier
mapping used io.KeySuper where ImGui's macOS mode reports Cmd as KeyCtrl. Not covered:
Vim keys in the GUI (self-test only), unbinding, Reset all, and Ctrl/Option chords on
menu items.

2026-10-25 68 (theme editor): `src/core/themeedit.*` (hex parse/format incl. #rgb and
alpha; lighten / hoverFor; the fifteen palette slots with labels; overrides per theme
name in extra["themeOverrides"] with canonical hex and unknown slots ignored;
serverAccent / withServerAccent in extra["serverAccents"]; `importTheme` for VS Code
themes - JSONC comments and trailing commas stripped, ~15 colour keys mapped with
fallbacks, hover derived - and Base16 schemes in YAML or JSON; `exportJson`).
`src/ui/theme.*`: `setOverrides` lays slot overrides over the base palette inside
apply(). `src/theme_editor.cpp`: `effectiveThemeName` ("system" resolved),
`themeOverridesForCurrent` (theme overrides + the active server's accent and derived
hover), `applyTheme` now sets them before theme::apply and runs once at init; Settings
"THEME EDITOR" card under Appearance - a colour well per slot applying as you drag, hex
beside it, Reset per slot, Reset all, Import VS Code / Base16 theme..., Copy as JSON, and
a Server accent well with Clear. Palette: "Theme editor". `--themetest` runs 11 offline
checks. Verified on a scratch home with seeded dark overrides (accent #e0453c, hover,
bg #101820, panel #182230): the app launched with the red accent and navy background,
the card showed the four overridden slots with Reset; Reset on Accent restored the blue
accent live and removed only that key from settings; Copy as JSON put the three remaining
overrides on the clipboard; Reset all restored the default look and removed
themeOverrides from settings. Not covered: dragging a colour well, the Import open panel
(importers self-test only), the server accent well, light and midnight themes.

2026-10-25 145 (onboarding): `src/core/onboarding.*` (eight tour steps with anchors;
State <-> extra["onboarding"] (tourDone, tourStep clamped, lastSeenVersion); a built-in
release list (1.1 with this round's features, 1.0); `compareVersion`;
`pendingReleases(lastSeen, current)`; `whatsNewText`). `src/onboarding_pane.cpp`:
`tickOnboarding` on the first signed-in frame starts the tour when it was never finished
(resuming at the saved step) or opens the what's-new sheet when release notes newer than
the last seen are pending; the tour is a modal card (its dim covers the window) placed
beside an accent outline drawn around the step's region (rail, chat list, header,
composer, Tools and Settings rail icons) with Back / Next / Skip and Escape; finishing
saves tourDone and hands over to what's new; the what's-new modal lists the pending
releases with Got it (saves lastSeenVersion), Copy and Take the tour; Settings "GETTING
STARTED" card with Take the tour, What's new and the cheat sheet; palette commands. The
bundle version is now 1.1. `--onboardtest` runs 7 offline checks. Verified on a scratch
home with no onboarding state: the tour opened on sign-in (step 1 of 8 centred), Next
moved to "The rail" with the accent outline around the rail and the card beside it, Skip
tour saved tourDone and opened the what's-new sheet for 1.1 (lastSeenVersion was empty),
and Got it recorded 1.1. A first cut drew the shade on the foreground list, which dimmed
the card too - replaced by the modal. Not covered: Back, the remaining six steps'
placement, resuming a half-done tour after a relaunch, and the Settings card buttons.

2026-10-25 63 (reading mode): `src/core/readingmode.*` (Prefs <-> extra["readingMode"]:
enabled, measure 480-1000 px, text scale 1.0-1.6, line spacing 1.0-2.0, hide sidebar,
controls on hover; `contentWidth`, `itemSpacingY`, `showControls`, `summary`).
`src/reading_mode.cpp`: `setReadingMode` tucks the sidebar away and restores it, keeps
the preference and toasts; `noteMessageRect` / `messageControlsVisible` remember each
message block's rectangle from the last frame so the action row, steering chips,
follow-ups and code-block buttons draw only when the pointer rests on that message (or it
is focused); the Settings "READING MODE" card with sliders and switches. Chat pane: the
transcript and composer measure come from readingmode::contentWidth, each message block
is wrapped in a scaled font and wider item spacing while reading, a book button in the
header toggles the mode, Cmd+Shift+R is a new keymap default ("view.reading"), and the
palette has the toggle. `--readingtest` runs 6 offline checks. Verified on a scratch
server with the seeded code-block chat: Cmd+Shift+R hid the sidebar, narrowed the
transcript to 680 px with larger text, lit the header button, hid the chips and action
row (and, after a second build, the code-block button rows) and saved the preference;
hovering the reply brought the chips and buttons back; Cmd+Shift+R again restored the
sidebar and the normal layout with an "off" toast. Not covered: the Settings sliders
beyond rendering, the "controls on hover" switch off, and focused-message gating.

2026-10-25 64 (virtualised transcript): `src/core/virtualize.*` (`Heights` remembers each
message's laid-out height per content width with the frame it was seen, `known` answers
-1 for unknown or other-width entries, `prune` drops stale ones; `shouldSkip` for a block
wholly outside the view plus a margin; `Stats` / `statsLine`). Chat pane: each frame the
transcript loop replaces any message whose height is known and which lies more than a
window-height outside the view with a spacer of that height (streaming, editing,
focused, scroll-target, selected, compare-mode and search-hit messages are always
drawn); heights are recorded right after each drawn block; extra["virtualTranscript"]
(default on) with a switch in Settings > Chat; with OWUI_DEBUG a "transcript: drew N of M
messages in X ms" line every two seconds. `--virtualtest` runs 6 offline checks.
Measured on a scratch server with a seeded 1500-message chat (every fifth reply holds a
code block): virtualisation off laid out all 1500 messages in 795-835 ms per frame; on,
12-13 of 1500 in 7-15 ms, and after wheel-scrolling hundreds of messages up the view the
numbers stayed at 7 ms with the transcript rendering normally (headers, bullets, code
blocks, action rows). Lazy image decode falls out of the same culling (images render
only when their message does); incremental markdown parsing was not done. Not covered:
jump-to-message into a culled region beyond the self-test's reasoning, and chats whose
message heights change with the window width (re-measured on the first frame they are
drawn at the new width).

2026-10-25 122 (menu-bar-only mode): `src/core/menubar.*` (Prefs <-> extra["menuBarOnly"]
/ extra["openAtLogin"], the rules that the extra is required and the app survives its
last window, SMAppService status words). `src/native.{h,mm}`: `setMenuBarOnly` switches
the activation policy to Accessory (no Dock icon, no app-switcher entry) and back, keeps
the window alive across a close (releasedWhenClosed = NO) and installs a local key-event
monitor that feeds Cmd chords to the main menu, since macOS stops routing key
equivalents for accessory apps; `setLoginItem` / `loginItemStatus` through SMAppService;
`showMainWindow` now finds the chat window (skipping the offscreen renderer) and the
extra's "Show Open WebUI" callback actually calls it (it was a no-op). main.mm keeps the
app running after the last window closes in this mode. `src/menu_bar_only.cpp`: the two
switches in Settings > Mac integration ("Show in the menu bar" is forced on and greyed
while menu-bar-only), the login-item status line, and `setMenuBarOnly` with its toast;
startup applies the saved mode; palette toggle. `--menubartest` runs 4 offline checks.
Verified on a scratch server: switching the mode on made the process background-only
(System Events) with the window still up; closing the window left the process alive with
no windows; the status item's menu showed Quick Ask / New Chat / Show Open WebUI / Quit,
and Show Open WebUI brought the window back (after the callback fix); a relaunch came up
in accessory mode straight away; Cmd+K opened the palette from the composer (after the
key-equivalent monitor fix) and the palette command switched the mode off again. Not
covered: "Open at login" - registering a login item is a persistence change on this Mac
that the test harness refused, so the SMAppService path has only the status read
exercised ("login item not found" for an unregistered dev build). A later check
(next entry) showed app-handled chords such as Cmd+Shift+R do fire with the native
composer focused in regular mode; what failed during this run was the accessory-mode
window before the key-equivalent fix, so no general limitation stands.

2026-10-25 105 (local reranker): `src/core/rerank.*` (Prefs <-> extra["localRerank"]:
enabled, keep 1-12, minScore 0-10; `systemPrompt` / `userPrompt` listing passages by id
with titles and a per-passage cut; `parse` reads a JSON array of {id, score} objects, a
bare positional list only when it has one score per passage, or "id: score" lines, with
think blocks ignored, unknown ids dropped and scores clamped; `order` by score above the
floor up to keep; `summary`). `src/local_rerank.cpp`: `rerankLocalHits` (blocking, one
completion on the auto-routing small model or the selected model; keeps the search order
on any failure), `rerankScoreFor` / `rerankSummary` for the tab, `drawRerankControls`.
Hooks: addLocalIndexHits fetches 10 hits instead of 4 and reranks them before they go in
front of the prompt; the Local index tab search reranks its 10 hits and shows "reranked
N/10" beside each plus a "Last rerank" line. `--reranktest` runs 8 offline checks.
Verified on a scratch home with the three-file corpus already indexed: with the switch on,
searching "when may visitors climb the tower" logged "3 scored: 10, 7, 0 by qwen3:8b in
6412 ms; kept 2 of 3" and the tab listed the two kept hits with their scores (visitors.md
10/10 first) and the summary line. Two parser defects fixed on the way: the line form
swallowed newlines, and "[2]" in prose was read as a positional list. Not covered: the
per-message prompt path beyond the shared function (the tab exercised the same call),
Keep / Drop below sliders beyond rendering, and behaviour with no small model set.

2026-10-25 143 (accessibility options): `src/core/accessibility.*` (Prefs <->
extra["accessibility"]: reducedMotion, highContrast, uiFont + uiFontBold paths;
`fontChoices` from folder listings - .ttf/.otf only, bold/italic variants dropped, bold
partners paired, dyslexia- and low-vision-friendly faces (OpenDyslexic, Atkinson
Hyperlegible, Lexend, Verdana, Tahoma, Trebuchet MS, Comic Sans, Arial Rounded) marked
recommended and listed first; `displayName`; `highContrastOverrides(dark)` full slot
sets). `theme::setFontOverride` puts the chosen face (and bold partner) in front of the
system candidates on the next loadFonts; `widgets::setReducedMotion` stops the toast fade
and the spinner rotation; the theme editor's `themeOverridesForCurrent` lays the
high-contrast set over the theme overrides (server accent still last); main.mm rebuilds
the font atlas between frames when `fontsReloadRequested` is set; `applyAccessibility`
runs at init. Settings "ACCESSIBILITY" card after Appearance: the two switches, the UI
font combo (System font + choices, Rescan), and a UI scale slider (0.8-1.6x) applied
live through FontScaleMain and saved as uiScale; palette entry. `--a11ytest` runs 5
offline checks. Verified on a scratch home: the card listed 181 fonts; High contrast on
turned the window black with white text, grey-white borders and a lighter accent (the
theme editor read #000000 / #7fb0ff) and off restored the theme; the font combo listed
Comic Sans MS, Tahoma, Trebuchet MS and Verdana first as recommended, choosing Tahoma
saved the regular and bold paths and the interface re-rendered in Tahoma after the
between-frames reload. Not covered: reduced motion visually (self-test and code review
only), the UI scale slider beyond rendering, and a user-installed OpenDyslexic.

2026-10-25 75 (multi-server pool): `src/core/pool.*` (Prefs <-> extra["backendPool"]:
enabled, members = backend ids deduped, strategy least-loaded | round-robin | first-alive,
checkEverySec clamped 5-600, failover; Health {alive, running, inFlight, latencyMs,
checkedAt, error}; `runningFrom` counts Ollama /api/ps models; `order`/`pick` drop dead
members, sort by inFlight*10 + running + latency/1000, rotate, or keep the configured
order - unknown health counts as alive; `describe`). `src/backend_pool.cpp`: a virtual
"pool" backend appended to `backendList()` while enabled with members ("Pool (N
servers)"), `backendTransport` routes it to `poolTransport()` which tries `pool::order`
in turn, counts in-flight per member, and treats only real chat chunks (message /
choices / delta / content_block / response / done) as output - the JSON body and the
{"error"} line streamChat forwards on an HTTP failure are held back so the next member
can be tried, replayed only when the member is kept; a member that fails before output
is marked down and logged "pool: <id> failed before any output (...) - trying the next
member". `tickBackendPool()` in frame() probes every member on a worker every
checkEverySec (GET /api/ps, /models or /v1/models with the saved key; alive = any
answer below 500), `poolModelUnion()` feeds the pool's model cache, Settings "BACKEND
POOL" card after Direct backends: enable, member checkboxes from the backend list,
routing combo, interval slider, fail-over switch, Check now, "last request went to
<name>" and the health table coloured success/danger; two palette entries. `--pooltest`
runs 7 offline checks. Verified on a scratch home with three members: a dead Ollama at
localhost:11435, a "half-dead" one pointed at the scratch Open WebUI on :3001 (answers
GET /api/ps 200 but POST /api/chat 405) and the real Ollama on :11434. The table showed
the dead box "down: Couldn't connect to server" and the others alive with latencies; a
private chat's backend picker listed "Pool (3 servers)" and the union model list; with
first-alive routing a message went to the half-dead member first, the log read "pool:
half01 failed before any output (HTTP 405) - trying the next member", and qwen3:8b on
the real Ollama answered "pong" (first token 4.8 s); the card then read "last request
went to Ollama" and Ollama "1 model loaded". Two defects fixed on the way: the wrapper
first counted the 405 body line as output, and then only held lines with an "error"
key. Known limit: a member that answers the probe but rejects chats is re-marked alive
at the next probe (the probe is a GET, not a chat), so it is retried first and fails
over again each time - the failover cost, not an outage. Not covered: round-robin and
least-loaded beyond the self-test, API-keyed members, and OpenAI-kind members in the
pool (the union path ran only against Ollama).

2026-10-25 80 (grammar / tool-choice controls): `src/core/outputctl.*` (Controls <->
chat.raw["outputControls"]: mode free | json | grammar, grammar text, toolChoice auto |
none | required | tool:<name>; `validateGrammar` - rules `name ::= body` with
continuation lines and # comments, root required, references resolved, quotes / classes
/ parens balanced per rule; `openaiToolChoice` / `anthropicToolChoice` wire shapes;
`nudge` - the system line for backends without a tool_choice and for JSON mode;
`applyToOllamaPayload`; `summary`). `LoopConfig.extra` carries top-level fields into
every tool-loop request, tool_choice only on the first round so a forced call is
followed by a free answer; `openaiRequest` passes `grammar` and `tool_choice` through,
`anthropicRequest` maps tool_choice (required -> any, forced -> tool), the Ollama branch
of `streamChat` strips both. `src/output_controls.cpp`: `outputControls` /
`setOutputControls`, `applyOutputControls(LoopConfig&)` (format json, extra fields,
tools cleared for none, nudge appended to the system prefix) called in the private-chat
and MCP tool streams, the server payload gets response_format json_object when no
schema is on, grammar, tool_choice (tool_ids dropped for none) and the system line, the
"Output and tools" block at the end of the sampling panel (radios, grammar box with the
live verdict, tool-choice combo listing the connected MCP tools, notes on where each
field is honoured; disabled while structured output's schema is on), gear highlight
through `hasSamplingParams`, a palette entry that opens the panel. `--outputtest` runs
14 offline checks (the backends self-test still passes). Verified on a scratch Open
WebUI 0.12.3 with qwen3:8b: choosing GBNF and typing `root ::= answer` showed "rule
`root` refers to `answer`, which is not defined"; adding `answer ::= "yes" | "no"`
showed "Grammar ok: 2 rules" and the summary "GBNF (2 rules)"; switching to JSON object
and asking for the moon's name, radius and three facts through the server returned a
bare JSON object (the Traffic tab's copied request body carried
response_format {"type":"json_object"} and the system line; the server's chat record
held outputControls); a new private chat on direct Ollama in JSON mode answered the
France question with a JSON object through the tool loop's `format`. One defect fixed
on the way: a chosen mode with an empty grammar did not count as active and was never
stored, so the radio snapped back. Not covered: tool choice beyond the self-test (no
MCP server on the scratch home), the grammar against a real llama.cpp server, and
Anthropic.

2026-10-25 78 (logprob view): `src/core/logprobs.*` (Prefs <-> extra["logprobs"]: enabled,
top clamped 1-8; Token {token, logprob, alts}; `fromEntries` for Ollama's chunk["logprobs"]
and OpenAI's choice.logprobs.content entries, `fromOpenAIChoice`; compact storage
[[token, logprob, [[alt, logprob]...]]...] via `toJson`/`fromJson`; `confidence` = exp
clamped; `tint` red -> amber -> green; `percent`; `stats` mean / under-50 % count / lowest;
`statsLine`; `tooltip`; `aligned`). Request: `applyLogprobRequest` adds logprobs +
top_logprobs to the server payload and to the tool loop's `extra` (private and MCP
streams); `openaiRequest` passes both through, the Ollama branch keeps them, Anthropic
never sees them. Capture: `LoopHooks.onLogprobs` fires for content chunks only (thinking
chunks skipped), `OpenAIDecoder` carries choice.logprobs.content on its Ollama-shaped
chunks, `api::StreamDelta.logprobs` reads choice.logprobs.content on the SSE and socket
paths, `Stream.logprobs` accumulates the compact entries and the frame flush copies them
to `Message.logprobs` (saved as `desktop_logprobs`, read back on load). View
(`src/logprob_view.cpp`): a layers button in the action row when a reply has logprobs,
`drawTokenFlow` replaces the markdown body with tinted token rectangles wrapped to the
content width (newlines in tokens honoured), a summary line, a note when the tokens no
longer spell the text, a tooltip with the hovered token's percentage and its
alternatives, and a legend; Settings > Chat gets the switch and the alternatives slider;
palette entry. `--logprobtest` runs 12 offline checks (output and backends self-tests
still pass). Verified on a scratch home against Ollama 0.40.10 / qwen3:8b: a private chat
asked why the sky is blue, the action row showed the layers button, the view read
"Confidence: mean 93% over 58 tokens - 3 under 50% - lowest ' atmosphere' 17%" with the
text tinted (amber on "shorter", "effectively", "atmosphere"), hovering ' atmosphere'
showed 17% with alternatives ' upper' 83% and ' ozone' 0%; after a relaunch the private
chat still offered the view with the same numbers. One defect fixed on the way: the
legend went through the printf-style dim text, so "50%." lost its sign. Known limit
found: Open WebUI 0.12.3 relays "logprobs": null for Ollama models even when the request
asks (a direct curl of /api/chat/completions confirmed it), so through the server the
button appears only for OpenAI-style models that return them - not exercised. Not
covered: OpenAI- and Anthropic-kind direct backends beyond the self-test, and very long
replies' draw cost (one rectangle and text call per token).

2026-10-25 100 (file editor): `src/core/fileeditor.*` (per-file snapshot history under
support/editor-history/<fnv-hash>.json: `record` skips a snapshot equal to the newest and
keeps the newest 30, `loadHistory`, `describe`; `languageFor` from the extension or
Makefile/Dockerfile; `lineCount`, `lineOf`, `offsetOfLine`, `findNext` with one wrap and a
caseless mode, `firstDifferingLine`). `src/file_editor.cpp`: an "Editor" window with
tabs per file (close buttons, " *" when modified), toolbar (Save / Cmd+S, Revert to disk,
Find / Cmd+F / Enter = next, go-to-line, History toggle, path - lines - modified), a
48 px line-number gutter driven by the input child's scroll, `InputTextMultiline` with
its text colour pushed transparent and `paintHighlight` re-entering the input's child
window (WindowPadding 0, origin = window pos - scroll) to draw `highlight::tokenize`
spans line by line over it - so the cursor (ImGuiCol_InputTextCursor), selection,
typing and stb_textedit undo/redo stay native; a history column (newest first, label,
line count, age, "= the editor's text" or the first differing line with Go to and
Restore; Restore records "before restore" first). `code_tools.cpp`: `confirmApply`
records "before apply" and "apply" snapshots per written file and refreshes an open
tab afterwards, an Edit button before each file's path in the Apply dialog opens it in
the editor and closes the dialog, `lastAppliedPaths()`; palette: "Open a file in the
editor...", "Open the last applied file in the editor", "Show the editor window".
`--fileeditortest` runs 11 offline checks (the self-test was first named --editortest,
which the editor bridge already owned - renamed). Verified on a scratch home with a
project folder holding hello.py and a seeded chat whose reply carried
```python path=hello.py```: the folder icon opened the Apply dialog, "Apply 1 of 1
change" wrote the file, the palette opened hello.py in the editor with Python
highlighting aligned to the gutter and the history showing "apply - 12 lines" (= the
editor's text) and "before apply - 10 lines - first difference at line 5"; Restore put
the 10-line version in the tab (marked *), typing a comment at the end of line 2 showed
the cursor and the highlighted text, Cmd+S toasted "Saved hello.py", the file on disk
carried the comment and the history read 3 snapshots with "saved in the editor" newest;
reopening the Apply dialog showed Edit before the path (it had been clipped after the
long path - moved). Not covered: files with tabs or very long lines (the overlay
measures with the same font, so widths should match), multi-file tabs beyond one file,
Cmd+Z inside the editor (ImGui's own, not exercised), and a history of more than 30
snapshots beyond the self-test.

2026-10-25 69 (layout presets): `src/core/layouts.*` (Layout {name, sidebar, sidebarWidth
200-480, column none | reference | pins | tree | artifacts, columnBelow, reading, floating,
windowW/H (0 = leave), referenceChatId, builtin}; five `builtins()`; toJson/fromJson with
clamping and column validation; `saved` from extra["layouts"] (nameless dropped, first of
a duplicate name kept), `toSaved`, `all` = built-ins with saved ones replacing a same-named
built-in then appended, `indexOf`, `summary`). `src/layout_presets.cpp`:
`captureLayout` reads sidebarVisible, sidebarWidth, the side-column flags, sideColumnBelow,
reading mode, floating and the main viewport size; `applyLayout` sets them (reference:
the remembered chat or the most recent other chat via `openReferenceChat`; reading via
setReadingMode, float via toggleFloating, size via native::setWindowSize) and toasts the
summary; `applyLayoutByIndex` for the shortcuts; `saveLayoutAs` / `deleteLayout`;
Settings "LAYOUTS" card after Reading mode (table with shortcut badges, summaries, Apply
/ Delete, name field + Save current layout with a "now:" line, and the split switch).
`chat_pane.cpp`: `sideColumnBelow` places the side column under the transcript at 38 % of
the height (min 160 px) with the column drawn at full width. Keymap: view.layout1-4 =
cmd+alt+1..4 handled in handleShortcuts; palette lists every layout with its summary.
`--layouttest` runs 8 offline checks (keymap self-test still 10/10). Verified on a scratch
home with two seeded server chats: the card listed the five built-ins; Apply on Three
columns opened the chat pane with "Winch greasing" as the reference column beside the
transcript and the toast "Layout: Three columns (sidebar 280 - reference beside)";
Cmd+Option+4 moved the reference under the transcript (Split); Cmd+Option+1 hid the
sidebar and switched reading mode on (Zen); typing "Desk review" into the name field and
Enter saved {reference, sidebar, 1380x900} to extra["layouts"] (file checked) and the
palette then listed "Layout: Desk review (sidebar 280 - reference beside - 1380x900)".
Not covered: the window-size part of apply beyond the self-test (the test window was
already that size), float-on-top within a layout, artifacts / tree columns via a layout,
and detachable panes - those remain the existing "open this chat in a new window"
command, not part of a layout.

2026-10-25 117 (generation metadata): `src/core/imagemeta.*` (PNG chunk walk; `readText`
reads tEXt, zTXt and iTXt (zlib for the compressed ones); `writeText` rewrites the file
with iTXt chunks after IHDR, dropping older chunks with the same keywords, CRCs via zlib;
`toText`/`fromText` for our keywords prompt, negative_prompt, model, size, seed, engine,
created, parameters; A1111 `parameters` parsing (prompt, Negative prompt:, the Seed /
Size / Model fields of the settings line); ComfyUI `prompt` graph parsing (KSampler
seed / noise_seed, CheckpointLoaderSimple ckpt_name, EmptyLatentImage size, the two
CLIPTextEncode texts as positive and negative); `parametersLine`; gallery Item index
to/from JSON, `fileNameFor`, `filter` newest-first with favourites-only and a caseless
query, `pngSize`). `src/image_gallery.cpp`: support/images + index.json; `galleryAdd`
writes the bytes with the metadata chunks and indexes them (jpg kept as is);
`galleryRescan` adds files found in the folder (metadata read from PNGs) and drops
entries whose files went; `drawImageGallery` - search, Favourites only, tile size,
Rescan, Open folder, the grid (thumbnails through the image cache keyed file://, star
overlay, selection frame, tooltip with the prompt, ★ / Reuse / Open / x under each) and
the selected image's card (time, size, Reveal in Finder, Copy parameters, prompt,
negative, model, size, seed or "not reported by the backend", engine). `images_pane.cpp`:
after a generation every result's bytes are fetched on the worker and saved with the
form's prompt / negative / model / size, the server's engine, and `seed` /
`revised_prompt` when the result object carries them; `imageReusePrompt` fills the
generator; the gallery section sits under the results; palette entry.
`--imagemetatest` runs 11 offline checks (a 2x1 PNG is assembled in the test and every
chunk CRC re-verified after writing). Verified on a scratch home with no image backend:
three PNGs written into the folder by a script (an Automatic1111 `parameters` chunk, a
ComfyUI `prompt` graph, one without text) were picked up by the first draw's rescan as
"GALLERY (3)" with thumbnails; Reuse was disabled on the plain one; selecting the boat
showed prompt "a red wooden boat on a calm lake at sunrise, soft light", negative
"lowres, text, watermark", model dreamshaper_8, size 256x256; its star wrote
favourite: true to index.json (seeds 31337 and 4242424242 and both models were in the
index); typing "fox" left only the ComfyUI image; Reuse on it filled the prompt, the
negative prompt and the checkpoint name and toasted the seed note. Not covered: the
generation path itself (no image backend on the scratch server - `galleryAdd` and the
chunk writer are exercised by the self-test only), delete, Open / Reveal, and sizes
outside the generator's list (256x256 left the size combo as it was).

2026-10-25 108 (knowledge graph): `src/core/kgraph.*` (`extractEntities`: #tags, `code`
terms, capitalised runs of 1-4 adjacent words with a stop list (incl. weekdays and their
plurals), a lone capitalised sentence-start dropped unless it is an acronym, dedupe by
lower case in order of appearance; `build`: one node per source (memory / note /
knowledge, a memory's cut-down title is never mined since it is a slice of the text),
entity nodes weighted by mentions with the source ids that mention them, minMentions /
maxEntities (most mentioned kept, ties by label) / coOccurrence options, source-entity
edges and entity-entity co-occurrence edges weighted by shared sources; `seedLayout`
(entities on a ring, sources inside); `layoutStep` (repulsion, springs along edges,
pull to the centre, damping, pinned nodes fixed, clamped to the box, returns the
largest move); `neighbours`, `stats`, `toJson`). `src/graph_pane.cpp`: a Tools "Graph"
tab after Local index; `loadKnowledgeGraph` fetches memories, notes (data.content md /
html / string) and every knowledge base's files - Open WebUI 0.12 returns `files: null`
on the base and lists them paged at /api/v1/knowledge/{id}/files, so that is the
fallback - with each file's extracted text from /api/v1/files/{id} data.content; the
canvas runs up to 400 layout steps (4 per frame) until the largest move drops under
0.3, draws edges (co-occurrence thinner; the selected node's links in the accent),
entity circles sized by mentions, source squares coloured by kind, labels on a dim
backing, a legend, hover tooltips, click to select (neighbours lit, the rest dimmed),
drag to move and pin, drag the background to pan; the side column lists the most
mentioned entities, or the selected entity's sources and companions, or the selected
source's text and mentions; Min mentions / Max entities / Links between entities
rebuild; Shake reseeds; Export JSON; Find dims non-matches. `openGraphTab` +
`toolsTabRequest` let the palette land on the tab (the forced selection is held two
frames, since the previously selected tab reports itself selected in the same frame).
`--graphtest` runs 8 offline checks. Verified on a scratch Open WebUI 0.12.3 seeded
with four memories, two notes and a knowledge base holding foghorn.txt: the tab read
"4 memories, 2 notes, 1 knowledge files - 9 entities, 7 sources, 52 links", the graph
drew the entities (Beacon Point 5, Captain Reyes 4, Winch Room 4, Ada Lovelace 3,
Harbour Ferry 3, Foghorn 2, Fresnel Lens 2, #maintenance, #visitors) around the
sources; clicking Captain Reyes lit its links and listed two memories, a note and the
knowledge file plus seven companions; clicking the knowledge file showed its extracted
text and six mentions. Two defects fixed on the way: entities were mined from the
truncated memory titles ("Harbour Ferr", "Winch"), and the palette's tab switch was
overridden by the old tab in the same frame. Not covered: dragging a node and panning
beyond code review, Export JSON, Find, the option sliders live (self-test only), and
very large graphs (the layout is O(n^2) per step).

2026-10-25 120 (sketch input): `src/core/sketch.*` (Doc of Strokes - points, colour,
width, eraser - with a redo stack; `add` clears redo, `undo` / `redo` move whole strokes,
`clear`, `empty` ignores eraser-only docs; `rasterize` at a scale paints each stroke
through a per-stroke max-coverage mask with anti-aliased capsule segments and blends it
once, erasers paint the background; `trim` to the drawn bounds plus a margin (whole
image when blank); `encodePng` - 8-bit RGBA, filter 0, zlib level 6, CRCs via zlib;
`summary`; JSON round-trip). `src/sketch_pane.cpp`: a "Sketch" modal - swatches, Pen /
Eraser, width 1-32, Undo / Redo / Clear with Cmd+Z / Cmd+Shift+Z, the canvas fitted to
the window with live round-cap strokes and a brush outline, an optional question field,
"Trim to the drawing", and Attach, which rasterises at 1.5x, trims with a 36 px margin,
encodes and calls `attachBytes("sketch.png", "image/png", ...)`, fills the composer
with the question when it is empty and toasts the size. A pencil button sits after the
capture button in the composer; palette entry. `--sketchtest` runs 10 offline checks,
the PNG decoded back through stb_image to identical pixels. Verified on a scratch home
against Open WebUI 0.12.3: the pencil opened the canvas; nine real mouse drags drew a
house (black outline and roof, a red door), the counter read "9 strokes, 99 points";
Cmd+Z took it to 8 and Cmd+Shift+Z back to 9; Attach put a 444x535, 6.8 KB thumbnail
above the composer with the question filled in; the sent message showed the image; the
Traffic tab's copied request body carried an image_url part whose base64 decoded to a
valid 444x535 PNG of the house. One defect fixed on the way: edges darkened where
consecutive segments of a stroke overlapped (visible beading on the house's walls) -
the per-stroke coverage mask replaced per-segment blending, a self-test check pins it,
and a second real run (a triangle, sent and decoded from the wire) came out clean. Not
covered: a vision model's answer (none is installed locally; qwen3:8b is text-only), the
eraser and width slider beyond the self-test, and pressure or smoothing (none).

2026-10-25 97 (LSP context): `src/core/lsp.*` - a minimal LSP client: posix_spawn with
three pipes and chdir to the project, Content-Length `Framer`, a reader thread matching
responses by id (requests from the server - workspace/configuration, progress,
registration - answered with null so it never waits), `request` with a timeout,
`notify`, `openFile` (didOpen once per path), initialize / initialized with
hierarchical document symbols and markdown hover, shutdown / exit / SIGKILL fallback.
Pure helpers: `defaultServers` (clangd --background-index=false, pyright-langserver,
typescript-language-server, gopls, rust-analyzer by extension), `findExecutable` (PATH
plus Homebrew, /usr/local, ~/.local, ~/.cargo, ~/go), `languageIdFor`, `pathToUri` /
`uriToPath` (percent-encoding), `flattenDocumentSymbols` (DocumentSymbol trees with the
container chain, SymbolInformation with containerName, clangd's "a::b" out-of-line
names split), `termsFromQuestion` (backticks, a::b, a.b with no space after the dot,
f() of 3+ chars, snake_case, inner-capital camelCase; words after '/' skipped, so
/no_think and path pieces are not searched; capped at 6), `matches` / `bestMatch`
(qualifier respected; the longest range - a body - wins, types and functions first),
`hoverText` (MarkupContent, MarkedString, arrays; capped), `snippet` (inclusive, 40
lines, "... N more lines"), `formatBlock` (relative path:line, budget 9000 chars).
`src/lsp_context.cpp`: one session per server and folder, lookups serialised; per
term, `workspace/symbol` first, else the files that mention the name (regex, defining
lines and implementation files first, up to 3) are opened and their documentSymbol
read; a match whose range is a single line (workspace symbols locate only the name) is
widened from the file's documentSymbol so the body comes along; hover at the selection
start, the source lines read from disk; `addLspContext` inserts the block as the first
system message on both server streaming branches (socket and HTTP) on the worker,
after the local-index hits; the question and folder are captured on the main thread.
Settings "LSP CONTEXT" card before Layouts (switch, symbols at most 1-8, the project
folder, a servers table with the executable found or "not found", a try-a-question box
showing the block and a log line, Stop servers, the last message's log); two palette
entries. `--lsptest` runs 12 checks incl. a live clangd round trip on a temp two-file
project (documentSymbol finds geo::area with its 3-line range; hover carries
`int area(int w, int h)` and the doc comment; clean shutdown). Verified on a scratch
home with this repo as the project folder (build/compile_commands.json exported into
the git-ignored build dir): the card listed /usr/bin/clangd and the others not found;
"How does pool::order rank the members, and what does runPoolSelfTest() check?" gave
"2 symbols in 4034 ms via clangd (298 files)" with pool::order at src/core/pool.cpp:51,
clangd's typed signature and parameters; sending a pool::order question in a chat put
the block first in the request (seen in the Traffic tab's captured body). Two defects
fixed on the way: with clangd warm, workspace/symbol answered with name-only ranges so
only the definition's first line was sent and qwen3:8b guessed ("likely influences") -
after widening the range the block carried the 16-line body and the answer described
round-robin, first-alive and the sort by load correctly; and "/no_think" was searched
as an identifier. Not covered: pyright / typescript / gopls / rust-analyzer (not
installed here), the private-chat and MCP tool-loop paths (the block is added on the
two server paths only), and very large projects (the file walk stops at 8000 entries,
the grep reads at most 2500 files).

2026-10-25 98 (notebook mode): `src/core/notebook.*` - Cell {id, kind code | prompt |
markdown, source, outputs, count, model}, Output {stream / result / error / image /
answer}; `toIpynb` (nbformat 4.5: kernelspec python3, language_info version, cell ids,
source as newline-kept lines; code outputs as stream / execute_result / display_data
image/png / error; a prompt cell as a markdown cell "**Prompt:** ..." plus the answer,
with metadata.owui {kind, prompt, answer, model}); `fromIpynb` (rejects non-notebooks
and nbformat < 4; reads Jupyter-made files - string sources, null counts, raw cells,
missing ids); `promptContext` (In [n] code with outputs truncated at 1500 chars, figures
noted, markdown, earlier Q / A, oldest cells dropped to fit 12000 chars);
`extractPython`; `stripThinking`. Kernel: python3 (Homebrew, /usr/local or /usr/bin)
spawned with `-u -c` and a driver that keeps one namespace, parses each cell with ast
and evals a trailing expression, redirects stdout / stderr, reports SystemExit and
exceptions with tracebacks minus its own frames, ignores SIGINT while idle and turns it
into KeyboardInterrupt while a cell runs, and saves matplotlib figures (MPLBACKEND=Agg)
as base64 PNG; JSON lines both ways, non-protocol stdout (os.write(1, ...)) folded
into the cell's output; `execute` with a timeout that interrupts and reports it;
`restart`; `stop` closes stdin and reaps. `src/notebook_pane.cpp`: a Tools "Notebook"
tab after Graph (palette entry; the forced-tab mechanism now takes "graph" or
"notebook"): title, New, Open (newest first, by title), Import / Export .ipynb, Run all,
Interrupt, Restart kernel and a "Python 3.15.0 - idle / busy / starting / stopped"
state; cells with In [n] / In [*] / In [ ] gutters, multiline editors (mono for code)
where Shift+Enter runs - a char filter drops the newline Shift+Enter would type - and
moves focus to the next cell; Run / Ask / Edit-Done, kind combo, Up, Down, + Code,
+ Prompt, + Text, Delete, Clear output; outputs inline (stdout dim, stderr amber, Out:
values, errors red with a collapsible traceback, figures through the image cache);
prompt answers rendered as markdown with "Insert code below" and the model name; a run
queue drained one cell at a time on a worker (the kernel is a shared_ptr the worker
holds), stopped at the first error; autosave to support/notebooks/*.ipynb after edits
and runs; the kernel starts lazily in the project folder (or home). `--notebooktest`
runs 21 checks (format round trips, Jupyter-made input, context, and a live kernel:
state between cells, NameError traceback without driver frames, None silent, an
interrupt of `while True` in ~0.4 s, a 0.6 s timeout, fd-1 writes, SystemExit, restart,
stop). Verified on a scratch home: palette > Notebook opened a new notebook; renamed
"Circle maths"; a typed four-line cell run with Shift+Enter showed "radius 3" and
"Out: 28.274" as In [1] on Python 3.15.0 with no stray newline; a prompt cell sent to
qwen3:8b came back with code that used the existing r and math; "Insert code below" +
Shift+Enter ran it as In [2] (Area 28.27, Circumference 18.85); `area / (r - 3)` gave
ZeroDivisionError with a 3-line traceback; Restart kernel cleared the counts with a
toast and Run all re-ran 1-3 stopping at the error; the autosaved file read as
nbformat 4.5 with ids, outputs and the prompt metadata; after quitting (no python
process left) and relaunching, the tab reopened the notebook as it was. One defect
fixed before testing: the first draft handed the kernel from the worker through a
reinterpret_cast of a shared_ptr - replaced by a shared_ptr held everywhere. Not
covered: figures (matplotlib is not installed here; the self-test checks the protocol
path only), Import / Export through the file dialogs, Interrupt from the button (the
self-test interrupts the same way), and input() (unsupported: stdin is the protocol).

2026-10-25 67 (text rendering: emoji, wide characters): root cause found first - ImGui was
built with 16-bit ImWchar, so every codepoint above U+FFFF (all emoji, the mathematical
alphanumerics) became U+FFFD before any font was asked; the target now defines
IMGUI_USE_WCHAR32 (all sources recompiled; 11 other UI self-tests re-run and passing).
`src/ui/emoji_font.{h,mm}`: an ImFontLoader backed by Core Text (CTFont
"AppleColorEmoji"): codepoint -> glyph via CTFontGetGlyphsForCharacters with UTF-16
surrogates; one CTFont per baked size x rasterizer density (FontBakedSrcLoaderDataSize);
glyphs drawn with CTFontDrawGlyphs into an RGBA CGBitmapContext, un-premultiplied,
packed with ImFontAtlasPackAddRect and stored with ImFontAtlasBakedSetFontGlyphBitmap
as RGBA32 with Colored = true (so the text tint doesn't grey them); sized at 0.8x the
text size and centred in the line box (Apple Color Emoji glyphs are ~1.3x their point
size). Two sources per face (FontLoaderFlags bit): an early one merged right after the
text font that claims only Emoji_Presentation=Yes characters (the astral emoji blocks
minus text-default ones, and the BMP list: watch, hourglass, umbrella, coffee, zodiac,
check mark, star, ... plus U+2763-2764, which chat always writes with U+FE0F), and a
late last-resort one for any emoji-ish codepoint no text font had. Zero-width: U+200D,
U+FE00-FE0F, tags, U+20E3, the variation-selector supplement and the skin-tone
modifiers (no sequence shaping in ImGui's one-codepoint layout, so the base emoji
shows); regional indicators are drawn as their bold capital letter, tinted like text
(a flag reads "GB"). theme.cpp merge order per face: text font, early emoji, Arial
Unicode (CJK, arrows, box drawing), STIX Two Math (maths alphanumerics), late emoji;
CoreText linked. `--emojitest` (an .mm) builds a headless atlas through theme::loadFonts
and checks 17 things: 32-bit ImWchar, the range helpers, 'A' monochrome from the text
face, the rocket coloured with real colour pixels, U+2705 and U+2B50 from emoji while
U+26A0 stays text, the heart coloured, G as a letter, the skin tone zero-width, CJK from
the fallback, U+1D465 from STIX, VS16 and ZWJ zero-width, emoji inside the line box,
and CalcTextSize counting the emoji but not the selector. Verified on a scratch home
with a seeded chat of emoji, CJK, Korean, maths and an emoji in a code block: the
installed build (16-bit) showed every emoji and maths letter as a `?` box and
`print("done ?")` in the code block; the new build drew the title and sidebar emoji,
the checklist (rocket, bug, red heart, thumbs up, GB, the family's three faces, star,
hourglass, coffee), ⚠ as text, 你好 / こんにちは / 안녕하세요, 𝑥² + 𝑦² = 𝑟² and 𝔽, emoji in
bold, inline code and the code block. Two refinements came from that comparison:
⭐ ⏳ ☕ first came out as monochrome text glyphs (Arial Unicode has them) - fixed with
the early presentation-only source - and 👍🏽 showed a separate brown swatch and 🇬🇧 two
blank boxes - fixed by dropping modifiers and drawing indicator letters. Not covered:
real sequence shaping (ZWJ families, flags, keycaps, skin tones would need a shaper
such as Core Text line layout), FreeType (not installed; Core Text does the colour
work and stb the rest), and Retina density beyond what ImGui already does.

2026-10-25 150 (CI) - partial: build + every self-test on a macOS runner; GUI driving and
screenshot diffs not done. `scripts/selftests.sh`: discovers the `--*test` flags in
src/main.mm (58 today), runs each with HOME set to a throwaway folder under
build/selftest-homes (not the system temp folder - the script sandbox allows writes
there by design, which made scripttest's "writes outside the working folder are denied"
check pass a write it should have caught), OWUI_NO_KEYCHAIN=1 and a watchdog
(default 90 s, CI 180 s); status ok / FAIL / TIMEOUT / NO SUMMARY from the exit code and
the "N passed, M failed" line; first FAIL lines echoed; logs in build/selftest-logs;
a Markdown table to $GITHUB_STEP_SUMMARY; `--only`, `--skip`, `--timeout`; non-zero exit
on any failure. `--selftest` (the integration suite) is skipped unless OWUI_SERVER is
set, and a "selftest-offline" check runs instead: against an unreachable server it must
exit 1. `.github/workflows/owui-desktop.yml`: push to main / pull_request with a path
filter on owui-desktop/** and itself, read-only token, per-ref concurrency, macos-15,
toolchain report, Release configure + build, the runner script, logs uploaded as an
artifact. Found by the first local run and fixed in the same change: `--selftest`
aborted at exit (SIGABRT, exit 134) whenever it returned early - the async worker pool
was a static std::vector<std::thread>, still joinable when static destructors ran, so
std::terminate fired; the pool's state (threads, queues, mutexes) now lives in a heap
object that is never destroyed (explicit shutdown() still joins), so no exit path can
abort. Verified locally: before the fix the installed build exited 134 with an
unreachable server, after it 1; the runner reported 57 of 57 offline self-tests passing
plus the offline check, and with a scratch Open WebUI 0.12.3 (OWUI_SERVER + OWUI_TOKEN)
the integration suite passed 65/65. The first hosted run's result is recorded below
when it is known. Not covered: driving the GUI and diffing screenshots on the runner
(hosted macOS runners have no Docker for a scratch server, and Metal / WindowServer
availability there is unverified), and Homebrew-only tools (pyright etc.) the live
tests skip when absent.

2026-10-25 130 (distribution: crash reporter, DMG, Homebrew cask) - DMG background art not
done. `src/core/crashreport.*`: `install(folder, version, quiet)` creates the folder,
sets an alternate signal stack (so a stack overflow still reports) and SA_SIGINFO
handlers for SIGSEGV / SIGBUS / SIGILL / SIGFPE / SIGABRT / SIGTRAP, plus
std::set_terminate; the handlers use only preallocated buffers and write(2): header
(version, unix time, signal, main / background thread, the faulting address for SEGV /
BUS, the reason), `backtrace_symbols_fd`, and a 40-line ring of recent activity fed by
`note()` - every core::debugLog line now goes there whether or not OWUI_DEBUG is set;
the terminate handler names the uncaught exception's what() or says std::terminate
was called (a joinable thread destroyed, ...). Then the signal is re-raised with the
default action so macOS still writes its .ips; `quiet` (OWUI_CRASH_QUIET) exits
128+signal instead - no .ips, no "quit unexpectedly" dialog - for tests.
`parseAppReport` (image names with spaces, names demangled with __cxa_demangle, handler
and libc++ terminate frames dropped), `parseIps` (header line + JSON body: app_version,
exception.signal, asi reason or termination.indicator, captureTime, the faulting
thread's symbols), `list` (ours *.crash + "<process>-*.ips", newest first),
`newerThan`, `summary` (first meaningful frame), `issueTitle` / `issueBody` (version,
signal, reason, top 12 frames; never the activity lines; home paths as ~) /
`issueUrl` (github.com/<repo>/issues/new, URL-encoded, labelled crash, < 8 KB).
`src/crash_reporter.cpp`: installed in main.mm before anything else on the GUI path;
`tickCrashReporter` (first launch with the reporter records "seen" = now instead of
surfacing old history; later launches open the sheet for reports newer than seen,
ours and macOS's for "Open WebUI"; OWUI_CRASH_TEST=segv|abort|throw crashes on purpose
30 frames in); the sheet - drawn on every screen, sign-in included, since a crash at
start-up leaves you there - with a picker when there are several, the summary in
red (wrapping), the reason, version, source and path, the top 16 frames, the last
activity line, View full report (read-only text), Copy report, Save as..., Show in
Finder, Report on GitHub... (tooltip: nothing is sent until you submit it there) and
Dismiss (marks them seen); opened from Settings it is titled "Crash report" and only
closes. Settings "CRASH REPORTS" card before About with View per report and Open the
crash folder; palette entry; the issue repo is extra["crashIssueRepo"] (default
w5ohr/Dereth). `--crashtest` runs 11 checks: four real crashes in child processes
(the app binary re-run with `--crashchild segv|abort|throw|thread`, quiet) each exit
128+signal with a report naming the crashing function, the signal, the reason (bad
access at 0x0, model list was empty, std::terminate called) and the two notes; the
summary; an .ips shaped like today's real exit crash (the vector<thread> destructor);
the issue text's privacy; the URL; redactHome; list / newerThan. Packaging:
`scripts/make-dmg.sh` stages a copy without extended attributes (a build inside
~/Documents carries Finder info and File Provider tags that strict codesign
verification rejects), verifies it --strict (re-signing ad hoc only if that fails),
builds a UDZO HFS+ image with an Applications link, `hdiutil verify`, mounts it
read-only without Finder, checks the app, the link, the version, a deep signature
check (not --strict: macOS adds com.apple.provenance to hdiutil's copies, which a
read-only image can't shed and Gatekeeper ignores) and runs `--pooltest` from inside
it, then writes the sha256 and `build/dist/open-webui-desktop.rb` from
`packaging/homebrew/open-webui-desktop.rb` (url on a GitHub release tag, Ventura+,
zap paths, a caveat that the build is ad hoc signed until #128). Verified on a scratch
home: first launch set seen; OWUI_CRASH_TEST=throw OWUI_CRASH_QUIET=1 wrote
crash-<time>-<pid>.crash (SIGABRT, "uncaught exception: OWUI_CRASH_TEST asked for an
uncaught exception", the last log lines) and exited with no dialog; the next launch -
on the sign-in screen, no server - showed "Open WebUI quit unexpectedly" with the
summary "SIGABRT ... in app::App::tickCrashReporter()" and a demangled stack
(tickCrashReporter, App::frame, drawInMTKView, MTKView draw, dispatch); View full
report expanded the text; Copy report put the exact 3476-byte file on the clipboard;
Dismiss stored seen and a relaunch raised nothing; signed in, Settings listed both the
test report and today's real .ips (copied in from ~/Library/Logs/DiagnosticReports),
and View on the .ips showed "abort() called" and the macOS stack down to the
vector<thread> destructor. make-dmg.sh produced Open-WebUI-1.1.dmg (4.4 MB) whose
sha256 matches the cask; `ruby -c` passes on the cask. Three fixes on the way: the
first build drew the sheet only once signed in; the backtrace regex choked on the
space in "Open WebUI"; and install() created only the last folder level. Not covered:
the DMG's background picture and icon layout (that needs Finder scripting, which asks
for Automation permission, or a .DS_Store writer), `brew style` / `brew audit` (they
need the cask inside a tap - only Ruby syntax was checked), Report on GitHub and Show
in Finder (not clicked: they open a browser and Finder), and a real crash's .ips
appearing in the sheet on the following launch (the test crashes were quiet by design).

2026-10-25 150 (CI) follow-up: the first complete hosted run (run 40238180544, commit
bffb7548, macos-15) built the app and passed 56 of 57 self-tests plus the offline
check; the one failure was healthtest's GPU check, which assumed IOKit always reports
GPU utilisation on Apple Silicon - a CI runner is a virtual machine whose paravirtual
GPU reports none (Metal itself worked there: the working-set check passed). Fixed in
a5bea3a9: the check stays strict on real hardware and accepts "unknown" (-1) only when
kern.hv_vmm_present says it is a VM. mcptest took 76 s on the runner (6 s here), well
inside the 180 s limit.

2026-10-25 129 (auto-update) - delta updates not done (full DMGs only). `src/core/updater.*`:
`parseAppcast` (Sparkle RSS: items with title, description incl. CDATA,
sparkle:releaseNotesLink, pubDate, sparkle:minimumSystemVersion, and the enclosure's
url / length / sparkle:version / sparkle:shortVersionString - as elements or enclosure
attributes - plus an `owui:sha256` attribute), `problem` (refuses items with no 64-hex
SHA-256, plain http to anything but localhost, or a minimum macOS above this one), `pick`
(newest installable item above the current version, minus skipped ones), `compare`,
`sha256Hex` / `sha256File` (CommonCrypto), `bundleValue` (PlistBuddy), and
`installFromDmg`: mount read-only with -nobrowse, find the .app, require the expected
bundle id and version and a passing `codesign --verify --deep`, ditto it beside the
target without extended attributes, move the current bundle into the backup folder
(rename, falling back to ditto + remove across volumes), rename the new one into place
- putting the old one back if that fails - and detach; `rollback` swaps the backup and
the target through a third name so the version rolled back from becomes the backup;
`backupVersion`. `src/updater_pane.mm`: the feed (extra["updates"].feed, default
https://github.com/w5ohr/Dereth/releases/latest/download/appcast.xml; HTTPS or
localhost only), a quiet daily check 120 frames after start-up (autoCheck, lastCheck)
with a toast when something is found; `installUpdate` refuses App Translocation and an
unwritable parent folder, downloads to support/updates, checks length and SHA-256,
installs into the running bundle's own path with the previous version kept in
support/updates/previous, writes updated.json, and relaunches (/bin/sh "sleep 1; exec
<the app's executable>" with the same environment, then NSApp terminate); a sheet on
the next start - "Updated to X" with the release notes and "Roll back to Y", or "Rolled
back to Y"; the Settings "UPDATES" card (this version and path, feed field, Check once a
day, Check now, last checked, a coloured status line, the offer with title, date, size
and hash prefix, Markdown notes, Install and relaunch, Skip this version, Roll back to
<previous>, and a note on what is checked); palette entry "Check for updates".
`scripts/make-dmg.sh` now also writes build/dist/appcast.xml (title, pubDate, minimum
macOS 13.0, the release notes from packaging/release-notes/<version>.md or
OWUI_RELEASE_NOTES as CDATA, and the enclosure with build, version, length and sha256)
with OWUI_DIST and OWUI_DOWNLOAD_BASE overrides. `--updatetest` runs 16 checks: the
FIPS SHA-256 vector, version compare, a six-item feed (versions from elements and
attributes, CDATA notes, refusals for no hash, plain http and a future macOS, localhost
http allowed, newest pick, skipping), non-feeds rejected, and a real update - this
app's bundle copied as "installed", a 9.9 copy re-signed and put in a real UDZO DMG;
a wrong promised version and a different bundle id are refused with the installed copy
untouched; the install puts 9.9 in place with the old version backed up and nothing
left beside it; the installed 9.9 runs; rollback restores the old version and keeps
9.9; rolling back again restores 9.9; no backup is reported. Verified on a scratch home
with a copy of the app in a scratch folder (/Applications untouched) and a feed
served from this Mac: make-dmg.sh built a 1.2 DMG and appcast from a re-signed 1.2 copy
and Markdown notes; the 1.1 copy's Updates card showed "Version 1.2 is available"
with the date, 4.5 MB, the hash prefix and the rendered notes; Install and relaunch
downloaded it (seen in the feed server's log), the 1.1 process exited and a new one
started, the bundle read 1.2 with 1.1 in support/updates/previous, and the relaunched
app opened on "Updated to 1.2 - Updated from 1.1 to 1.2" with the notes and "Roll back
to 1.1", still signed in; Roll back relaunched into 1.1 with 1.2 kept as the backup, a
"Rolled back to 1.1" sheet, and a bundle whose signature verifies; then, with the
feed's sha256 replaced by zeros, Check now offered 1.2 again (hash 000000...) and
Install downloaded it and refused - "Not updated: the download's SHA-256 does not match
the feed" - the app kept running on 1.1 and the download was deleted. Not covered:
delta updates, an EdDSA signature on the feed (the hash is only as trustworthy as the
feed's HTTPS host until Developer ID signing, #128), translocated or read-only installs
beyond the refusal message, and publishing a real release (the default feed URL has
no appcast yet, so a check there reports the HTTP error).

2026-10-25 114 (audio transcription): the server's /api/v1/audio/transcriptions (Open WebUI
0.12.3, local faster-whisper "base") returns only {"text"} - no timestamps - so the
timing is done here: `src/core/transcript.*` - `segment` (20 ms RMS frames in dB, a
threshold 30 % of the way from the 10th-percentile floor to the 95th-percentile level,
voiced runs joined across pauses under 0.30 s, runs over 25 s cut at their quietest
frame in the second half, segments under 1.5 s merged into a neighbour, 0.15 s padding
with overlapping pads split; no contrast means one span or none), `wav16` / `readWav16`
(16-bit mono PCM WAV), `clock` (m:ss, h:mm:ss, SRT's hh:mm:ss,mmm), `lineAt`, `toSrt`,
`toVtt`, `toMarkdown`, `plainText`, `cleanText` (drops [BLANK_AUDIO]-style tags,
collapses whitespace), `isAudioPath`; `src/core/audiodecode.mm` - `decodeAudio` with
AVAudioFile + AVAudioConverter to 16 kHz mono float (no microphone, no prompt).
`src/transcribe_pane.mm`: a Tools "Transcribe" tab after Notebook (palette entry; the
forced-tab request takes "transcribe"); Open audio... or an audio file dropped while
the tab is showing (the drop handler routes it there instead of attaching it to a
chat); the file is hashed (SHA-256) and a saved transcript in support/transcripts/<hash>.json
opens at once, otherwise it is decoded and segmented on a worker and each segment's WAV
goes to `api_.transcribe` in order, lines filling in as they come back, with Stop and a
"transcribing piece n of m" row; an AVAudioPlayer with Play / Pause, a seek slider and
the time; lines as full-width selectables (click = seek + play, tooltip) with the time in
mono and the text drawn over them, the one being heard highlighted and scrolled into
view; Copy text (Markdown with times), Save .srt / .vtt / .md, Send to chat (the
composer gets the Markdown), Transcribe again. `--transcribetest` runs 15 checks:
synthetic amplitude-modulated bursts over noise (three phrases with 1 s pauses -> three
spans, a 0.15 s breath not splitting, a 0.3 s blip merged, a pause-free minute cut into
pieces of at most 25 s, silence / constant tone), the WAV cut reading back, clock,
lineAt, SRT, VTT, Markdown / plain / cleanText, isAudioPath, and `say` output - a
22 kHz WAV and an AAC .m4a decoding to the same 9.2 s at 16 kHz, its two 1.2 s
[[slnc]] pauses giving three sentences - and an unreadable file reported. Verified on a
scratch Open WebUI 0.12.3: a 16.5 s four-sentence `say` recording (AAC) opened through
the app's Open panel came back as four timed lines - 0:00 "Welcome to the Lighthouse
Keepers Briefing.", 0:03 "The lenses polished every Monday before sunrise.", 0:08 "The
fog horn is tested on the first of each month.", 0:12 "Visitors may climb the tower on
weekends, but never during a storm." (whisper's own mishearings); reopening the file
loaded the saved transcript with no new server requests; clicking the 0:08 line played
from 0:08 and the highlight moved on to 0:12 as it played; Copy text gave the Markdown;
Send to chat filled the composer. One defect fixed on the way: the first draw put each
line's text after its full-width selectable, so only the times showed, and the next
attempt moved the cursor past the last item (an ImGui assert) - the text is now drawn
on the window's draw list over the selectable. Not covered: dropping a file (no drag
source in the harness; the routing is code-reviewed), very long recordings (pieces
go one at a time, so an hour takes as long as the server needs for each), private
chats / offline transcription (no local whisper), and speaker labels.

2026-10-25 144 (localisation) - partial by design: the strings table covers the app's frame
(103 strings), not every label. `src/core/i18n.h`, `i18n_tables.cpp` (rows of English key +
de / es / fr / ja: rail labels, pane titles and subtitles, sidebar, empty chat and its four
suggestions, composer, sign-in, Save, "just now", every Settings / Tools section header, the
language card's own labels), `i18n.mm`: `setLanguage` ("" = the first of NSLocale
preferredLanguages we have, else en), `tr(const char*)` - English returns the pointer it
got; "Label##id" translates the label and keeps the ImGui id; translations cached by
language + key so pointers stay valid; misses fall back to English; "qps" returns
pseudo("[Šéttîñgš]", keeping %s / %d and ids) for every looked-up string; `localeId` (the
system locale for System, else language + this Mac's region - the macOS convention - or the
language's home region de_DE / es_ES / fr_FR / ja_JP with `setFormatsFromLanguage`);
`relative` (NSRelativeDateTimeFormatter, full units, named style: "yesterday", "vor 2
Minuten", "2 分前"; whole units like the old helper; older than 30 days -> a medium date;
under a minute -> tr("just now")), `dateTime`, `number` (NSNumberFormatter), `bytes`;
`isRtl` (first strong character: Hebrew, Arabic, Syriac, Thaana, NKo, ... and the RTL
presentation forms and supplementary ranges; digits and punctuation are weak); `visual` -
a CTLine with a right-to-left base writing direction, its glyph runs taken in visual
order, RTL runs reversed by composed character sequence (marks stay on their letter) with
brackets and guillemets mirrored, LTR runs (English words, numbers) kept; `wrapWords`.
Wiring: widgets::sectionHeader, widgets::primaryButton and paneHeader translate what they
are given; the rail tooltip, sidebar (date groups, search hint, By meaning, + New folder,
No conversations yet, Show archived / active), sign-in, the empty chat's greeting, prompt
and suggestions (a clicked suggestion goes out in the reader's language), the drawn and
the native (NSTextView) composer placeholders and the New chat button go through tr;
core::relativeTime and core::humanBytes now use the locale formatters (31 files call them);
the markdown renderer sends a paragraph, heading or list item whose text starts RTL to
`renderRtl` - wrapped in logical order, each line reordered and right-aligned (inline
styling dropped for such paragraphs). Settings "LANGUAGE AND REGION" card first: the
language combo, "Formats from the language instead of this Mac's region", a preview (date,
two relative times, a number, a size, the locale id), a note on coverage and a Hebrew
sample; `language` and `formatsFromLanguage` in extra, applied at start-up. `--i18ntest`
runs 14 checks: the table complete in all four languages with no duplicate keys and the
same placeholders, English returning its own pointer, German (plain, ##id, %s, a miss),
cache stability, Japanese, the pseudo-locale, the system language, numbers (1,234.5 /
1.234,5 / 1 234,5), relative times (2 minutes ago, vor 2 Minuten, hace 2 horas, yesterday,
分前), dates (25. Oktober 2026, octobre 2026, 2026年10月25日), isRtl, visual ("שלום world"
-> "world םולש", mirrored brackets, "1.2" kept), the locale switch (de_US by region, de_DE
by language) and wrapWords. Verified on a scratch home: in German the sidebar, rail
tooltips, greeting "Hallo, Admin", the four suggestions, "Nachricht senden..." in the
native composer and "vor 4 Minuten" on messages; a seeded Hebrew reply rendered
right-aligned and in correct reading order ("שלום! ... Open WebUI, ומספר: 1.2.") while its
English paragraph stayed left; the Settings card showed de_US formats (1,234,567.89,
10/25/26) - which led to the formats switch - and in Japanese with it ticked "2026/10/25
18:14", "2 分前 / 一昨日" and ja_JP, with the whole frame in Japanese (CJK from the Arial
Unicode fallback); the pseudo-locale bracketed every covered string and left the rest
(Theme, Reduced motion, ...) plain. Not covered: translating the rest of the UI (each
string needs a tr() at its call site; the pseudo-locale shows where), mirroring the
layout for RTL UI languages (no RTL UI language is offered), Arabic letter joining (ImGui
draws Arabic letters in their isolated forms; Hebrew needs no shaping), inline styling
inside RTL paragraphs, and the palette's command names.

2026-10-25 116 (image editing): against ComfyUI directly (Settings > Connection's ComfyUI URL;
this Mac runs ComfyUI 0.42 on MPS with sd_xl_base_1.0 and flux1-schnell-fp8). `src/core/imgedit.*`:
`workflow(Job)` builds API-format graphs - LoadImage of the uploaded source; inpaint adds
LoadImageMask (red channel) -> GrowMask (8 px, tapered) -> SetLatentNoiseMask on the
VAEEncode latent, KSampler at denoise 1.0, VAEDecode, then ImageCompositeMasked(destination =
the source, source = the decode, mask = the grown mask) so pixels outside the mask are the
source's exactly; img2img samples the encoded source at the strength; variations put
RepeatLatentBatch (1-8) before the sampler; upscale is ImageScaleBy lanczos straight to the
preview, or with "detail" re-encoded and sampled at 0.3; every graph ends in PreviewImage so
results go to ComfyUI's temp folder. `presetFor` (SDXL: 25 steps, cfg 6.5, dpmpp_2m karras;
Flux: schnell 4 / dev 20 steps, cfg 1, euler simple, negative prompt left empty),
`linksValid`, `maskPng` (the strokes scaled to the image, white on black, via the sketch
rasteriser), `fit` (multiples of 8, longest side <= 1536), `workingSize` (SDXL / Flux / SD3 ~1
MP with up-scaling capped at 2x, SD 1.x ~0.4 MP), `uploadedName` ("name [temp]"),
`historyImages` (running / finished / execution_error with ComfyUI's message), `viewQuery`,
`checkpoints` (from /object_info/CheckpointLoaderSimple), `summary`. `src/image_editor.mm`:
sources decoded and redrawn with ImageIO + Core Graphics (to the fitted size on open and to
the model's working size at run time, the mask following); uploads to /upload/image with
type=temp; POST /prompt; /history/<id> polled every 0.5 s (10 min cap); results fetched from
/view; the "Edit image" modal - canvas with the image fitted and the mask shown as
translucent red (brush 8-240 px, Option erases, Cmd+Z, Clear mask), Inpaint / Img2img /
Variations / Upscale, model combo (SDXL chosen by default when present), prompt, negative
(not for Flux), strength, count, 2x / 4x + detail, steps, seed + New seed, Run with elapsed
seconds, a status line ("1 result in 39.0 s - inpaint - sd_xl_base_1.0 - 25 steps - seed 1 -
1248x832" or ComfyUI's error), a results strip with Source, and Save to gallery (galleryAdd
with prompt, negative, model, seed, size and engine "comfyui (<mode> of <name>)"), Edit
this one, Save as. Edit buttons on gallery tiles; palette "Edit an image..."; ImageIO
linked. `--imgedittest` runs 17 offline checks (presets, the four graphs and their links,
Flux without a negative, a dangling link caught, the mask PNG decoded back, fit,
workingSize, upload names, history states and an execution error, view query,
checkpoints, summary); one real bug was found by it on the way - iterating items() of a
temporary JSON copy. Verified on a scratch home against the local ComfyUI: a synthetic
768x512 sky-and-hill PNG in the gallery opened from its Edit button; seven brush strokes
masked part of the sky; the first run at 768x512 took 128.6 s and SDXL filled the mask with a
soft cloud rather than the asked-for balloon, but a pixel comparison of the saved result
against the source showed every changed pixel inside the grown mask (x 412-655, y 33-237)
and none outside; that led to `workingSize`, and the rerun at 1248x832 (39.0 s, model warm)
painted a red hot-air balloon in the masked sky; Save to gallery stored it with prompt,
model sd_xl_base_1.0, seed 1, 1248x832 and "comfyui (inpaint of meadow.png)"; Edit this one +
Upscale 2x returned 2496x1664 in 0.5 s. ComfyUI's input and output folders were untouched
(0 files); its temp folder held the test's 5 uploads and 3 previews, which were then
deleted. Not covered: variations and img2img live (their graphs are self-tested and share
the sampler path), the detail pass live, Flux live, a remote ComfyUI over the Tailscale
name, inpaint at a working size different from the source being pixel-exact against the
original rather than the resized source, and Open WebUI's own image-edit API (the server
here has no image engine configured).

2026-10-25 65 (smooth streaming): the auto-scroll that pauses when you scroll up, the
"New text" pill and the per-chat scroll memory were already built, so this added the token
fade-in. `core/fade` keeps a per-message `Timeline` of when each rendered character first
appeared (a shrinking total, such as a regenerate, resets it); `md::Options::glyphAlpha`
asks it for an alpha per word as markdown draws, with a smoothstep over 0.25 s, and
`md::lastRenderedChars()` reports how far the draw got. Only streaming messages get a
timeline; settled ones are erased and draw exactly as before. Settings checkbox "Fade in
streamed text" (`extra["fadeIn"]`, default on); Reduced motion also disables it.
`--fadetest` runs 9 checks (first-seen arrivals, undrawn and settled sentinels, the curve's
ends and midpoint, monotonic alpha, `fading`, a shrink resetting, `forget` trimming).
Verified in the GUI against a scratch Open WebUI with qwen3:8b: frames 0.2 s apart during a
150-word reply showed the newest three words in steps from faint to full ("its crust
crackling" and the next word), the text above at full strength, and the settled reply fully
opaque; two earlier replies also ended fully opaque. Not covered in the GUI: the checkbox off
and Reduced motion on (both just skip the timeline), and table cells fading (they share the
paragraph path, so they should). Code blocks, table header rows and right-to-left paragraphs
draw through other paths and appear without fading, by design for now.

2026-10-25 142 (accessibility: VoiceOver and keyboard): ImGui keeps no widgets, so the tree
is rebuilt each frame. `IMGUI_ENABLE_TEST_ENGINE` turns on ImGui's item hooks;
`src/ui/a11y_imgui.cpp` implements them (ItemAdd gives the rectangle and flags, ItemInfo
the label and the checkable / openable / inputable state; windows registering themselves
are skipped; tab items report before their rectangle, so their label is held until it
arrives) and feeds `core/a11y`'s Builder. Panes add what ImGui does not report: headings
(`sectionHeader`, `paneHeader`), status text (`dimText`, `loadingRow`, `centeredText`),
links, region groups (rail, chat list, conversation or pane), and one group per message
labelled "author, when[, pinned][, writing]" holding its plain text (`md::toPlainText`,
cached per message), also for messages the virtualised transcript skips. Names come from
the tooltip for `icons::button` (replacing its ID), and explicitly for the rail, sidebar
and private-chat rows, starters and follow-up chips. Node keys are ImGui IDs or hashes of
a pane key, so elements are stable across frames. A modal leaves only a Dialog subtree.
One vendored change: `BeginCombo` gained an ITEM_INFO-style call (marked "owui-desktop
patch") so its 33 uses read as pop-up buttons with their current value.
`src/ui/a11y_bridge.mm`: `OWUIMetalView` (the Metal view's new class) returns the roots
plus the native subviews as children, hit-tests, and reports the focused node;
`OWUIAXElement`s look their node up by key on each call (roles: button, check box, tab
radio button, pop-up, disclosure, text field, static text, AXHeading, groups with
"message" / "dialog" role descriptions). Press plays a click into ImGui over three frames
(pointer, down, up); a text field first takes the keyboard from the native composer.
Notifications: layout changed (structure hash, at most twice a second), focused element
changed, and announcements (finished replies "qwen3:8b replied: ...", every toast). The
costlier nodes are built only while an assistive app has asked within two minutes,
VoiceOver is on, or `OWUI_A11Y_DEBUG` is set. That variable makes the app write the tree
(walked through the public NSAccessibility methods from the window) and its announcements
to a folder, and run "press / focus <label>" and "focused" commands from it. It is a test
hook, so a reader process needs no Accessibility permission. The composer is a named text
area ("Message"). Keyboard audit: ImGui keyboard nav was already on, and the transcript
already walked with arrows and letter keys. Added View > Next / Previous Region
(Cmd+Alt+] / Cmd+Alt+[) and Focus the message field (Cmd+L), all rebindable; moving to the
conversation clears ImGui's nav cursor so the focused message is what VoiceOver gets.
`--voiceovertest` runs 36 checks: cleanLabel, groups and frame union, stable duplicate
keys, hit test, structure hash, modal filtering, labels and announcement cut, region
cycling; then a headless ImGui context (hooks on; button, check box, named icon button,
text field, tree node, tabs, combo, heading, a scope-closed group, a press played in as
exactly one click, focusFirstIn landing on the group's first control, a modal); then an
offscreen window with the real view (roles, values, subroles, parents, screen frames,
hit test, element reuse across frames, accessibilityPerformPress clicking the ImGui
button, the window dump, pressLabel, announcements). Two bugs were found by it: the dump
crashed on a title-bar proxy that does not implement the method-based protocol, and tab
items were missing. Verified in the GUI against a scratch Open WebUI with
`OWUI_A11Y_DEBUG`. The tree showed the rail as 12 tabs with Chats selected, the named
chat-list rows, and the 14 header icon buttons named by their tooltips. A sent prompt
announced "qwen3:8b replied: Bonjour Hola Hallo"; both messages were groups with their
text and buttons. "press Language Greetings" opened that chat and "press Settings"
switched panes, showing headings, check boxes with state and disabled where they are, and
pop-ups with values. Cmd+Alt+] three times announced Navigation, Chat list, Conversation and
focused Chats, New chat, then the latest reply. Up moved focus to the user message, with
the accent bar on it on screen, and Cmd+L returned focus to the Message text area. Not
covered: VoiceOver itself was not run, because turning it on is a system setting. Every
check used the same public methods VoiceOver's server calls, in process. Sliders read as
text fields without their value. Text-field values are only known while a field is being
edited. Code blocks, tables and diff views inside messages are read only as the message's
plain text. The cost per frame was not measured.

2026-10-25 146 (follow-up chips): already built, logged here so the list reads right.
After each answer the server's task model proposes follow-ups (`generateFollowUps`, on
with Settings' auto follow-ups); they draw as chips under the reply, fill the composer,
and are saved with the message (`followUps`). Since #142 they are named buttons for VoiceOver.

2026-10-25 121 (true multi-window), finished: one process, a window per chat, native
tabs, drag a chat out. This replaces the second-instance version from 2026-10-24 (the
--chat / --mini flags still work for scripts). Design: App keeps its single-window code,
and a chat window swaps its state in. `OWUI_VIEW_MEMBERS` (app.h) names the 70 members that
belong to one window: chat, composer and its sync state, scroll and follow state, overlays
(palette, inspector, cheat sheet, citation, project files, find), selection, artifacts
list, timings, fade timelines and the window's streams. A `ViewState` declares the same
fields from it. `App::enterView` / `leaveView` / `runInView` (windows.cpp) swap them into
App and back, nesting included, and token 0 is the main window. Everything done for a
window runs inside that swap:
its frames (`frameView`: app-wide ticks, the socket, the scheduler, recovery, the dock
badge and the crash/update sheets stay with the main window; a chat window polls its own
streams and draws its UI and overlays), menu actions while it is key (`KeyViewScope` at the
top of every AppDelegate action, which also routes copy/paste chords to its ImGui context),
its composer's keys (native.mm runs composer callbacks through `native::runInWindow`),
VoiceOver presses and focus, and the network callbacks it started. `async::run` /
`runDetached` now carry the queuing thread's view token to the worker. `post` from there
runs back inside that window, or is dropped once it has closed. `postMain` posts to the
main window explicitly. Socket deltas find their stream in any window, and generated
titles and tags for a chat open elsewhere are routed to that window. Native side
(main.mm): `OWUIChatWindowController` has an NSWindow, an `OWUIMetalView`, a command queue
and its own ImGui context created over the main context's font atlas. Its style is copied
from the main one each frame and its frame rate follows the main window's. With the main
window hidden it drains the app's queue itself. Teardown happens after the close event and
never inside a frame. The native composer is one slot per window (`composerSelectWindow`);
window helpers (float, size, cascade) act on the window being drawn. Each ImGui context
has its own accessibility state (a11y_imgui) and each view reads its own tree. One
more vendored patch, marked "owui-desktop patch (#121)": imgui_impl_osx's event monitor
and key responder switch to the context they were made for (they used whatever context
was current, so a second context would have taken the first window's keys). Tabs: every
window shares tabbingIdentifier "OpenWebUIChat"; File ▸ New Tab (Cmd+T, rebindable as
chat.newTab), the tab bar's +
(`newWindowForTab:`), "Open in new tab" in the sidebar row and chat More menus and the
palette. "Open in new window" briefly disables tabbing so it is a window even while a tab
bar shows. Dragging a sidebar row past the window's edge and releasing opens it there
(`onDragChatOut`). The mini window is 560x760, has no sidebar, floats, and never tabs.
Closing the main window with chat windows open hides it; closing the last chat window
then quits, as closing the main window would. The artifact web view and PDF view stay in
the main window (a chat window shows a note and "Open this chat in the main window").
Found while testing: the composer sync state, the title being renamed and the tool picker
were function or file statics, so the main window's frame wiped what was typed in a tab.
All five are now view members. `--windowstest` runs 16 checks: fresh and inherited view
fields, the mini view, members swapped in and back, a view keeping its changes, nesting,
token 0 from inside a view, enter/leave, streams across windows, closing stopping streams
and refusing the token; a job's token on the worker and its post routed back, a post for
a closed window dropped, main posts skipping the runner; two contexts sharing one atlas
with separate accessibility trees, and one closing while the other keeps drawing.
Verified in the GUI against a scratch Open WebUI:
- Cmd+T opened a native tab beside the first chat, with the + button.
- Prompts sent in both tabs streamed at the same time. "The two oceans are Pacific and
  Atlantic." landed in one tab and "The two planets are Mercury and Venus." in the other,
  and each tab got its own generated title (Two Oceans, Planets).
- Shift+Cmd+N with a tab bar showing opened a separate window, not a third tab.
- Dragging the "Primary Colors" row out of a window opened it in a new window.
- Cmd+W on that window left the app running.
- Cmd+W on the main window hid it. A prompt sent from a chat window still got its reply
  ("The fruit is an apple."), so the queue drained without the main window.
- Closing the last two windows quit the app, with no crash report.
- The palette's "floating mini window" opened Planets at 560x760 without a sidebar.
Not covered: a scripted drag of about 0.3 s did not register as a drag-out, while a 1.4 s
drag did. Tab drag-out is AppKit's own and was not driven. The floating level was not
checked from outside. The main window's periodic work (chat-list refresh, socket
reconnect, scheduler) pauses while it is hidden. Some app-wide tool windows (sketch, image
and file editors, playground, tour) open in the main window even when started from a chat
window. Two windows on the same chat both edit it, with the last save winning. Remaining
static UI state in other panes, such as the settings pane's edit buffers, is shared.
