# Feature plan: the fifty additions

Goal: build and ship all fifty features listed under "The list" below, then
install the result to /Applications. This document is written so that several
agents can take packages in parallel without colliding. Read all of it before
starting a package; the conventions section is what keeps the merges clean.

Branch: `claude/desktop-app-cpp-c307e5`, directory `owui-desktop/`. The tree
builds and links as of this commit; the feature code that exists is not yet
wired into the UI (that is package B).

## State of the tree

Done and compiling (not yet verified end to end):

| file | what it holds |
|---|---|
| `src/native.h/.mm` | global hotkey (Carbon), menu bar extra, Services, `owui://` URL scheme, drag-and-drop overlay, Core Spotlight, notifications with inline reply, Dock badge/progress, Quick Look, Live Text (Vision), Handoff, appearance observer, FSEvents watcher, sandboxed `runProcess` |
| `src/ui/highlight.h/.cpp` | tokenising syntax highlighter for ~20 languages, ImGui draw + HTML export |
| `src/ui/diffview.h/.cpp` | unified-diff parser tolerant of bad hunk headers, per-hunk accept/reject renderer, tolerant `apply`, LCS `generate` |
| `src/export.h/.mm` | chat -> Markdown / self-contained HTML / PDF (WebKit) |
| `src/features.cpp` | palette, quick ask, context inspector, in-chat search, presets, continue/retry, compaction, pins, scheduler, voice mode, offline queue, keyboard nav, profiles, auto-routing, slash prompts, compare view, reference column, export, native hook installation |
| `src/app.h` | all new state and method declarations (search for "Feature state") |

Stubs that compile but do nothing, each file owned by one package:

| file | package |
|---|---|
| `src/code_tools.cpp` | C |
| `src/knowledge_tools.cpp` | D |
| `src/tools_pane.cpp` | E |

Nothing in `features.cpp` or `native.mm` is called yet except what `app.cpp`
already did before this work. Wiring is package B and is the critical path.

## Conventions (non-negotiable)

- **One owner per file.** The ownership table below is the whole conflict
  policy. If your package needs a change in a file you do not own, write the
  function in a file you own and leave a one-line TODO naming the call site;
  package B makes the call.
- **UI thread only touches state.** Network and disk go through `async::run`;
  results come back with `async::post`. Nothing in a draw function may block.
  `native::runProcess` blocks - call it from a worker.
- **No new dependencies.** System frameworks and what is in `third_party/`.
  No Homebrew libraries.
- **Settings keys.** Persist in `settings().extra[...]` using these names, and
  add new ones to this list:
  `presets`, `schedules`, `profiles`, `projectFolder`, `projectFiles`,
  `globalHotkey`, `menuBarExtra`, `autoRoute`, `autoRouteSmallModel`,
  `autoRouteTokenThreshold`, `kbSync`, `localIndexFolder`, `localIndexEnabled`,
  `referenceChatId`, `theme` (lives in `settings().theme`: dark|light|system|midnight).
- **Style.** Match the surrounding code: `core::`, `widgets::`, `theme::`,
  `pane_common.h` helpers, comments explain why. No apologies in toasts.
- **Build:** `cmake --build build -j8` from `owui-desktop/`. It must stay green
  after every commit. If you cannot finish a function, leave the stub.
- **Verify before you say done.** See "Verification" - the scratch server
  recipe, the self-test, and the screenshot method are all there.
- **Commit** per package, ending the message with whatever attribution line
  your harness specifies. Do not push; do not open PRs; the branch owner merges.

## Packages

### A - Native layer verification and hooks
Owner files: `src/native.mm`, `src/native.h`, `resources/Info.plist.in`.

Everything is implemented but unverified. For each item: exercise it, fix what
is wrong, and note in this file what it needed.

- Hotkey ⌥Space -> `openQuickAsk()` (already wired in `installNativeHooks`).
  Verify Carbon registration survives app relaunch and respects the setting.
- Menu bar extra shows recent chats; `refreshStatusMenu()` must be called after
  `refreshChats` lands (TODO for B: call it from the `refreshChats` post).
- Services: select text in TextEdit -> Services -> "Ask Open WebUI". Needs the
  app launched at least once from /Applications for `NSUpdateDynamicServices`.
- `owui://ask?prompt=hello`, `owui://chat/<id>`, `owui://quick` from Terminal
  with `open`.
- Drop a file onto the window; confirm the overlay forwards clicks (hitTest
  returns nil) and drops reach `attachFiles`.
- Spotlight: after opening a chat, search its title in Spotlight. Handle the
  `CSSearchableItemActionType` continuation in `main.mm` (TODO for B:
  `application:continueUserActivity:` -> `openChat`).
- Notification reply: `native::notifyWithReply` after a completion when the
  window is not key (TODO for B in `afterCompletion`); reply text must reach
  `sendMessage`.
- Dock badge = number of running streams; progress during `ollamaPull`.
- Quick Look on an attachment (download to a temp file first).
- Live Text: pasting an image should offer "Extract text" using
  `native::recognizeText` (TODO for B in the composer).
- Handoff: `updateHandoffAndSpotlight()` on `openChat`; check the Safari
  Handoff icon appears on another device signed into the same iCloud account.

### B - Integration (critical path)
Owner files: `src/app.cpp`, `src/chat_pane.cpp`, `src/main.mm`,
`src/ui/markdown.cpp`, `src/ui/markdown.h`, `src/settings_pane.cpp`,
`src/sidebar.cpp`, `src/ui/theme.cpp`, `src/features.cpp`.

Wire what exists. Specifically:

1. `App::frame()`: call `tickScheduler()`, `tickVoiceMode()`,
   `tickOfflineQueue()` each frame; draw `drawPalette()`, `drawQuickAsk()`,
   `drawContextInspector()`, `drawCitationPopup()`, `drawProjectFilesPopup()`
   after the main window; call `installNativeHooks()` once after the first
   frame; `native::setDockBadge` from `streams_.size()`.
2. `startStream`: store the payload in `lastPayload`; record
   `timings[assistantId].sentMs`; honour `activeStreamModelOverride_`; apply
   `routeModel(text)` in `sendMessage` and set `routedModelNote`.
3. `pollStreams`: set `firstTokenMs` on first content, `doneMs` on done.
4. `openChat`: call `updateHandoffAndSpotlight()`; `deleteChat`: call
   `native::removeChatFromSpotlight`.
5. `sendMessage`: when the transport fails with status 0, push to
   `offlineQueue`, set `serverReachable=false`, toast; prepend
   `projectContextBlock()` to the system prompt when `projectFolder` is set.
6. `afterCompletion`: `native::notifyWithReply` when `!ImGui::GetIO().AppFocusLost`
   is false (window in background) and `settings().notifyOnCompletion`.
7. `chat_pane.cpp`:
   - header: presets button (`ImGui::OpenPopup("##presets")` + `drawPresetMenu()`),
     compare toggle, pins toggle, search (⌘F) via `drawChatSearchBar`, export
     submenu (md/html/pdf), "Open reference chat" picker filling
     `referenceChatId` and loading `referenceChat`, context-pressure meter
     (`currentContextTokens()` / `contextWindowFor(model)` as a thin bar).
   - per message: Continue button on the last assistant message; "Retry with…"
     submenu listing models -> `retryWithModel`; pin toggle; timing line from
     `timings`; focused-message highlight when `focusedMessageId` matches;
     scroll to `pendingScrollTo`; citations call `openCitation(source, i)`.
   - siblings: when `compareMode` and two assistant siblings exist, call
     `drawCompareView` instead of the single message.
   - layout: `drawPinnedRail` on the right when `showPins`;
     `drawReferenceColumn` when `referenceChatId` is set.
   - composer: `/` opens a popup of `slashPromptMatches(prefix)`; Live Text
     button when the last attachment is an image; voice-mode toggle and state
     indicator; offline banner when `!serverReachable`; `routedModelNote`.
   - keyboard: call `handleKeyboardNavigation(path)` once per frame.
8. `markdown.cpp`: code blocks render through `highlight::drawLine`; add a
   `onCodeBlockActions(code, language)` callback to `md::Options` and have the
   chat pane pass `[this](...) { drawCodeBlockActions(...); }`; when
   `diffview::looksLikeDiff(code)` render with `diffview::render` instead.
9. `settings_pane.cpp`: theme gains `system` and `midnight`; toggles for
   `globalHotkey` and `menuBarExtra` (call `native::registerHotkey` /
   `setStatusItemVisible` on change); profiles editor (`profiles()` /
   `saveProfiles` / `switchProfile`); auto-route toggle + small-model picker +
   threshold; schedules editor (add/remove/enable, hour/minute, weekdays);
   presets list with delete.
10. `theme.cpp`: add the `midnight` palette (true black background).
11. `main.mm`: menu items for palette (⌘K), quick ask, export, compare, pins,
    voice; `application:continueUserActivity:` for Spotlight/Handoff;
    `applicationShouldHandleReopen` shows the window.

Acceptance: every item in sections "Native", "Chat workflow" and the
highlighting/diff parts of "Code" in the list below is reachable from the UI
and works against the scratch server.

### C - Code tools
Owner file: `src/code_tools.cpp` (replace the stubs; keep signatures).

- `runCodeBlock`: language -> runner. python: `python3 -I file.py`; shell:
  `bash file.sh`; javascript: `jsc file.js`; cpp/c: `clang++ -std=c++17 -o out
  file.cpp && ./out`; go/rust if the toolchain exists (`go run`, `rustc`).
  Write to a temp dir, run through `native::runProcess(argv, dir, 30, true)`
  (sandboxed) on a worker, store in `codeResults[hash(code)]`, mark `running`
  while in flight.
- `lintCodeBlock`: python `python3 -m py_compile`; cpp `clang++ -fsyntax-only
  -Wall -Wextra`; javascript via `jsc` with `checkSyntax(readFile(path))`;
  shell `bash -n`. Same result storage, prefixed "lint:".
- `drawCodeBlockActions`: buttons Run / Lint / Apply to file… / Copy, and the
  result box below the block (output, exit code, timed-out notice, Clear).
  Called from markdown through package B's callback.
- `applyCodeBlockToFile`: `platform::openFiles(false)` -> read file ->
  `diffview::generate(path, before, code)` -> show the diff in a modal via
  `diffview::render(..., editable=true)` -> on confirm `diffview::apply` and
  write. Keep a `.bak` beside the file.
- `attachFolderAsContext`: walk the folder, honour `.gitignore` (support
  `name`, `dir/`, `*.ext`, leading `/`, `**`; ignore negations), skip binaries
  (NUL in first 1 KB) and files > 200 KB, cap total at 1.5 MB, emit one text
  document "### path\n```lang\n...\n```" per file, upload via `attachBytes`
  as `folder-context.md`.
- `attachGitContext(folder, mode)`: `git diff` | `git diff --staged` |
  `git log -8 --stat`, attach as a file; toast if not a repo.
- `explainBuildError(log)`: parse `path:line(:col)` references, read ±25
  lines around each from `projectFolder` (or absolute paths), compose a
  prompt with the log and the excerpts, `sendMessage` it.
- `setProjectFolder` (exists) + `projectContextBlock()`: a capped file tree
  (depth 3, 400 entries) plus the full text of each ticked `projectFiles`
  entry; `drawProjectFilesPopup()`: tree with checkboxes writing
  `projectFiles`, persisted in `extra["projectFiles"]`.
- `runCommand`: run in `projectFolder` (or home) with a 120 s timeout, append
  to `commandOutput`, toggle `commandRunning`. The UI for it is package E.

Acceptance: a Python block with a `print` runs and shows output; a block with
a syntax error lints with the message; apply-to-file on `src/core/common.h`
shows a diff and writes it on confirm; attaching `owui-desktop/src` produces
an upload under 1.5 MB that skips `build/`.

### D - Knowledge tools
Owner file: `src/knowledge_tools.cpp`.

- `syncFolderToKnowledge(folder, kbId)`: initial upload of every eligible file
  (same filter as package C's walk - copy the helper, do not share the file),
  `api_.addFileToKnowledge` each, remember `path -> fileId` in
  `extra["kbSync"][kbId]`; then `native::watchFolder` and on change: remove
  the old file from the KB, upload the new one, add it. `folderSyncStatus`
  carries progress text; `stopFolderSync` stops the watcher.
- `captureUrlToKnowledge(url, kbId)`: fetch with `net::perform`, strip tags
  and scripts (regex is fine), keep the title, upload as `<title>.md` via
  `api_.uploadFile`, add to the KB. Fall back to `api_.processWeb(url, kbId)`
  when the server has the web loader enabled.
- `drawCitationPopup()`: modal showing `citationPopup` - Open WebUI sources
  are `{source:{name,url}, document:[chunks], metadata:[{file_id,page}]}`.
  Show the chunk at `citationChunk`, prev/next chunk, "Open source" (URL ->
  `platform::openUrl`, file_id -> fetch `/api/v1/files/{id}/content` and show
  it with the chunk highlighted).
- Local index: SQLite at `core::supportDir()/local-index.db` with
  `(path, chunk, text, mtime, vector BLOB)`; chunk files at ~800 chars;
  embeddings from `settings().ollamaUrl + /api/embed` with
  `nomic-embed-text` via `net::perform`; `localSearch` = cosine top-k;
  `drawLocalIndexSection` = folder picker, Index button, progress, a query box
  with results, and an "Attach top hits to next message" action that prepends
  them to `projectContextBlock`-style context (coordinate the hook with B).
  `indexFolderLocally` runs on a worker and reports via `localIndexStatus`.

Acceptance: index `owui-desktop/src`, query "where is the socket.io
transport" and get `core/socketio.cpp` in the top three; sync a folder to a KB
on the scratch server and see edits re-upload within a few seconds.

### E - Tools pane and admin additions
Owner files: `src/tools_pane.cpp`, `src/admin_pane.cpp`,
`src/pane_common.h/.cpp`.

Tools pane tabs:
- **Command**: input, Run (-> `runCommand`), output area (mono, autoscroll),
  "Send output to chat" (-> composer), working-folder display with picker.
- **Build error**: multiline paste box (`buildErrorInput`) + Explain button
  (-> `explainBuildError`).
- **Benchmark**: model multi-select from `models`, Run (spawns
  `python3 ~/.ai-server/bench/bench_coding.py -m a,b` via `native::runProcess`
  on a worker, 20 min timeout), parse the per-task PASS/FAIL lines and the
  summary table into `benchResults`, draw a bar per model (score) and a
  second bar (tok/s) with `ImDrawList`, raw output below.
- **Local index**: `drawLocalIndexSection` (package D's function).
- **Folder sync** and **URL capture**: pickers + KB dropdown from
  `knowledgeList`, calling package D's functions; status lines.

Admin pane:
- Models tab: "fits in memory" column - installed size vs
  `hw.memsize * 0.75` minus resident sizes from `ollamaPs`; colour it
  fits / tight / too big.
- Resident models: keep-alive control (POST `/ollama/api/generate` with
  `{model, keep_alive:"30m"}`) and "Unload all".

Acceptance: benchmark tab runs two models and shows two bars; command tab
runs `ls` in the project folder.

### F - Release (after A-E land)
Owner: branch owner.
- `--selftest` still 52/52 against a scratch server.
- One screenshot per feature group in `docs/screenshots/`.
- README feature list updated; this file's status table updated.
- `cmake --install build`, confirm `/Applications/Open WebUI.app` launches.
- Commit.

## The list, mapped to packages

| # | feature | package |
|---|---|---|
| 1 | Global hotkey quick-ask | A (native), B (overlay wiring) |
| 2 | Menu bar extra | A |
| 3 | Services menu | A |
| 4 | Shortcuts.app actions | A - via `owui://` URL scheme (App Intents would need Swift; document the Open URL recipe) |
| 5 | URL scheme | A |
| 6 | Drag and drop | A |
| 7 | Spotlight indexing | A + B (continuation handler) |
| 8 | Notification with reply | A + B |
| 9 | Dock badge/progress | B |
| 10 | Quick Look | A + B |
| 11 | Live Text | A + B |
| 12 | Multiple windows/tabs | B - scoped to the in-window reference column (`drawReferenceColumn`); a second ImGui context per NSWindow is out of scope |
| 13 | Keyboard navigation | B (`handleKeyboardNavigation`) |
| 14 | System appearance + midnight | B |
| 15 | Handoff | A + B |
| 16 | ⌘K palette | B |
| 17 | `/` prompts | B |
| 18 | Message pinning | B |
| 19 | Chat presets | B |
| 20 | Side-by-side compare with diff | B |
| 21 | Continue | B |
| 22 | Retry with another model | B |
| 23 | Context-pressure meter | B |
| 24 | Context inspector | B |
| 25 | Compaction | B |
| 26 | Scheduled prompts | B (settings UI) - scheduler exists |
| 27 | Export MD/PDF/HTML | B (menu) - exporter exists |
| 28 | In-chat search | B |
| 29 | Per-message timing | B |
| 30 | Voice mode | B (toggle + indicator) - state machine exists |
| 31 | Syntax highlighting | B (markdown hook) - highlighter exists |
| 32 | Apply code block to file | C |
| 33 | Diff viewer with per-hunk apply | B (render hook) + C (apply) - parser exists |
| 34 | Attach folder, .gitignore | C |
| 35 | Git-aware prompts | C |
| 36 | Run code locally (sandboxed) | C |
| 37 | Explain build error | C + E (UI) |
| 38 | Command runner pane | C + E |
| 39 | Project mode | C + B (system prompt hook) |
| 40 | Lint generated code | C |
| 41 | Folder -> knowledge live sync | D + E (UI) |
| 42 | Clickable citations | D + B (click hook) |
| 43 | URL capture | D + E |
| 44 | Local embedding index | D + E |
| 45 | "What fits" guidance | E |
| 46 | Resident-model dashboard | E |
| 47 | Auto-routing | B (settings + sendMessage hook) - `routeModel` exists |
| 48 | Benchmark panel | E |
| 49 | Offline queue | B (hook in sendMessage) - tick exists |
| 50 | Server profiles | B (settings UI) - switching exists |

## Verification

Scratch server (never test against the live instance):

    docker run -d --name owui-test -p 3001:8080 \
      -e OLLAMA_BASE_URL=http://host.docker.internal:11434 \
      ghcr.io/open-webui/open-webui:main
    # first signup becomes admin:
    curl -s -X POST http://localhost:3001/api/v1/auths/signup \
      -H 'Content-Type: application/json' \
      -d '{"name":"Test Admin","email":"test@local.test","password":"testpass123"}'

Run the GUI against it without touching saved settings or the keychain:

    OWUI_SERVER=http://localhost:3001 OWUI_TOKEN=<token> OWUI_DEBUG=1 \
      "build/Open WebUI.app/Contents/MacOS/Open WebUI"

Self-test (52 checks, both streaming transports):

    OWUI_SERVER=http://localhost:3001 OWUI_TOKEN=<token> \
      "build/Open WebUI.app/Contents/MacOS/Open WebUI" --selftest

Screenshots: `screencapture -x out.png`, then drive the app with
`osascript -e 'tell application "System Events" to set frontmost of process
"Open WebUI" to true'` followed by `keystroke` calls. ⌘1-0 switch panes. Do
not click by screen coordinate - clicks land in whatever window is under them.

Use `qwen3:8b` for anything that needs a model reply; never load
`llama3.3:70b` (47 GB, evicts everything).

## Status log

Append a dated line when you land a package, with what was verified and what
was left: e.g. `2026-10-24 B: palette, quick ask, pins wired and screenshot;
voice mode untested (no mic in the test harness).`
2026-10-24 B: wired and verified against a scratch server - palette (Cmd-K), quick ask
(Cmd-J, answered 17x23=391), owui:// link sends a prompt, in-chat search with hit
markers, context inspector, time-to-first-token, syntax highlighting, PDF export
(rendered and checked), offline queue across a server stop/start with socket
reconnect and backoff. Self-test 52/52. Fixed along the way: HTTP fallback must not
send chat_id, empty replies now show an error, Escape closes modals, overlays get a
panel and border. Not exercised by automation: drag-and-drop, notification reply,
Services (needs the /Applications copy), Quick Look, Live Text, Handoff, voice mode
(microphone), compare view and pins (need clicks).

2026-10-24 C: `code_tools.cpp` implemented; build green. Verified with a scratch harness that links the real objects (45/45 checks): a Python `print` block runs and returns output; the sandbox blocks network and writes outside its scratch dir; an infinite loop stops at the 30 s timeout; js (jsc + console shim), c++ and shell blocks run. Lint reports the Python `SyntaxError`, clang errors, jsc `checkSyntax` and `bash -n`. `diffview::generate` + `apply` round-trips a 3-hunk edit to a copy of `src/core/common.h` (rejecting a hunk works; `.bak` written). Attaching `src` gives 491 KB / 57 files; attaching `owui-desktop` skips `build/` via `.gitignore` and files over 200 KB. Also checked: git log attach, non-repo toast, `runCommand`, `projectContextBlock`, the `explainBuildError` prompt, and 120 headless ImGui frames of the draw functions with asserts on. Not verified: the NSOpenPanel step of apply-to-file and the upload itself (no server used). Call sites B must add: `drawProjectFilesPopup()` every frame (it also hosts the apply-to-file modal and loads `projectFolder`/`projectFiles` from settings on first call); `drawCodeBlockActions` from the markdown callback; prepend `projectContextBlock()` in `sendMessage`; something that sets `showProjectFiles = true` (palette or composer). Result keys are `std::hash<std::string>(code)` and `"lint:"` + that.

2026-10-24 C follow-up (for B's diff Apply button): when `applyCodeBlockToFile` gets a unified diff, it resolves each `newPath`/`oldPath` against `projectFolder` (absolute paths too) and asks with `platform::openFiles` only when a path can't be found. It supports several files and new files (`/dev/null` source) and leaves deletions out with a toast. It dry-runs every file and shows any error in the confirm dialog, and on confirm writes all files or none, with `.bak` files. The harness now passes 50/50, including a two-file diff (an edit to common.h plus a new file) written through the real confirm path, and a diff that won't apply showing its error. The action row under code blocks is now a compact single row. Bug in `diffview::parse` (not C's file, not fixed): text ending in `\n` gains a phantom blank context line at the end of the last hunk. That line can't be placed in an empty file, and `apply`'s whole-file fallback can use it to put a hunk in the wrong place. C strips trailing newlines before parsing; `diffview.cpp`'s owner should drop the final empty piece in `parse`.

2026-10-24 D: `knowledge_tools.cpp` implemented, build green. Verified with a
throwaway harness that links every app object except `main.mm` (scratch server
on :3003, Ollama on :11434, HOME redirected so no real settings were touched):
local index over `src/` = 57 files / ~815 chunks in 11.7 s, re-run skips
unchanged files in 0.0 s; "where is the socket.io transport" -> 1.
`core/socketio.h`, 2.-3. `core/socketio.cpp`. Folder sync honours
`.gitignore`/dotfiles/binaries; an appended edit re-uploaded 1.4-1.5 s after
the write, a delete left the KB in 1.1 s, and a re-sync (with or without the
saved map) reports "4 unchanged" because it adopts KB files whose SHA-256
matches `meta.file_hash`. URL capture of a Wikipedia article landed a readable
40 KB `Asheron's Call - Wikipedia.md` (starts at `<main>`, links kept);
short or unfetchable pages fall back to `processWeb`, whose text is uploaded
as the same visible .md file. Citation popup and local-index section rendered
offscreen through the Metal backend with ImGui asserts on (clicks driven by
the test-engine item hooks): prev/next, "Open source" loading
`/files/{id}/data/content` scrolled to and highlighted the excerpt, Close
clears `showCitation`, "Attach top hits" adds `local-index-hits.md`.
`extra["kbSync"][kbId] = {folder, files: {absPath: {id, hash}}}`.
Call sites still to add: B - `drawCitationPopup()` once per frame;
chat-pane source chips -> `openCitation(message.sources[i], 0)`; optional:
`stopFolderSync()` in `shutdown()`; optional: when `localIndexEnabled`, call
`localSearch(text, 4)` on the worker that builds the payload (it blocks: it
embeds the query) and prepend the hits like `projectContextBlock()`.
E - Local index tab -> `drawLocalIndexSection(width)`; Folder sync tab ->
`syncFolderToKnowledge(folder, kbId)` / `stopFolderSync()`, show
`folderSyncStatus` (sync is live while `folderSyncToken >= 0`); URL capture
tab -> `captureUrlToKnowledge(url, kbId)` (the outcome arrives as a toast).
Not exercised: "Open source" on a web citation (`platform::openUrl`).
