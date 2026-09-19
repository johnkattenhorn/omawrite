# Claude beside the document

`Alt+G`, or the robot button at the top right of the window, opens a dock on
the right of it. Ask a question in it and Claude answers there, next to
the paragraph the question is about.

The panel is not a chat window that happens to live in the editor. It knows
which document is in front of you, where the caret is and what you have
selected, and it runs where the document lives, so everything else in that
folder is already within reach.

```
Alt+G                 open the panel, or close it
Enter                 send
Shift+Enter           a newline in the question
Escape                stop the turn, or close the panel when nothing is running
new                   forget the conversation, keep the panel
drag the left edge    resize; double-click it to go back to the default width
```

The panel is drawn in the desktop's own font rather than the writing font,
at Omamail's sizes: 12 for an answer, 11 for the chrome around it. That is
narrower than iA Writer Mono S, so more of an answer fits beside the page.

## What Claude is told

The first turn of a chat carries the standing brief: the document's path, the
caret's line, the selection if there is one, and where the turn is running.
It also says that the editor answers its own command line, because the editor
is holding text no file has yet:

```
  `omawrite --tabs` lists what is open
  `omawrite --read [TAB]` prints a tab's text as the editor holds it,
  including edits no file has yet
  `omawrite --open FILE[:LINE]` moves the window onto something
```

Follow-ups carry the question alone, and say so when the document underneath
has changed since the chat started. A selection is passed inside a fence: it is
the writer's own words, not a second set of instructions.

## What it runs

One turn is one child process:

```
claude -p --verbose --output-format stream-json --include-partial-messages \
       --permission-mode dontAsk [--resume <session> --fork-session]
```

The question goes in on stdin, never in an argument — an argument list is
readable by every other process on the machine, and a document's words are not.
The working directory is the folder the document is saved in, or the folder the
sidebar is showing when the document has never been saved.

The panel shows answer text as it streams and a line saying what the turn is
doing — `Reading standup.md`, `Running a command` — with the seconds it has been
at it. It does not show reasoning, tool arguments, tool results or stderr.
Answers are drawn as plain text, so nothing an answer contains can reach for a
remote image or a link.

`stop`, or Escape, ends the turn and the process group it started, so a command
the turn was running goes with it. What had already arrived stays on screen.

A turn's answer is held to 64 KiB and the panel to its last 64 messages; a
question, with its brief, to 1 MiB. Nothing is written to disk: close the
window and the conversation goes with it.

## Edits

Claude edits files with its own tools, and the editor is watching the file it
has open. The panel saves the buffer before it starts a turn, which is what
makes that work: `Backend::reportExternalChange` takes newer text without asking
when the document holds no local changes, so an edit made during a turn appears
in the editor with no dialog. Type while the turn is running and the
external-change prompt comes back, which is the right answer — that is a real
conflict, and the writer is the one who settles it.

## Permissions

Claude runs with `--permission-mode dontAsk`. A child with no terminal cannot
answer an approval prompt, and a turn waiting on one would never end.

What that means is worth saying plainly: inside the panel, Claude can write
anything this user account can write, with nothing asking first. Your own
settings and tool permissions still apply; the panel adds no sandbox of its own.

The mode is read from the `agent/permissionMode` setting, so it can be changed
without a rebuild — and a mode that prompts will hang the turn rather than
protect it.

`OMAWRITE_CLAUDE` names the command to run instead of `claude`, which is how
the tests drive a synthetic stream.

## Not yet

- **Insert at cursor** and **replace the selection** from a finished answer.
- A D-Bus write call, so an edit arrives as one undoable buffer mutation rather
  than a file change the watcher has to notice.
- A queue for a second question asked while a turn is running. Today the panel
  ignores it.
- Anything remembered between sessions. Each window starts with an empty panel.
