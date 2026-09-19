# Omawrite

A dead-simple Markdown writing app built with Qt Quick and C++ that automatically follows system dark/light mode.

> **This is a fork.** [omacom/omawrite](https://github.com/omacom/omawrite) is
> deliberately minimal and stays that way on purpose. This branch takes 21 of
> its open pull requests and adds two things nobody had built, so it has tabs, a
> file sidebar, a preview that renders local images, wikilinks, checkboxes,
> focus mode, image paste, and a command line that reaches the running window
> over D-Bus.
>
> 105 commits, 112 tests. Merging those pull requests meant reconciling
> contributors' designs rather than resolving conflict markers, and
> [`docs/DECISIONS.md`](docs/DECISIONS.md) records the calls that are not
> obvious from the diff. [`docs/STATUS.md`](docs/STATUS.md) says what was taken,
> what was left, and why.

<img width="2948" height="3227" alt="screenshot-2026-06-23_15-24-08" src="https://github.com/user-attachments/assets/4e930c0d-edda-4046-b444-a59eff523329" />
<img width="2948" height="3227" alt="screenshot-2026-06-23_15-23-23" src="https://github.com/user-attachments/assets/8ced7c26-961b-4ded-b263-84403001a951" />


## Install

Install via the Omarchy Package Repository via the `omawrite` package. It's installed by default in new installations of Omarchy (from Quattro forward).

## Command line

```
omawrite [FILE]              open a window, on FILE when there is one
omawrite --open FILE[:LINE]  show FILE in the window already on screen
omawrite --open FILE --tab   open it beside what is showing, not over it
omawrite --append FILE       add stdin to the end of FILE, no window
omawrite --list-tabs         print what the last session left open
omawrite --tabs [--json]     print what the running window has open now
omawrite --read [TAB]        print a tab's text as the editor holds it
omawrite --select TAB        bring a tab forward, and its window with it
omawrite --help              print the usage and exit
```

`--open` goes over the session bus, so it reaches the Omawrite already running
instead of starting a second one beside it — a script that shows a file five
times leaves five tabs at most, never five windows. It returns as soon as the
window has the file, so it does not block a shell:

```sh
omawrite --open notes/standup.md:12
```

`--append` and `--list-tabs` answer before Qt claims the terminal, so they work
over ssh with no desktop session and with nothing running. Appending writes the
file directly; an Omawrite holding that file notices through the watcher it
already has:

```sh
date -u +%F | omawrite --append notes/log.md
```

`--tabs`, `--read` and `--select` go over the same bus, and they are how an
agent or a script works on a document while you have it open in front of you.
`--tabs` numbers every tab across every window and marks the one showing.
`--read` prints that tab's text as the editor holds it, which the session file
is up to an autosave behind. `--select` moves the editor onto a tab. All three
name a tab the same way: the number `--tabs` printed, a path, a file name, or
nothing for the tab in front.

```sh
omawrite --tabs
omawrite --read standup.md > /tmp/draft.md
omawrite --select 2
```

This is mostly for coding agents. Ask one to open a file in Omawrite and it runs
`omawrite --help` first to work out how, so the help has to answer in the
terminal, and it has to say that a plain `omawrite FILE` stays up until the
window is closed while `--open` does not.

## Shortcuts

- `Ctrl+S` saves, though writing is saved for you anyway. Naming a document
  yourself uses the XDG desktop portal file picker.
- `Ctrl+Shift+S` saves as.
- `Ctrl+O` opens a Markdown file through the portal picker.
- `Ctrl+P` opens the system print dialog.
- `Ctrl+E` shows the sidebar listing the Markdown in the current document's
  folder, and puts the keyboard in it. Pressing it again takes the sidebar away
  and hands the keyboard back to the text — it never reaches into the panel
  while you are writing. It starts closed.
- `Ctrl+Shift+P` toggles the Markdown preview.
- `Alt+G` opens the Claude panel on the right of the window, and closes it
  again. See [Claude beside the document](docs/AGENT.md).
- `Ctrl+N` opens a new Omawrite window.
- `Ctrl+Z`, `Ctrl+Shift+Z`, and `Ctrl+Y` handle undo and redo.
- `Super+F` toggles fullscreen. Qt maps this key as `Meta+F`.
- `Ctrl+F` searches the document. Use `Enter` or `Ctrl+G` for the next match and `Shift+Enter` for the previous match.
- `Ctrl+H` opens find and replace.
- `Ctrl+B`, `Ctrl+I`, and `Ctrl+Shift+X` toggle bold, italic, and strikethrough Markdown. `Ctrl+K` inserts a link.
- `Tab` and `Shift+Tab` nest and unnest list items. Bullets and numbers line up under
  the item above them, and ordered lists renumber themselves.
- `Ctrl+Click` or `Ctrl+Enter` follows a link: Obsidian `[[wikilinks]]` open the note in Omawrite, `https://` and markdown `[text](url)` links open in the browser.
- Hovering a link names its destination above the footer, so you can read where
  it goes before deciding to follow it.
- Selecting with the mouse copies. Let go of the drag and what was highlighted
  is on the clipboard, the way a terminal does it, and the footer says how many
  characters for a moment. `Ctrl+Shift+C` turns it off and on, and it stays as
  you left it.
- `Ctrl+Shift+J` unwraps hard-wrapped lines: text pasted out of a mail client or
  a file wrapped at 72 columns goes back to one line per paragraph. It leaves
  blank lines, list items, table rows, headings, fenced and indented code, front
  matter and Markdown's own two-space line break where they are. With a
  selection it takes the selected lines, without one the whole document, and
  `Ctrl+Z` puts it back in a single step.
- `Ctrl+V` pastes an image from the clipboard: it is written to an `images` folder
  beside the document and referred to by a relative path, which the preview renders.
- `Ctrl++` and `Ctrl+-` adjust the editor text size. `Ctrl+=` also increases it,
  and `Ctrl+0` resets it to the original size.
- `Ctrl+Shift+T` toggles focus mode: typewriter scrolling and paragraph dimming.
- `Ctrl+/` shows the keyboard shortcut reference. `Ctrl+?` does the same, for
  anyone whose fingers already know it.

## Sidebar

The sidebar lists the folder the open document lives in — Markdown files and
the folders beside them, nothing else. Opening a document puts the cursor after
everything already written in it, so writing carries on with the sidebar still
open, and the document you are leaving is saved on the way out rather than
asked about.

With the keyboard, once `Ctrl+E` has put focus there:

- `Up`/`Down` or `j`/`k` move through the folder.
- `Enter`, `Right`, or `l` opens a document and hands the keyboard back to the
  text, or walks into a folder and stays put.
- `Backspace`, `Left`, or `h` goes up a level.
- `a` starts a new Markdown file and `A` a new folder; type the name and press
  `Enter`, or `Esc` to abandon it. A new document opens straight away, and an
  existing name is reported rather than overwritten.
- `Esc` returns to writing, leaving the sidebar open.

The folder is re-read whenever anything in it changes, and the selection keeps
its place through that rather than snapping back to the top. Opening the panel
starts from the document being written.

Drag its right edge to widen it; the width is remembered, and stops short of
squeezing the writing column below its usual measure.

## Claude panel

`Alt+G`, or the robot button at the top right of the window, opens a dock on the right
and asks Claude there rather than in a terminal in the next tile. It knows
which document is open, where the caret is and what is selected, and the turn
runs in the folder the document lives in, so the notes beside it can be read
without naming a path. Answers stream in as they are written. A line above the
input says what the turn is doing and for how long, and `Escape` or `stop`
ends it.

It drives the `claude` command line non-interactively, which means it can write
anything you can write without asking first. [docs/AGENT.md](docs/AGENT.md)
covers what it is told, what it runs, how an edit it makes reaches the editor,
and what has not been built yet.

## Font

The window is drawn in the desktop's own monospace font — JetBrainsMono Nerd
Font where Omarchy has installed it — at 12px, document and Claude panel alike.
A machine without it falls back to the bundled iA Writer Mono S. `Ctrl++` and
`Ctrl+-` change the document's size and remember it; `Ctrl+0` goes back to 12.

## Measure

The writing column is 65 characters wide. To change it, set `editorColumns`
under `[layout]` in `~/.config/Omawrite/Omawrite.conf` — any width from 20
characters up, or `0` to let the text fill the window with ten characters of
margin on either side.

## Saving

Writing is saved a moment after you stop typing, and again whenever you leave
the document — switching files, closing the window, or moving to another app.
There is no prompt to answer and nothing to remember to press; `Ctrl+Z` is the
way back rather than a discard button.

A document you never named takes its name from its first line when you leave
it, landing in the folder the sidebar is showing; if the same name is taken
already it becomes `... 2`. One with nothing written in it is not kept.

Unsaved drafts are recovered after an abnormal exit. Omawrite also watches open files
and warns before an external change can replace local work.

Text follows the desktop text size — `omarchy display text size`, or GNOME's
`text-scaling-factor` — and re-flows without a restart. The default of 12px leaves
Omawrite at the size it is designed around; larger and smaller sizes scale from there.
Editor text size starts at 20px, changes in 2px steps from 10px to 48px, and is
remembered across launches. Desktop text scaling is applied on top of this base size.

## Requirements

- Qt 6.5 or newer: `qt6-base`, `qt6-declarative`, `qt6-quickcontrols2`
- `xdg-desktop-portal` and a portal backend

The iA Writer Mono font is bundled under the SIL Open Font License 1.1; see
`fonts/OFL.txt`. The font is copyright Information Architects Inc. and based on
IBM Plex, copyright IBM Corp.
