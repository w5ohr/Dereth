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
