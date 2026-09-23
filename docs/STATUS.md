# Status

Last updated 2026-09-23.

A fork of [omacom/omawrite](https://github.com/omacom/omawrite), the Markdown
writing app Omarchy 4.0 ships. Upstream keeps it deliberately minimal. This fork
takes the open pull requests that make it a tool you can live in, and adds the
two things nobody had built: pasting an image, and a command line an agent can
drive.

## Where it is

Branch `custom`, ahead of `upstream/master` (`8f98892`);
`git rev-list --count 8f98892..HEAD` says by how many. The `agent-panel` branch
is merged into it and has been deleted. The suite passes;
`recentresOnlyWhenFocusModeMovesTheEditor` is flaky and has failed one run in
five. Clean build, no warnings.
Pushed to
[johnkattenhorn/omawrite](https://github.com/johnkattenhorn/omawrite).

```sh
./bin/build    # build/omawrite
./bin/test     # builds and runs the suite, and says how many tests it is
```

## What is in it

Upstream pull requests, merged and reconciled:

| PR | What | Author |
|---|---|---|
| #22 | File sidebar (`Ctrl+E`) and autosave | @ejuro |
| #60 | Tabs, `Ctrl+T`/`Ctrl+W`/`Ctrl+Tab`, session restore | @joemugen |
| #68 | Preview (`Ctrl+Shift+P`) with sandboxed local images | @rafaelvzago |
| #40 | `Ctrl+Click` links and Obsidian `[[wikilinks]]` | @pastorryanhayden |
| #66 | Interactive checkboxes (`Ctrl+L`) | @LMRTX |
| #30 | `Tab`/`Shift+Tab` list nesting | @jwahdatehagh |
| #36 | Heading sizes | @kevinsteffer |
| #17 | Focus mode (`Ctrl+Shift+T`) | @jvlianodorneles |
| #41 | Persistent editor font size | @kevinherron |
| #24 | Configurable measure | @x3m |
| #13 | Open a file that is not there yet | @rodgco |
| #35 | `--help` in the terminal | @jankeesvw |
| #12 #15 #18 #34 #43 #48 #50 #55 #67 | Bug fixes | various |

Written here:

- **Image paste** (`Ctrl+V`) — upstream issue #64 had no pull request. Writes to
  an `images/` folder beside the document and inserts a relative reference,
  which is the only shape #68's preview sandbox will load.
- **Command line** — `--open FILE[:LINE] [--tab]` over the session bus,
  `--append FILE` from stdin, `--list-tabs` from the session file, and
  `--tabs` / `--read [TAB]` / `--select TAB` live from the running window, so
  an agent can see what is open, read a tab as the editor holds it, and move
  the editor onto one.
- **Copy on select** — let go of a mouse selection and it is on the clipboard,
  the way a terminal does it, with a line in the footer that fades. A
  `PointHandler` does the watching; `Ctrl+Shift+C` turns it off, persisted under
  the `editor` settings category.
- **Wrap at 80 columns** (`Ctrl+J`) — the convention Markdown files are held
  to, as the other half of the unwrap. It joins the paragraph before it fills
  it, so a file wrapped at 72 comes out at 80 rather than at 72 with the
  overhang tucked under; headings, tables, code, front matter and breaks keep
  their own lines, and a word longer than the measure overhangs rather than
  being cut. `wrapColumns` sets the measure.
- **Unwrap hard-wrapped lines** (`Ctrl+Shift+J`) — pasted text wrapped at a
  column goes back to one line per paragraph, leaving every newline that means
  something: blank lines, list items, quotes, tables, headings, fences,
  indented code, front matter, and the two-space hard break. Selection first,
  whole document when there is none.
- **Escape dismisses the external-change prompt** — it answers nothing, so a
  file removed outside Omawrite stays removed and the writing carries on. An
  explicit `Ctrl+S` is what answers it.
- **Claude panel** (`Alt+G`) — a dock on the right where Claude answers beside
  the document rather than in a terminal in the next tile. One turn is one
  `claude -p --output-format stream-json` child, started in the folder the
  document lives in, with the question on stdin and the answer streamed back.
  It is told the path, the caret line and the selection, and that the running
  window answers `--read`. The buffer is saved before a turn, so an edit the
  agent makes reloads without the external-change prompt.
  Conversations are filed under the document they are about and kept across
  restarts, session id included, so a turn after a restart carries on rather
  than starting over. [`docs/AGENT.md`](AGENT.md) has the rest, including the
  permission mode (`acceptEdits`: edits land, shell commands are refused) and
  what it costs.
- **Omamail's chrome** — the AI control sits at the footer's right edge as
  Omamail's sits at its header's, the icons are Nerd Font glyphs from the
  Material Design Icons range the Omarchy shell draws from, and they are drawn
  at Omamail's `dim` mix rather than a fixed grey behind 0.55 opacity. The
  drawn icons stay as the fallback for a machine with no Nerd Font. The window
  is drawn in the desktop's font at 12, document included, with the bundled
  iA Writer Mono S as the fallback; heading sizes follow the document's own
  size rather than a hardcoded 20px. The writing column fills the window and
  narrows as the docks take their width, with a one-time move off the old
  65-character default.
- **Preview scroll sync** — the toggle used to drop the reader at the top.
- **Preview follows the tab that comes forward** — with the preview
  showing, opening a file in a new tab or switching tabs left the previous
  document rendered while the tab strip, title, word count and status line
  had all moved on. A render was only ever asked for from the editor's
  `onTextChanged`, which returns early while `backend.restoringActiveBuffer`
  is up, exactly when a tab's text loads. It is asked for on
  `documentLoaded` instead, which fires after every load whoever caused it.
- **Code blocks set apart in the preview** — the preview sets everything in one
  monospace face, so the font change Qt gives a code block disappeared, and a
  paragraph ran straight into the command output under it. Each run of fenced or
  indented code lines now sits on a tint mixed from the theme's own page and
  text colours, with padding round it, in lists and quotes as well as at the top
  level. A theme change restyles a preview that is already showing. Inline code
  spans are left as they were.
- **Preview image refusals that hold** — a refused image used to fall
  through to Qt's own handler, which read and drew it from any path, so the
  allow-list only governed half the preview. A refusal is now a transparent
  pixel. HTML stays off, and the status line says "HTML shows as text in the
  preview." once per document that has some.
- **Link hover** — reworked from #8 onto #40's existing hover, rather than
  adding a third MouseArea over the editor.

## Not taken

- **#59** (tabs, session, assemble-folder) — not a tabs PR but a competing fork
  direction: its own preferences and about dialogs, macOS packaging, its own
  highlighter opinions, and it restores the `UnsavedChangesDialog` that #22
  deliberately removed for autosave. #60 is the same feature without the
  baggage.
- **#9, #61** — obsolete. #22 deleted the dialog they fix.

## Known

- Print (`Backend::printDocument`) renders with HTML on and none of the
  preview's resource rules, so a printout can differ from the preview and can
  draw an image the preview refuses.
- Two code blocks with only a blank line between them render as one tinted
  block. Qt's importer leaves nothing between them to split on: each line is a
  block marked as code, and the two runs are consecutive.
- The 140% line height puts its extra space under each line, so a block's
  bottom padding reads a little deeper than its top, most visibly on a
  one-line block. `QTextFrameFormat` has one padding for all four sides.
- `recentresOnlyWhenFocusModeMovesTheEditor` is flaky.
- A tab's full text is cached in `session.json`, so a large document makes a
  large session file: a 522KB `STATUS.md` gave a 533KB session.

## Next

- Upstreaming. Several of these reconciliations are worth sending back,
  particularly the #66-on-#30 checkbox continuation and the #50 fallback
  narrowing.
- `--open` always reaches the primary window. With more than one window open,
  choosing which one is not yet answered.
- The preview keeps its place through a toggle, but it does not follow the
  caret while you write.
- The Claude panel is phase one: it answers, and its edits reach the editor
  through the file. Insert-at-cursor and replace-the-selection from an answer,
  a D-Bus write call so an agent edit is one undoable mutation, and a queue for
  a question asked mid-turn are all still to build.
