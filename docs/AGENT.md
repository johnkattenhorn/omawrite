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

The panel is drawn in the desktop's own font at Omamail's sizes: 12 for an
answer, 11 for the chrome around it. The document is drawn in the same face at
the same size, so the page and the conversation beside it read as one window.

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
question, with its brief, to 1 MiB.

## What is kept

A conversation belongs to a document, not to a window. Switching tabs shows
that document's chat; switching back brings the first one's back. Closing
Omawrite keeps them: the panel is where you work something out, and losing it
to a restart is how you find out you were relying on it.

They live in `agent.json` beside the session file, under
`$XDG_DATA_HOME/omawrite`, written 0600 — your questions and Claude's answers
about your own documents. The forty most recently used documents are kept, 64
messages each; the oldest goes when a forty-first arrives. A document that has
never been saved has nowhere to file a chat under, so its conversation stays
in memory.

The stored chat carries the Claude session id, so a turn after a restart
carries on the same conversation rather than only looking like it does. When
that session has gone — cleared, expired, or left on another machine — the
panel says so, drops it, and the next question starts a fresh one.

Selecting an answer copies it on release, the same rule the document follows
and the same `Ctrl+Shift+C` to turn it off. Each message is its own field, so a
drag stops at the message it started in.

**new** clears the conversation on screen and the kept copy with it. That is
the one thing that means "do not bring this back".

## Edits

Claude edits files with its own tools, and the editor is watching the file it
has open. The panel saves the buffer before it starts a turn, which is what
makes that work: `Backend::reportExternalChange` takes newer text without asking
when the document holds no local changes, so an edit made during a turn appears
in the editor with no dialog. Type while the turn is running and the
external-change prompt comes back, which is the right answer — that is a real
conflict, and the writer is the one who settles it.

## Permissions

Claude runs with `--permission-mode acceptEdits`. A child with no terminal has
nobody to ask, so the mode has to answer for you, and a mode that prompts hangs
the turn rather than protecting it.

What `acceptEdits` allows, plainly: Claude edits and writes files without
asking, anywhere your account can write. What it refuses: anything that would
otherwise need a prompt, which in practice means shell commands. Ask for
something that needs one and the panel says so rather than doing it — including
`omawrite --read`, which is a shell command like any other, so the brief's
offer of the live buffer only holds in a mode that allows Bash.

`dontAsk` sounds like the permissive one and is the opposite: it blocks Write
and Bash both, leaving a panel that can read and talk but not edit. It is the
right mode for Omamail, which never edits anything; it was the wrong one here,
and reading the file is all the panel could do until this was corrected.

Two settings decide what a turn can do, both read at spawn:

```ini
[agent]
permissionMode=acceptEdits          ; or bypassPermissions
allowedTools=Bash(omawrite:*)       ; --allowedTools, empty for none
```

`allowedTools` names what may run without anyone to approve it. The default is
the editor's own command line, which is what makes the brief's offer of
`omawrite --read` true rather than a promise the turn cannot keep. Widen it the
way Claude Code takes it — `Bash(git status:*) Bash(git diff:*)` — or set
`permissionMode=bypassPermissions` for a panel that can run anything, including
the network, with nothing asking first.

The turn is told which of these it has. Without that it offers a patch to paste
when it could have made the edit, or reports a command as failing when it was
never allowed to run.

`OMAWRITE_CLAUDE` names the command to run instead of `claude`, which is how
the tests drive a synthetic stream.

## Not yet

- **Insert at cursor** and **replace the selection** from a finished answer.
- A D-Bus write call, so an edit arrives as one undoable buffer mutation rather
  than a file change the watcher has to notice.
- A queue for a second question asked while a turn is running. Today the panel
  ignores it.
