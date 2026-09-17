# Decisions

Non-obvious calls made in this fork, and why. Newest first.

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
