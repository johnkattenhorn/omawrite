# Status

Last updated 2026-09-17.

A fork of [omacom/omawrite](https://github.com/omacom/omawrite), the Markdown
writing app Omarchy 4.0 ships. Upstream keeps it deliberately minimal. This fork
takes the open pull requests that make it a tool you can live in, and adds the
two things nobody had built: pasting an image, and a command line an agent can
drive.

## Where it is

Branch `custom`, 105 commits ahead of `origin/master` (upstream `8f98892`).
114 tests pass. Clean build, no warnings. Pushed to
[johnkattenhorn/omawrite](https://github.com/johnkattenhorn/omawrite).

```sh
./bin/build    # build/omawrite
./bin/test     # 114 passing
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
  `--append FILE` from stdin, `--list-tabs` from the session file.
- **Copy on select** — let go of a mouse selection and it is on the clipboard,
  the way a terminal does it, with a line in the footer that fades. A
  `PointHandler` does the watching; `Ctrl+Shift+C` turns it off, persisted under
  the `editor` settings category.
- **Unwrap hard-wrapped lines** (`Ctrl+Shift+J`) — pasted text wrapped at a
  column goes back to one line per paragraph, leaving every newline that means
  something: blank lines, list items, quotes, tables, headings, fences,
  indented code, front matter, and the two-space hard break. Selection first,
  whole document when there is none.
- **Escape dismisses the external-change prompt** — it answers nothing, so a
  file removed outside Omawrite stays removed and the writing carries on. An
  explicit `Ctrl+S` is what answers it.
- **Preview scroll sync** — the toggle used to drop the reader at the top.
- **Link hover** — reworked from #8 onto #40's existing hover, rather than
  adding a third MouseArea over the editor.

## Not taken

- **#59** (tabs, session, assemble-folder) — not a tabs PR but a competing fork
  direction: its own preferences and about dialogs, macOS packaging, its own
  highlighter opinions, and it restores the `UnsavedChangesDialog` that #22
  deliberately removed for autosave. #60 is the same feature without the
  baggage.
- **#9, #61** — obsolete. #22 deleted the dialog they fix.

## Next

- Upstreaming. Several of these reconciliations are worth sending back,
  particularly the #66-on-#30 checkbox continuation and the #50 fallback
  narrowing.
- `--open` always reaches the primary window. With more than one window open,
  choosing which one is not yet answered.
- The preview keeps its place through a toggle, but it does not follow the
  caret while you write.
