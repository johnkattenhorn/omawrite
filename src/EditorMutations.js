.pragma library

function normalizePlainText(text) {
    return text.replace(/\r\n/g, "\n").replace(/\r/g, "\n");
}

function toggleWrap(editor, before, after) {
    var start = Math.min(editor.selectionStart, editor.selectionEnd);
    var end = Math.max(editor.selectionStart, editor.selectionEnd);
    var selected = editor.text.slice(start, end);

    // "*" is also the first half of "**", so a marker is only ours to strip
    // when it is a whole run rather than part of a longer one.
    var openChar = before.charAt(0);
    var closeChar = after.charAt(after.length - 1);

    // Selection includes the markers: strip them.
    if (selected.length >= before.length + after.length
            && selected.startsWith(before) && selected.endsWith(after)
            && selected.charAt(before.length) !== openChar
            && selected.charAt(selected.length - after.length - 1) !== closeChar) {
        var inner = selected.slice(before.length, selected.length - after.length);
        replaceRange(editor, start, end, inner, 0, inner.length);
        return;
    }

    // Selection (or caret) sits directly inside the markers: strip them.
    if (start >= before.length
            && editor.text.slice(start - before.length, start) === before
            && editor.text.slice(end, end + after.length) === after
            && editor.text.charAt(start - before.length - 1) !== openChar
            && editor.text.charAt(end + after.length) !== closeChar) {
        replaceRange(editor, start - before.length, end + after.length, selected,
                     0, selected.length);
        return;
    }

    replaceRange(editor, start, end, before + selected + after,
                 before.length, before.length + selected.length);
}

function replaceRange(editor, rangeStart, rangeEnd, replacement,
                      selectionStartOffset, selectionEndOffset) {
    var start = Math.max(0, Math.min(editor.text.length, rangeStart));
    var end = Math.max(start, Math.min(editor.text.length, rangeEnd));
    var insertedText = normalizePlainText(replacement);

    if (start !== end)
        editor.remove(start, end);

    editor.cursorPosition = start;
    editor.insert(start, insertedText);

    // TextEdit.insert() already leaves the caret after the inserted text. Only
    // move it again when the caller deliberately requests a selection/caret
    // within the replacement.
    if (selectionStartOffset !== undefined && selectionEndOffset !== undefined) {
        var insertedEnd = editor.cursorPosition;
        var selectionStart = Math.max(start,
                                      Math.min(insertedEnd, start + selectionStartOffset));
        var selectionEnd = Math.max(start,
                                    Math.min(insertedEnd, start + selectionEndOffset));
        if (selectionStart === selectionEnd)
            editor.cursorPosition = selectionStart;
        else
            editor.select(selectionStart, selectionEnd);
    }

    return insertedText;
}

// Markdown nests a list item under the item above it by indenting the item to
// where that item's own text starts: two columns for `- `, three for `1. `.
// Everything below works in those columns rather than in a fixed indent unit,
// so nesting lines up the way a Markdown reader resolves it.
//
// The editor asks for a "plan" — a range of the document, the text to put
// there, and where the caret lands — and applies it with replaceRange(). The
// same grammar lives again in markdownhighlighter.cpp, which colours these
// lines; the two cannot share code across the C++/QML boundary.

var TAB_WIDTH = 4;

var LIST_ITEM_RE = /^([ \t]*)([-+*]|\d+[.)])([ \t]+)(.*)$/;
var QUOTE_LINE_RE = /^([ \t]*)(>+)([ \t]+)(.*)$/;
// A GitHub task marker sits between a list marker and the item's own text.
var TASK_MARKER_RE = /^\[([ xX])\](?:([ \t]+)|$)/;

function columnWidth(text) {
    var width = 0;
    for (var i = 0; i < text.length; i++) {
        width = text.charAt(i) === "\t"
            ? (Math.floor(width / TAB_WIDTH) + 1) * TAB_WIDTH
            : width + 1;
    }
    return width;
}

function indentOf(width) {
    return " ".repeat(Math.max(0, width));
}

function leadingWhitespace(line) {
    return line.match(/^[ \t]*/)[0];
}

function isBlankLine(line) {
    return line.trim().length === 0;
}

// Indented text under an item: a wrapped paragraph, a code block, anything
// that travels with the item it hangs under.
function isContinuationLine(line) {
    return !isBlankLine(line) && /^[ \t]/.test(line);
}

function markedLine(match, ordered) {
    var prefix = match[1] + match[2] + match[3];
    var content = match[4];
    // A checked or unchecked box belongs to the marker, not to the text, so
    // an item holding only a box counts as empty and ends the list.
    var task = content.match(TASK_MARKER_RE);
    if (task)
        content = content.slice(task[0].length);
    return {
        task: task ? { checked: task[1].toLowerCase() === "x",
                       spacing: task[2] || " " } : null,
        indent: columnWidth(match[1]),
        indentText: match[1],
        marker: match[2],
        spacing: match[3],
        content: content,
        prefixLength: prefix.length,
        ordered: ordered,
        number: ordered ? parseInt(match[2], 10) : 0,
        delimiter: match[2].slice(-1),
        // Where the line's own text starts, and so where its children nest.
        contentIndent: columnWidth(prefix)
    };
}

function listItem(line) {
    var match = line.match(LIST_ITEM_RE);
    return match ? markedLine(match, /\d/.test(match[2])) : null;
}

// A blockquote carries its marker onto the next line like a list item, but it
// never nests or renumbers.
function quoteLine(line) {
    var match = line.match(QUOTE_LINE_RE);
    return match ? markedLine(match, false) : null;
}

function markedPrefix(line) {
    return listItem(line) || quoteLine(line);
}

function prefixLength(line) {
    var item = listItem(line);
    return item ? item.prefixLength : leadingWhitespace(line).length;
}

function documentLines(text) {
    var lines = text.split("\n");
    var starts = [];
    var offset = 0;
    for (var i = 0; i < lines.length; i++) {
        starts.push(offset);
        offset += lines[i].length + 1;
    }
    return { lines: lines, starts: starts };
}

function lineIndexAt(doc, position) {
    var index = 0;
    while (index + 1 < doc.lines.length && doc.starts[index + 1] <= position)
        index++;
    return index;
}

// The one line around a position, without splitting the whole document.
function lineAt(text, position) {
    var start = position > 0 ? text.lastIndexOf("\n", position - 1) + 1 : 0;
    var end = text.indexOf("\n", position);
    return text.slice(start, end < 0 ? text.length : end);
}

function insideCodeFence(text, position) {
    var before = text.slice(0, position);
    return ((before.match(/^\s*```/gm) || []).length % 2) === 1;
}

// A line still belongs to the surrounding list when it is an item, text
// hanging under one, or a blank line with more of the list beyond it.
function belongsToList(doc, index, step) {
    var scan = index;
    while (scan >= 0 && scan < doc.lines.length && isBlankLine(doc.lines[scan]))
        scan += step;
    if (scan < 0 || scan >= doc.lines.length)
        return false;
    return listItem(doc.lines[scan]) !== null || isContinuationLine(doc.lines[scan]);
}

// The whole list around an edit, because renumbering reaches past the lines
// that moved.
function listRegion(doc, firstLine, lastLine) {
    var start = firstLine;
    var end = lastLine;
    while (start > 0 && belongsToList(doc, start - 1, -1))
        start--;
    while (end + 1 < doc.lines.length && belongsToList(doc, end + 1, 1))
        end++;
    return { start: start, end: end };
}

// The nearest item above a line that sits at maxIndent or shallower. Blank
// lines and text hanging under an item don't interrupt the search; a
// paragraph does, because the list starts after it.
function itemAbove(doc, line, maxIndent) {
    for (var i = line - 1; i >= 0; i--) {
        var candidate = doc.lines[i];
        if (isBlankLine(candidate))
            continue;
        var item = listItem(candidate);
        if (item === null) {
            if (isContinuationLine(candidate))
                continue;
            break;
        }
        if (item.indent <= maxIndent)
            return item;
    }
    return null;
}

// An item nests under the sibling above it, aligning with that sibling's text.
// The first item of a list has nothing to nest under, and neither does one
// that already is a first child.
function nestingIndent(doc, line, anchor) {
    var above = itemAbove(doc, line, anchor.indent);
    return above !== null && above.indent === anchor.indent ? above.contentIndent : -1;
}

// Outdenting lifts an item to sit beside the item it used to hang under.
function outdentIndent(doc, line, anchor) {
    if (anchor.indent === 0)
        return -1;
    var above = itemAbove(doc, line, anchor.indent - 1);
    return above !== null ? above.indent : 0;
}

// Children move with the item they hang under.
function subtreeEnd(doc, lastLine) {
    var item = listItem(doc.lines[lastLine]);
    if (item === null)
        return lastLine;
    var end = lastLine;
    while (end + 1 < doc.lines.length) {
        var line = doc.lines[end + 1];
        var next = listItem(line);
        if (next !== null) {
            if (next.indent <= item.indent)
                break;
        } else if (!isContinuationLine(line)
                   || columnWidth(leadingWhitespace(line)) <= item.indent) {
            break;
        }
        end++;
    }
    return end;
}

// Walk the items of a region, reporting each one with the level it sits in. A
// level is a run of siblings at one indent; a bullet and a number at the same
// indent are separate lists, and unindented prose ends the list outright.
function walkListLevels(lines, visit) {
    var levels = [];
    for (var i = 0; i < lines.length; i++) {
        var line = lines[i];
        if (isBlankLine(line))
            continue;
        var item = listItem(line);
        if (item === null) {
            if (!isContinuationLine(line))
                levels = [];
            continue;
        }
        while (levels.length > 0 && levels[levels.length - 1].indent > item.indent)
            levels.pop();
        var level = levels.length > 0 ? levels[levels.length - 1] : null;
        var starts = level === null || level.indent < item.indent
            || level.ordered !== item.ordered;
        if (starts) {
            if (level !== null && level.indent === item.indent)
                levels.pop();
            level = { indent: item.indent, ordered: item.ordered, next: 0 };
            levels.push(level);
        }
        visit(i, item, level, starts);
    }
}

function levelStartLines(lines) {
    var starts = {};
    walkListLevels(lines, function (index, item, level, isStart) {
        if (isStart)
            starts[index] = true;
    });
    return starts;
}

// Numbers only mean anything in sequence, so every ordered level is renumbered
// once the structure has changed. A level that already opened the same way
// before the edit keeps its first number — a list deliberately starting at 3
// stays there — while a level this edit created starts at 1.
function renumberOrderedItems(lines, keptStarts) {
    walkListLevels(lines, function (index, item, level, isStart) {
        if (!item.ordered)
            return;
        var number = isStart && keptStarts[index] ? item.number
                   : isStart ? 1
                   : level.next;
        level.next = number + 1;
        if (number !== item.number) {
            lines[index] = item.indentText + number + item.delimiter + item.spacing
                + item.content;
        }
    });
}

function offsetInLines(lines, lineIndex, column) {
    var offset = 0;
    for (var i = 0; i < lineIndex; i++)
        offset += lines[i].length + 1;
    return offset + column;
}

// Follow a caret across a rewritten line: text keeps its distance from the
// marker, and a caret inside the marker stays inside the new one.
function mapPosition(doc, region, newLines, position) {
    var line = lineIndexAt(doc, position);
    if (line > region.end)
        return offsetInLines(newLines, newLines.length - 1,
                             newLines[newLines.length - 1].length);
    var index = line - region.start;
    var column = position - doc.starts[line];
    var oldPrefix = prefixLength(doc.lines[line]);
    var newPrefix = prefixLength(newLines[index]);
    var mapped = column >= oldPrefix ? newPrefix + (column - oldPrefix)
                                     : Math.min(column, newPrefix);
    return offsetInLines(newLines, index, Math.min(mapped, newLines[index].length));
}

// Drop text into a range and leave the caret after it.
function textPlan(start, end, replacement) {
    return {
        start: start,
        end: end,
        replacement: replacement,
        selectionStartOffset: replacement.length,
        selectionEndOffset: replacement.length
    };
}

// Narrow a rewritten region down to the lines that actually differ, so an edit
// leaves the rest of the list — and the undo stack — alone. The caret offsets
// come in measured against the whole rewritten region.
function linesPlan(doc, region, newLines, caretStart, caretEnd) {
    var oldLines = doc.lines.slice(region.start, region.end + 1);
    var first = 0;
    while (first < oldLines.length && first < newLines.length
           && oldLines[first] === newLines[first])
        first++;
    if (first === oldLines.length && first === newLines.length)
        return null;

    var oldLast = oldLines.length - 1;
    var newLast = newLines.length - 1;
    while (oldLast >= first && newLast >= first && oldLines[oldLast] === newLines[newLast]) {
        oldLast--;
        newLast--;
    }

    var replacement = newLines.slice(first, newLast + 1).join("\n");
    var dropped = offsetInLines(newLines, first, 0);
    var start;
    var end;
    if (oldLast >= first) {
        start = doc.starts[region.start + first];
        end = doc.starts[region.start + oldLast] + oldLines[oldLast].length;
    } else if (first < oldLines.length) {
        // Lines inserted ahead of a line that stays put.
        start = doc.starts[region.start + first];
        end = start;
        replacement += "\n";
    } else {
        // Lines appended after the region, which has no newline to insert at.
        var tail = oldLines.length - 1;
        start = doc.starts[region.start + tail] + oldLines[tail].length;
        end = start;
        replacement = "\n" + replacement;
        dropped -= 1;
    }
    return {
        start: start,
        end: end,
        replacement: replacement,
        selectionStartOffset: caretStart - dropped,
        selectionEndOffset: caretEnd - dropped
    };
}

function indentPlan(doc, from, to, direction) {
    var firstLine = lineIndexAt(doc, from);
    var lastLine = lineIndexAt(doc, to);
    // A selection ending exactly at a line start stops on the line before it.
    if (lastLine > firstLine && doc.starts[lastLine] === to)
        lastLine--;

    // The shallowest item in the selection sets the level the block moves by;
    // the rest keep their distance from it.
    var anchorLine = -1;
    var anchor = null;
    for (var i = firstLine; i <= lastLine; i++) {
        var item = listItem(doc.lines[i]);
        if (item !== null && (anchor === null || item.indent < anchor.indent)) {
            anchor = item;
            anchorLine = i;
        }
    }
    if (anchor === null)
        return null;

    var target = direction > 0 ? nestingIndent(doc, anchorLine, anchor)
                               : outdentIndent(doc, anchorLine, anchor);
    if (target < 0)
        return null;
    var delta = target - anchor.indent;

    var blockEnd = subtreeEnd(doc, lastLine);
    var region = listRegion(doc, firstLine, blockEnd);
    var newLines = doc.lines.slice(region.start, region.end + 1);
    var keptStarts = levelStartLines(newLines);
    for (i = firstLine; i <= blockEnd; i++) {
        var line = newLines[i - region.start];
        if (isBlankLine(line))
            continue;
        var indent = leadingWhitespace(line);
        newLines[i - region.start] = indentOf(columnWidth(indent) + delta)
            + line.slice(indent.length);
    }
    renumberOrderedItems(newLines, keptStarts);

    return linesPlan(doc, region, newLines,
                     mapPosition(doc, region, newLines, from),
                     mapPosition(doc, region, newLines, to));
}

// Indent (direction 1) or outdent (direction -1) the list items the selection
// touches. Returns null when there is nothing to nest, leaving Tab to whatever
// it did before.
function listIndentPlan(text, selectionStart, selectionEnd, direction) {
    var from = Math.min(selectionStart, selectionEnd);
    var to = Math.max(selectionStart, selectionEnd);
    // Tab outside a list is the common case; answer it from the caret's own
    // line rather than by walking the document.
    if (from === to && !LIST_ITEM_RE.test(lineAt(text, from)))
        return null;
    if (insideCodeFence(text, from))
        return null;
    return indentPlan(documentLines(text), from, to, direction);
}

// A bare marker ends the block: an indented item steps out one level, anything
// else drops out of the list or quote altogether.
function endBlockPlan(doc, lineIndex, from, to) {
    var item = listItem(doc.lines[lineIndex]);
    if (item !== null && item.content.length === 0 && item.indent > 0) {
        var lift = indentPlan(doc, from, to, -1);
        if (lift)
            return lift;
    }
    return textPlan(doc.starts[lineIndex], to, "\n");
}

// Carry the marker onto the next line, splitting the item at the caret and
// renumbering the rest of the list around it.
function continueBlockPlan(doc, lineIndex, from, to) {
    var line = doc.lines[lineIndex];
    var lastLine = lineIndexAt(doc, to);
    var tail = doc.lines[lastLine].slice(to - doc.starts[lastLine]);
    var head = line.slice(0, from - doc.starts[lineIndex]);
    var item = listItem(line);
    if (item === null) {
        var quote = quoteLine(line);
        return textPlan(from, to, "\n" + quote.indentText + quote.marker + quote.spacing);
    }

    var marker = item.ordered ? (item.number + 1) + item.delimiter : item.marker;
    var region = listRegion(doc, lineIndex, lastLine);
    var newLines = doc.lines.slice(region.start, region.end + 1);
    var split = lineIndex - region.start;
    // A new item starts unchecked however the one above it was ticked.
    var task = item.task ? "[ ]" + item.task.spacing : "";
    newLines.splice(split, lastLine - lineIndex + 1, head,
                    item.indentText + marker + item.spacing + task + tail);
    // A new item is always a sibling of the one it follows, so it never opens
    // a level and every level start survives the split.
    renumberOrderedItems(newLines, levelStartLines(newLines));

    // The caret goes where the text carried onto the new line begins, which
    // renumbering can shift but never moves off the end of the line.
    var caret = offsetInLines(newLines, split + 1,
                              newLines[split + 1].length - tail.length);
    return linesPlan(doc, region, newLines, caret, caret);
}

// Return inside a list or a blockquote. What the caret has behind it on the
// line decides which: a marker with text after it carries the marker onto the
// next line, a bare marker ends the block. Returns null where Markdown
// structure doesn't apply, leaving Return to the editor's own default.
function returnPlan(text, selectionStart, selectionEnd) {
    var from = Math.min(selectionStart, selectionEnd);
    var to = Math.max(selectionStart, selectionEnd);
    // A fence suspends the structure below it, so Return is a plain break.
    if (insideCodeFence(text, from))
        return textPlan(from, to, "\n");

    var doc = documentLines(text);
    var lineIndex = lineIndexAt(doc, from);
    var typed = markedPrefix(doc.lines[lineIndex].slice(0, from - doc.starts[lineIndex]));
    if (typed === null)
        return null;
    return typed.content.length === 0 ? endBlockPlan(doc, lineIndex, from, to)
                                      : continueBlockPlan(doc, lineIndex, from, to);
}

// Hard-wrapped text — pasted out of a mail client, a terminal, a file wrapped
// at 72 columns — ends every line with a newline. Markdown still reads the
// block as one paragraph, but every edit after the paste has to be rewrapped
// by hand. Unwrapping puts each paragraph back onto the one line it is and
// leaves the newlines that mean something alone: the blank line between
// paragraphs, the lines of a list, a table, a fence or front matter, and the
// two trailing spaces Markdown reads as a line break of its own.

var FENCE_RE = /^[ \t]{0,3}(```+|~~~+)/;
var ATX_HEADING_RE = /^[ \t]{0,3}#{1,6}([ \t]|$)/;
var THEMATIC_BREAK_RE = /^[ \t]{0,3}([-*_])[ \t]*(?:\1[ \t]*){2,}$/;
// The `===` or `---` under a paragraph that makes it a heading.
var SETEXT_UNDERLINE_RE = /^[ \t]{0,3}(?:=+|-+)[ \t]*$/;
var TABLE_ROW_RE = /^[ \t]{0,3}\|/;
var TABLE_DELIMITER_RE = /^[ \t]{0,3}\|?[ \t]*:?-+:?[ \t]*(?:\|[ \t]*:?-+:?[ \t]*)+\|?$/;
var REFERENCE_DEFINITION_RE = /^[ \t]{0,3}\[[^\]]+\]:/;
var INDENTED_CODE_RE = /^(?: {4}|\t)/;
var HARD_BREAK_RE = /(?:[ \t]{2,}|\\)$/;
var FRONT_MATTER_RE = /^(?:---|\+\+\+)[ \t]*$/;
var QUOTE_MARKER_RE = /^[ \t]*(?:>[ \t]*)+/;
var QUOTE_START_RE = /^[ \t]{0,3}>/;

// A file written on Windows carries a carriage return at the end of every
// line. It is part of the break, not of the text, so it goes when the break
// does and it never decides anything.
function withoutCarriageReturn(line) {
    return line.replace(/\r+$/, "");
}

// A line Markdown reads as the start of something in its own right, and so
// never as the continuation of the line above it.
function startsOwnBlock(line) {
    return isBlankLine(line)
        || FENCE_RE.test(line)
        || ATX_HEADING_RE.test(line)
        || THEMATIC_BREAK_RE.test(line)
        || SETEXT_UNDERLINE_RE.test(line)
        || TABLE_ROW_RE.test(line)
        || TABLE_DELIMITER_RE.test(line)
        || REFERENCE_DEFINITION_RE.test(line)
        // A blockquote interrupts a paragraph rather than continuing it, and a
        // bare marker is the blank line of the quote it sits in.
        || QUOTE_START_RE.test(line)
        || listItem(line) !== null;
}

// What the block opened by this line does with the lines under it. Prose and
// the text of a list item or a blockquote run on; everything else keeps the
// lines it was given.
function blockKind(line) {
    // A nested item is indented far enough to look like code, so the list
    // grammar answers first.
    if (listItem(line) !== null)
        return "prose";
    // A marker with nothing after it is the quote's own blank line.
    if (QUOTE_START_RE.test(line))
        return isBlankLine(line.replace(QUOTE_MARKER_RE, "")) ? "verbatim" : "quote";
    if (isBlankLine(line)
            || ATX_HEADING_RE.test(line)
            || THEMATIC_BREAK_RE.test(line)
            || SETEXT_UNDERLINE_RE.test(line)
            || TABLE_ROW_RE.test(line)
            || TABLE_DELIMITER_RE.test(line)
            || REFERENCE_DEFINITION_RE.test(line)
            || INDENTED_CODE_RE.test(line))
        return "verbatim";
    return "prose";
}

// A line joins the block above it when that block runs on, the line it would
// join did not end in a deliberate break, and the line is wrapped prose rather
// than the start of the next thing.
function continuesBlock(kind, previous, line) {
    if (kind === "verbatim" || HARD_BREAK_RE.test(withoutCarriageReturn(previous)))
        return false;
    if (kind === "quote") {
        // Inside a quote the marker travels down the wrapped lines, so what
        // the marker carries is what decides.
        var quoted = quoteLine(line);
        if (quoted !== null)
            return !isBlankLine(quoted.content) && !startsOwnBlock(quoted.content);
        return !startsOwnBlock(line);
    }
    return !startsOwnBlock(line);
}

// Join each block's wrapped lines onto its first line, keeping a note of where
// every source line's text ended up so a caret or a selection comes out of the
// rewrite on the same word.
function unwrapLines(lines, atDocumentStart) {
    var out = [];
    var map = [];
    var fence = null;
    // Front matter is not Markdown, so whatever is between the fences keeps
    // its own lines.
    var frontMatter = atDocumentStart && lines.length > 0 && FRONT_MATTER_RE.test(lines[0]);
    var kind = null;
    var previous = "";

    function keep(line) {
        map.push({ line: out.length, base: 0, strip: 0 });
        out.push(line);
        kind = blockKind(line);
        previous = line;
    }

    for (var i = 0; i < lines.length; i++) {
        var line = lines[i];

        if (frontMatter) {
            keep(line);
            kind = null;
            if (i > 0 && FRONT_MATTER_RE.test(line))
                frontMatter = false;
            continue;
        }

        var fenceMatch = line.match(FENCE_RE);
        if (fence !== null) {
            keep(line);
            kind = null;
            // A fence closes on a run of the same character at least as long
            // as the one that opened it.
            if (fenceMatch && fenceMatch[1].charAt(0) === fence.charAt(0)
                    && fenceMatch[1].length >= fence.length)
                fence = null;
            continue;
        }
        if (fenceMatch) {
            fence = fenceMatch[1];
            keep(line);
            kind = null;
            continue;
        }

        if (kind !== null && continuesBlock(kind, previous, line)) {
            var index = out.length - 1;
            var head = out[index].replace(/[ \t\r]+$/, "");
            var tail = line.replace(kind === "quote" ? QUOTE_MARKER_RE : /^[ \t]*/, "");
            map.push({ line: index, base: head.length + 1, strip: line.length - tail.length });
            out[index] = head + " " + tail;
            previous = line;
            continue;
        }

        keep(line);
    }

    return { lines: out, map: map };
}

// Where a document position lands once its line has been joined onto another:
// the same distance into its own text, which the join moved but did not cut.
function unwrappedPosition(doc, region, result, position) {
    var line = Math.max(region.start, Math.min(region.end, lineIndexAt(doc, position)));
    var entry = result.map[line - region.start];
    var column = position - doc.starts[line];
    var mapped = entry.base + Math.max(0, column - entry.strip);
    return offsetInLines(result.lines, entry.line,
                         Math.min(mapped, result.lines[entry.line].length));
}

// With a selection, the selected lines are unwrapped; without one the whole
// document is, which is what a pasted file needs and what a single undo puts
// back.
function unwrapPlan(text, selectionStart, selectionEnd) {
    var from = Math.min(selectionStart, selectionEnd);
    var to = Math.max(selectionStart, selectionEnd);
    var doc = documentLines(text);
    var region;
    if (from === to) {
        region = { start: 0, end: doc.lines.length - 1 };
    } else {
        var firstLine = lineIndexAt(doc, from);
        var lastLine = lineIndexAt(doc, to);
        // A selection ending exactly at a line start stops on the line before it.
        if (lastLine > firstLine && doc.starts[lastLine] === to)
            lastLine--;
        region = { start: firstLine, end: lastLine };
    }

    var result = unwrapLines(doc.lines.slice(region.start, region.end + 1),
                             region.start === 0);
    var plan = linesPlan(doc, region, result.lines,
                         unwrappedPosition(doc, region, result, from),
                         unwrappedPosition(doc, region, result, to));
    if (plan === null)
        return null;
    plan.joinedLines = (region.end - region.start + 1) - result.lines.length;
    return plan;
}
