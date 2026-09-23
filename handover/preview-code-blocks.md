# Handover: preview-code-blocks

Issue #2 on johnkattenhorn/omawrite, "Code blocks in the preview look the same
as body text". Everything below is for the lead to fold into `docs/STATUS.md`
and `docs/DECISIONS.md`; neither was edited on this lane.

## For STATUS, under "Written here"

- **Code blocks set apart in the preview** — the preview sets everything in one
  monospace face, so the font change Qt gives a code block disappeared, and a
  paragraph ran straight into the command output under it. Each run of fenced or
  indented code lines now sits on a tint mixed from the theme's own page and
  text colours, with padding round it, in lists and quotes as well as at the top
  level. A theme change restyles a preview that is already showing. Inline code
  spans are left as they were.

## For STATUS, under "Known"

- Two code blocks with only a blank line between them render as one tinted
  block. Qt's importer leaves nothing between them to split on: each line is a
  block marked as code, and the two runs are consecutive.
- The 140% line height puts its extra space under each line, so a block's
  bottom padding reads a little deeper than its top, most visibly on a
  one-line block. `QTextFrameFormat` has one padding for all four sides.

## For DECISIONS

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
