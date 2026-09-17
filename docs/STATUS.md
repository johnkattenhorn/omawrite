# Status

Last updated 2026-09-17.

A fork of [omacom/omawrite](https://github.com/omacom/omawrite), the Markdown
writing app Omarchy 4.0 ships. Upstream keeps it deliberately minimal. This fork
takes the open pull requests that make it a tool you can live in, and adds the
two things nobody had built: pasting an image, and a command line an agent can
drive.

## Where it is

Branch `custom`, 87 commits ahead of `origin/master` (upstream `8f98892`).
101 tests pass. Clean build, no warnings.

```sh
./bin/build    # build/omawrite
./bin/test     # 101 passing
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
- **Command line** — `--open FILE[:LINE]` over the session bus, `--append FILE`
  from stdin, `--list-tabs` from the session file.

## Not taken

- **#59** (tabs, session, assemble-folder) — not a tabs PR but a competing fork
  direction: its own preferences and about dialogs, macOS packaging, its own
  highlighter opinions, and it restores the `UnsavedChangesDialog` that #22
  deliberately removed for autosave. #60 is the same feature without the
  baggage.
- **#8** (link URL on hover) — needs a third hover-enabled `MouseArea` over the
  editor, on top of #40's and #66's. Worth revisiting by reusing #40's existing
  `target` rather than #8's separate `destination` span.
- **#9, #61** — obsolete. #22 deleted the dialog they fix.

## Next

- Upstreaming. Several of these reconciliations are worth sending back,
  particularly the #66-on-#30 checkbox continuation and the #50 fallback
  narrowing.
- `--open` currently goes to the primary window. A `--tab` flag to force a new
  tab rather than reusing the active one would round out the CLI.
- The preview jumps to the top of the document when toggled; no scroll sync yet.
