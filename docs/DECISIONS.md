# Decisions

Non-obvious calls made in this fork, and why. Newest first.

## 2026-09-23 — Touching code blocks are told apart, and images and code set solid

Two fenced blocks with only a blank line between them came out of Qt's importer
as one run of code lines with nothing between, so the preview drew them as one
block. There is nothing in the document to split on. So before Qt reads the
file, `separateAdjacentFences` puts a paragraph holding only a zero-width space
between a closing fence and an opening one that follows it, under the same
quote or list prefix. The typography pass hides that paragraph. Only the text
handed to Qt changes; the file does not. Fences are the only case that needs it:
an indented block next to a fenced one, or two fences in different languages,
already differ in how Qt marks them, so a run also ends where the fence or the
language changes.

A line holding an image had the same problem at a larger scale. The 40% was 40%
of the image's height, so a 1235px diagram had about 490px of blank page under
it. A line with an image in it is set at 100%, with a text size of bottom margin
so the text under it sits a paragraph break away rather than as a caption. A
test measuring a layout of the document made on the side missed this: it only
shows where the preview's own `TextEdit` lays the page out, so the test asks the
preview for positions.

The deeper bottom padding came from the 140% line height, which Qt adds below
each line. Under the last line of a block that room landed on top of the
padding. The last line is now set at 100%. `QTextFrameFormat` has one padding for
all four sides, so taking the room away was the fix, not adding to the top.

## 2026-09-23 — Print loads its images before Qt clones the document

Print rendered into a plain `QTextDocument` with HTML on and no base URL, so it
drew an image from any absolute path and could not find one written relative
to the document, which is every image Omawrite pastes. It now renders through
the preview's `PreviewDocument`, so both follow one set of rules.

That alone printed no images at all. `QTextDocument::print` draws a clone, and
`clone()` copies neither the base URL nor anything loaded so far, so in the
clone a relative name no longer resolves. It does copy resources added with
`addResource`, so `printableDocument` loads each image through the allow-list
before printing and adds it under the name as written. The test checks the
clone, parented the way `print` parents it, not the original: checking the
original passed while the PDF was missing its image.

## 2026-09-23 — Code blocks in the preview go in frames, tinted from the theme

Qt marks a code block with a monospace face and nothing else, and the preview
already sets every line in one monospace face on purpose, so a code block had
nothing left to tell it from the paragraph above. It needed a ground of its own.

A block format was the first thing to reach for and cannot do it.
`QTextBlockFormat` has no padding, and the importer makes one block per code
line, so a background on each block plus top and bottom margins leaves the tint
striped at every line. A `QTextFrame` has background and padding and holds any
number of blocks, so each consecutive run of code lines is wrapped in one with
`QTextCursor::insertFrame` on a selection of the run, which moves the existing
blocks in without copying them.

Three things turned up in doing it, and each has code that answers it:

- Qt marks a fenced block with `BlockCodeFence` and `BlockCodeLanguage`, and an
  indented block with an empty `BlockCodeLanguage` and no fence at all. The
  language is what finds both; the fence alone misses indented code, and the
  test fails that way.
- A Qt Quick `TextEdit` paints a frame's background across its margins too
  (the scene graph fills the frame's whole bounding rect), so a tinted frame
  cannot be moved in with a margin to sit under a list item or inside a quote.
  An untinted outer frame carries the indent and the tinted one sits inside it.
  The empty lines Qt leaves either side of the inner frame are hidden, or the
  gap above and below doubles.
- Inserting a frame gives the first line inside it a blank block format, which
  lost it the 140% line height, so that format is put back. The empty line it
  splits off on either side of the frame is kept as the gap between block and
  prose, held to a fixed height.

The tint is a small share of the theme's text colour mixed into its page
colour (`codeBlockTintShare` in `backend.cpp`), the way the chrome's dim
colours are mixed in `Main.qml`. Two hardcoded greys would be right for the
default palettes and wrong for every other Omarchy theme. It is also the first
thing in the preview document that depends on the theme (the text colour is a
QML binding on the `TextEdit`), so `loadOmarchyTheme()` now re-renders a
preview that is showing. Without that, a light-to-dark flip would leave light
tints on a dark page until the next keystroke.

## 2026-09-23 — Mermaid is parked

Issue #3 asked for ```` ```mermaid ```` blocks to draw as diagrams in the
preview. The preview is a `QTextDocument` with no JavaScript, so the only route
that keeps it is `mmdc` (mermaid-cli) drawing a PNG per block, cached by a hash
of its source. That works: on 2026-09-23 it drew an 11-node flowchart in 0.9 s.

It is still parked. `mermaid-cli` brings Node and headless Chromium, about
325 MiB, run as a background process from a text editor, with a cache folder,
an exception to the image allow-list and new ways to fail. Making it optional
keeps the install light but not the code. Upstream would not take it, and once
the preview runs one outside renderer the case for KaTeX, PlantUML and Graphviz
is made. GitHub, Obsidian or `mmdc` itself draw the diagram where it is needed.

If it comes back, two things change the cost. A general hook (a code-block
language mapped to a command the user sets, off by default) ships no
dependency. And a text renderer such as `mermaid-ascii`, a single Go binary,
draws into the code block itself, with no image and so no allow-list exception.

## 2026-09-23 — HTML stays off in the preview, and the preview says so

Issue #1 asked for a README's HTML header to render. A lane built it, and it
took more than dropping `MarkdownNoHTML`. Qt's importer pours each piece of
HTML into the block it is already in, so a heading runs into the paragraph
under it, and it counts `<x` against `</` and `/>`, so one `<br>` or `<img>`
hides the rest of the document. Working round that took about 250 lines: a
hand-written CommonMark HTML-block lifter, a void-tag rewriter that skips code
spans and fences, and fragments put back after the import. It still drew a logo
alone in `<p align="center">` at the left margin, left links in Qt's blue, did
not centre Markdown inside `<div align="center">`, and broke on indented HTML.

That is more parser than this editor should carry for a partial result, so the
flag stays. The issue allowed for it: the status line says "HTML shows as text
in the preview." once per document. It is found by reading the file again with
HTML on, into a document that loads nothing, and comparing the text: the flag
keeps HTML as text and its absence takes the tags out, so the two differ exactly
when there is HTML. A `<` in prose or tags in code read the same both ways and
say nothing.

The lane found a real hole while doing it, and that part is kept. When
`loadResource` returned nothing, Qt's `QTextImageHandler` read the file itself
from the name as written and drew it, so `![x](/any/path.png)` or a `file://`
image was drawn whatever the allow-list said. A refused image is now answered
with a transparent pixel. The test draws each image through the layout's own
handler, because asking the document for the resource is exactly the check the
fallback went round.

## 2026-09-21 — The handover is keyed on the file, not on the kind

Sending a bare launch to the running Omawrite was keyed on `Request::Kind`, and
that was the wrong key. `omawrite FILE` parses as `Run` with a path, not `Open`
— `Open` is only what `--open` produces. A desktop entry's `Exec=omawrite %f`
sends the first form, so every file opened from a file manager took the
"nothing was named" branch: the window came forward and the file was dropped.
Clicking a second file looked exactly like the switching bug fixed earlier that
morning, which is the worst way for a regression to present.

`Cli::handoverFor` now decides, and it asks whether a file was named rather than
how it was named. It lives in `Cli` rather than as a static in `main.cpp` so a
test can reach it; the decision had no coverage before, which is why the
regression shipped. `Append` is answered before a window is built and never
reaches it.

## 2026-09-21 — The panel's input scrolls under the caret

`inputBox` stops growing at `160 * textScale`, and the `TextArea` filled it with
nothing behind it that could scroll. A question longer than the cap ran on below
the border: the caret left the box and you could no longer see what you were
typing.

The `TextArea` is now `TextArea.flickable` inside a `Flickable`, which is what
makes Qt keep the caret in view — a `TextArea` that merely fills a clipped box
has nothing to scroll. The scrollbar is the `AsNeeded` one the history list
already uses, so the composer and the conversation behave the same way.

## 2026-09-21 — A bare launch goes to the Omawrite already running

`main.cpp` handed a launch to the running instance only when it named a file
(`Cli::Request::Open`). A launch with no file — a launcher entry, which is how
Omawrite is usually started — skipped that entirely and started a second
process. Both then restored the same window records and shared one
`session.json`, each overwriting the other's view of it. That is the suspected
source of the orphaned window records that used to swallow an open.

`Remote` gained a `Present` call: bring a window forward without changing what
is in it. A bare launch now asks for that first, and a process whose request was
taken has nothing left to do, so it exits. Opening Omawrite when it is already
open raises it rather than adding a second copy.

`Remote::claim` was called for its side effect and its answer dropped. Losing
the name means another Omawrite finished starting while this one was building
its windows, and the loser now hands its launch over and goes. It gives up its
windows through `WindowManager::abandonWindows`, which takes their records out
of the session with them — a record left behind is a window nothing is showing,
which is the state that swallowed an open. The handover is attempted before the
windows are given up, so a process that finds nobody to hand to still has
something to show.

## 2026-09-21 — An open that cannot be handed on is opened here

Picking a second file in the sidebar could leave the first one on screen. The
document flashed, the status line said the new file had opened, and nothing had
changed.

`openPath` brings a file forward rather than opening it twice, and asked the
session which tab holds it. `WorkspaceSession::findOpenLocalFile` searches every
window the session file lists, not the windows on screen, and
`WindowManager::activateTab` returned without a word when the tab belonged to a
window this process is not showing — the kind of record a crash, a kill or a
second process leaves behind. The open went to that window and stopped there.
`loadActiveBuffer()` then reloaded the tab that was already showing, which is
the flash, and `setStatus("Opened ...")` ran regardless. Two more return values
were dropped on the way: `createTab` and `updateTab` both answer whether the tab
took the file, and neither answer was read.

`activateTab` now says whether it landed, and the backend reaches it through a
`TabActivator` callback rather than a fire-and-forget signal, so the answer
comes back. An open that cannot be handed on is opened in the window the writer
is looking at. `createTab` and `updateTab` are checked, and a refusal says
"Could not open" instead of claiming success.

A window record nothing is showing is removed when it turns up, rather than left
to refuse every later open of the file it claims. Its tabs were unreachable in
any case, and a process that really is showing that window still holds their
text and writes it back on its next save.

What put a window record there in the first place is not settled. `main.cpp`
short-circuits a second instance only for a launch that names a file
(`Cli::Request::Open`), so a bare launch starts a second process that shares one
`session.json`; `Remote::claim` runs afterwards and its answer is discarded.
Two processes do run at once — that much is confirmed — but the step from there
to an orphaned window record is still a guess.

## 2026-09-21 — `origin` is this fork, `upstream` is omacom

The remotes were named the other way round: `origin` pointed at
`omacom/omawrite` and `fork` at ours. That inverts what every other repo here
means by `origin`, and it is not a cosmetic difference. A branch tracking
`origin/master` read as though it tracked our own master when it tracked
upstream's.

`upstream-image-paste`, the head of open pull request #69, was tracking
`origin/master` under the old names — upstream's master, not its own branch on
the fork. `git status` measured it "ahead 1" against the wrong base, and a bare
`git pull` there would have merged omacom's master into the pull request branch
without asking. A bare `git push` aimed at `omacom/master`, which only failed
because we hold `pull` and not `push` on that repo. Permissions were doing the
work a correct remote should have been doing.

Renamed `origin` to `upstream` and `fork` to `origin`, and repointed
`upstream-image-paste` at `origin/upstream-image-paste`. `master` still tracks
`upstream/master` on purpose: it is a mirror of upstream, and pulling it should
bring upstream's commits.

## 2026-09-19 — The preview is the same text, rendered

Qt renders Markdown at its own heading sizes and in its own fixed-pitch face,
so `Ctrl+Shift+P` changed the typography as well as the rendering: headings
jumped to a different scale and code came back in a fallback typewriter the
source view never uses. Two views of one document that disagree about how big
a heading is make the toggle feel like a different application.

The line height was the same mistake at block level: the source view sets its
lines 40% apart and the preview took Qt's single spacing, so the same words
came back at a different rhythm. `Backend::lineHeightPercent` is the one
number both views read.

`Backend::applyPreviewTypography` walks the rendered document and puts every
fragment back on the editor's own font, with headings at
`MarkdownHighlighter::headingPointSize` -- the same function the highlighter
uses, so the two cannot drift apart. Code takes the same face as the prose
around it, which is what the source view shows: everything here is written in
one monospace font, and a preview that switches face for a code span is
inventing a distinction the document does not make.

## 2026-09-19 — Wrapping reflows the paragraph rather than folding the line

`Ctrl+Shift+J` took hard-wrapped prose back to one line per paragraph.
`Ctrl+J` is the other direction, for the 80-column convention most Markdown
files are held to.

It joins before it fills. Folding each line where it stands would take a file
wrapped at 72 and leave it at 72 with a few words tucked under each line; the
measure is a property of the paragraph, not of whatever lines it happens to be
on, so the unwrap runs first and the fill runs over its result. That also means
one implementation decides what counts as a paragraph, and `Ctrl+J` then
`Ctrl+Shift+J` returns the document it started from.

A line whose breaks are its meaning is left alone however long it is: a
heading, a table row, a thematic break, a reference definition, fenced and
indented code, front matter. A heading folded in two stops being a heading, and
a folded table row stops being a table. A word longer than the measure — a URL,
mostly — takes a line of its own and overhangs, because a URL broken across two
lines is not a link.

`wrapColumns` is a separate setting from `editorColumns`. How wide the writing
column is drawn and what the file is wrapped to are two questions, and a writer
who wants a full-width column and an 80-column file should not have to choose.

## 2026-09-19 — A conversation belongs to a document, and outlives the window

The panel started as one chat per window, held in memory. Two sittings with it
showed what that costs: a rebuild and a restart during testing took a
conversation with it, and what was lost was not the transcript but the thread
of an argument being worked out — in that case whether Omawrite should help
hold Markdown to 80 columns.

So a chat is filed under the document it is about. Switching tabs switches the
conversation; switching back brings it back; closing Omawrite keeps them. The
store is `agent.json` in the same state directory as the tabs, 0600, the forty
most recently used documents, 64 messages each, oldest dropped first. It is
read-modify-written rather than overwritten, because two windows can hold it at
once and a blind write would take the other window's chat with it.

What makes it worth doing rather than decorative is the session id. The
transcript alone would be a picture of a conversation Claude no longer has;
kept beside it, the next turn resumes the same session and the model still
knows what was being argued. A session can be gone by then — cleared, expired,
or left on another machine — so a resumed turn that fails says exactly that,
drops the id, and lets the next question start cleanly, rather than failing the
same way for ever.

A turn already running when the writer changes tab keeps its chat: the answer
belongs to the document that asked for it, so the swap waits for it. **new**
clears the kept copy as well as the screen, because clearing is the one gesture
that means "do not bring this back".

The transcripts are on disk now, which the panel's first version deliberately
avoided. They are the writer's own questions about their own documents, and
Claude Code already keeps its full session transcripts under `~/.claude`; this
adds a smaller copy next to the tabs rather than a new kind of record.

## 2026-09-19 — The measure fills the window, and the docks take from it

The column was 65 characters, which is a good measure for a page and the wrong
one for a window with two docks in it: at 12px the text sat in the middle with
the sidebar and the panel opening into space it was never using.

It fills the width now, ten characters of margin either side, and
`availableEditorWidth` already subtracted whatever the docks were taking — so
opening either one narrows the column rather than sliding over it, and closing
it gives the width back. `editorColumns` still fixes a measure for anyone who
wants one.

A stored 65 is moved to 0 once, behind a `layout/measureFills` flag, because 65
was the default rather than a choice: an install that had never touched the
setting would otherwise keep the old column for ever. Any other number is
somebody's decision and is left where it is.

## 2026-09-19 — The chrome follows Omamail, down to the mix

Omamail is the other Qt window in this desktop doing the same kind of work, so
it is the standard this fork measures its chrome against rather than inventing
a second one.

Four things come from it. The AI control sits at the window's top right, where
Omamail's is: above the writing rather than in a header Omawrite does not have,
and clear of the verbs in the footer that act on the document. The tab strip
stops short of it rather than scrolling underneath. The icons are Nerd Font glyphs
from the Material Design Icons range the Omarchy shell draws its own bar from,
so a verb in the editor looks like the same verb on the desktop —
`content-save-outline`, `folder-open-outline`, `dock-left`, `eye-outline`, and
`robot-outline` for the agent, which is the glyph Omamail's own AI button
draws. And they are drawn at Omamail's `dim`, which is 68% foreground over 32%
background mixed from the live theme, rather than at a fixed grey behind 0.55
opacity: the old footer read as decoration you were meant to ignore.

The whole window is drawn in the desktop's font at Omamail's sizes -- 12 for
the text, 11 for the chrome around it -- rather than in iA Writer Mono S at 13
and 20. That started with the panel, where the narrower face shows more of an
answer at once, and then the document followed it: a panel and a page side by
side in two faces at two sizes read as two applications. `Backend::appFont`
settles which face that is once, preferring JetBrainsMono and falling back to
the bundled iA Writer Mono S, which is still what a machine without it gets.

The heading sizes had to move with it. They were multiples of a hardcoded
20px, so on a 12px document an H1 came out three times the body; they are
multiples of the size the document is actually set in now. The same line fixed
a ratio that had always been a third too large: the multipliers are ratios of
pixels and `setFontPointSize` takes points, so they now convert at Qt's
logical 96 DPI.

The drawn icons stay in the file as a fallback. A machine with no Nerd Font
installed would otherwise show five boxes, and Omawrite ships to machines that
are not this one. `FooterIconButton` picks the first Nerd family it finds and
falls back to the Canvas path when it finds none, so the question is answered
per machine rather than at build time.

## 2026-09-19 — Claude writes beside the document, in a panel the window owns

Working with an agent on a document has meant a terminal in the next tile: it
knows the folder, and nothing about the writing. It cannot see which file is in
front of you, where the caret is, or what you just selected, so every question
starts by typing a path. The panel exists to close that gap, not to add a chat
window.

[Omamail](https://github.com/huacnlee/omamail) had already built the shape —
`docs/AGENT.md` there is a full account of it — and it is the same stack, Qt
and QML over a native backend. What is taken from it: a dock on the right, a
child process per turn, `claude -p --output-format stream-json
--include-partial-messages`, the question on stdin rather than in an argument,
`--resume <id> --fork-session` for a follow-up, and a panel that shows answer
text and public status but never raw tool arguments or reasoning.

What is deliberately different is the working directory. Omamail runs the CLI
in a private per-turn state directory and hands it mail as JSON, because mail
is not a file. A document is a file, so the panel runs the CLI in the folder
the document lives in and lets Claude's own tools do the reading. That is the
whole feature request — "access all the other things in the working directory"
— and it costs no context plumbing at all. An untitled document has no folder
of its own, so it borrows the one the sidebar is showing.

The turn is given the document's path, the caret line and the selection, and is
told that `omawrite --read`, `--tabs` and `--open` reach the running window.
The reading calls landed yesterday for scripts and outside agents; the panel is
their first caller, and the reason a question about "this paragraph" can be
answered while the paragraph is still unsaved.

An agent that edits the document is the part that could go wrong: the editor
holds a buffer, autosave writes it, and a file changing underneath raises the
external-change prompt. The rule `reportExternalChange` already follows answers
it — a document holding no local changes takes the newer text silently — so the
panel saves the buffer before it starts a turn. An edit Claude makes then lands
in the editor without a dialog, and the prompt comes back only when the writer
typed during the turn, which is a real conflict and worth being asked about.

Permissions are `acceptEdits`. A headless child has nobody to ask, so the mode
answers for the writer, and one that prompts hangs the turn instead of
protecting it. Claude edits files without asking and refuses anything that
would otherwise need a prompt, shell commands included.

`dontAsk` was the first choice, copied from Omamail, and it was wrong here. The
name reads as the permissive one; it blocks Write and Bash both, which is
exactly right for a mail window that never edits anything and leaves a writing
panel able to read and talk and nothing else. It took a screenshot of the panel
answering "Can't write — Edit denied in this session. Here is the patch; paste
it in" to see it, and three runs — sandbox off, environment scrubbed, mode
changed — to establish that neither the sandbox nor the inherited session was
the cause. The mode is read from `agent/permissionMode`, so `bypassPermissions`
is there for anyone who wants the shell as well.

`acceptEdits` refuses every shell command, which made the brief's offer of
`omawrite --read` a promise the turn could not keep, so `agent/allowedTools`
carries an allow-list through to `--allowedTools` and ships defaulting to
`Bash(omawrite:*)` — the editor's own command line and nothing else. A machine
that wants its own tools names them; this one allows its skill CLIs and
read-only git.

The turn is told which permissions it has, because one that believes it can
run anything answers "here is a patch, paste it in" when it could have made
the edit, and reports a command it was never allowed to run as one that
failed.

Phase 1 is the panel, the stream and the folder. Insert-at-cursor and
replace-selection from a finished answer come next, and a D-Bus write call after
that, so an agent edit can arrive as one undoable buffer mutation rather than a
file change the watcher has to notice.

## 2026-09-18 — The reading calls answer from the window, not the session file

`--list-tabs` reads `session.json`, which is what autosave last wrote. That is
the right source for a question asked with nothing running, and the wrong one
for "what is on screen right now": the showing tab can be up to 750ms ahead of
it, and the session says nothing about which window is focused.

`--tabs`, `--read` and `--select` therefore go over the same session bus
`--open` already used, and `Remote::State()` reads each window's `Backend`
directly. The tab that is showing is measured from the editor's own
`QTextDocument`; the rest come from the session copy, because nothing else
holds them. `--list-tabs` stays as it was, for the case where nothing is
running.

One index runs across every window, so the number `--tabs` prints is the number
`--read` and `--select` take, and a tab can equally be named by its path or
file name — a number is only useful next to the listing that produced it.

## 2026-09-17 — Escape dismisses the external-change prompt, and answers nothing

The prompt used to refuse Escape: `closePolicy: Popup.NoAutoClose`, on the
grounds that keep, reload and neither are all answers and Escape cannot pick
one. A removed file showed what that costs. `Reload` is disabled when the file
is gone, so `Keep Mine` was the only control on a modal dialog, and the only
way out of the window was to kill the process — for a writer whose answer was
"the deletion was deliberate, carry on".

Escape and a click outside now dismiss it, which is what dismissing a popup
means everywhere else in Omarchy (`Ui/ConfirmDialog.qml` answers Escape and a
scrim click with `canceled()`).

Dismissing answers nothing, and the machinery for an unanswered question was
already there: `m_externalChangePending` stays up, so `saveTo()` refuses the
contested path, autosave falls back to the recovery draft, and the tab keeps
its dot. A removed file therefore stays removed. What is new is
`m_externalChangeDismissed`, which only an explicit `Ctrl+S` reads: with the
file gone there is no second version to weigh, so the save writes it back, and
with one on disk the prompt comes again rather than overwriting it unasked.
Autosave never takes that path, so neither happens behind the writer.

## 2026-09-17 — Copy on select watches the pointer, not the selection

Copying when `selectedText` changes would copy while a Shift+arrow selection is
still being extended, so the clipboard would hold every prefix of it in turn.
The copy happens when the left button comes back up instead, which is what a
terminal does and what a hand doing it expects.

The editor's own `MouseArea` cannot see that release. It passes the press
through (`mouse.accepted = false`) so the `TextEdit` can select, and passing the
press through means the release goes elsewhere too. A `PointHandler` only takes
a passive grab, so it reports press and release while the `TextEdit` runs the
selection underneath it.

`Ctrl+Shift+C` turns it off, under the `editor` settings category, because a
clipboard that changes without being asked is worth being able to refuse.

## 2026-09-17 — Unwrapping reads Markdown rather than counting blank lines

`Ctrl+Shift+J` joins hard-wrapped prose back onto one line. The naive version —
join every line that is not blank — eats the newlines that carry meaning, so the
walk in `EditorMutations.js` keeps a block's own lines wherever Markdown reads
them as structure: list items, quote markers, table rows, headings and the
underline that makes one, thematic breaks, fenced and indented code, link
reference definitions, YAML or TOML front matter at the top of the file, and a
line ending in two spaces, which is Markdown's own line break.

It also rewrites in one undo step. `replaceRange()` is a remove and an insert,
and on a whole document that leaves a `Ctrl+Z` between them showing an empty
page. `Backend::beginUndoBlock()` opens a `QTextCursor` edit block on the
attached document so both land in one undo command.

## 2026-09-17 — Opening replaces the active tab; only `Ctrl+T` adds one

#22 gives a sidebar you browse with. #60 gives tabs. Both answer an open, and
#60's answer is a tab per file — so a morning spent clicking through a folder
in the sidebar leaves thirty tabs waiting at the next start. The test session
reached 536 buffers before this was noticed.

Opening now takes over the tab that is showing. Autosave has already written
whatever it held, so nothing is lost, and a file some tab already holds is
brought forward rather than opened twice. `Ctrl+T` is the only thing that adds
a tab, which makes a tab mean "something I chose to keep open".

Reloading had to stop going through `open()` for the same reason: it would find
the tab already holding that path and stop there.

## 2026-09-17 — Autosave owns the text; the tab session owns the registry

#22 and #60 are two persistence designs. #22 writes the real file every 750ms
and has no unsaved state. #60 keeps each tab's text in `session.json` and writes
the file on `Ctrl+S`.

They are unified on #22's timer: `persistDocument()` saves the file first, then
writes the tab entry, so a tab records the state the save left behind rather
than the one it was about to change. `requestCloseTab()` saves on the way out
instead of prompting — #60's version called the dialog #22 deleted.

The tab session still stores text. It is redundant for a named file that
autosave has already written, and it is the only copy for an untitled one.

## 2026-09-17 — `--append` writes the file rather than asking a running instance

The tidy version would send the text over the bus so the editor shows it at
once. The useful version works with no instance running, no desktop session, and
over ssh — which is where a script or an agent actually appends to a note.

So it writes the file directly. An Omawrite holding that file sees the change
through the file watcher it already has. `--open` is the one that needs the bus,
because showing a file in a window means reaching a window.

## 2026-09-17 — Only a trailing run of digits after a colon is a line number

`omawrite --open "10:30 standup.md"` has to open that file. A colon is legal in
a filename, and guessing wrong opens a file nobody named. So `FILE:LINE` is only
read as a line number when everything after the last colon is digits and greater
than zero.

## 2026-09-17 — `#50`'s direct-write fallback is held to writable folders

#50 falls back to a plain write when `QSaveFile` never opens, because CIFS/SMB
answers `O_TMPFILE` with `ENOENT`. As written it also fired for a read-only
folder, where the target file is still writable — so a save that must be
reported as failed silently succeeded, and three of #22's tests caught it.

The fallback now requires the containing folder to be writable. CIFS still gets
its retry; a folder we may not write to still reports the refusal.

## 2026-09-17 — Focus mode without the disappearing footer

#17 fades the footer out and brings it back on hover. #43 had just given that
footer an opaque surface on purpose, because document text was showing through
it. The footer stays; focus mode takes only the typewriter scrolling and the
paragraph dimming.

## 2026-09-17 — Pasted images go to `images/` beside the document

#68's preview will only load an image under the document's own directory, and
sanitises SVG and caps allocation on the way. Anywhere else and a pasted image
renders as a broken reference. An untitled document is refused rather than
guessed at: it has no folder of its own, so an image put anywhere would not
survive being saved somewhere else.

## 2026-09-17 — Each test clears the tab session first

The suite shares one `AppDataLocation`, so every test inherited the tabs opened
by the tests before it — a thousand buffers by the end, and a `Backend` meant to
start untitled came up holding another test's document. Three of #60's own tests
fail on #60's own branch for this reason. Clearing `session.json` in `init()`
also took the run from 24s to 3s.
