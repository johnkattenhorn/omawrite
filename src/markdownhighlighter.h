#pragma once

#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>

class MarkdownHighlighter : public QSyntaxHighlighter {
    Q_OBJECT

public:
    explicit MarkdownHighlighter(QTextDocument *document);

    void setDarkMode(bool darkMode);
    void setColors(const QString &background, const QString &foreground, const QString &accent);
    void setSearch(const QString &query, int currentMatchStart);
    void setTextScale(qreal textScale);
    // The size a heading is drawn at, in points, for a document being written
    // at editorPixelSize. The preview asks the same question, so the answer
    // lives in one place rather than drifting between the two views.
    static qreal headingPointSize(qreal editorPixelSize, int level);
    void setFocusMode(bool enabled);
    void setFocusCursorPosition(int position);

    struct Span {
        int start;
        int length;
    };

    enum class InlineKind { Bold, Italic, Strikethrough, Link, WikiLink };

    struct InlineMarkup {
        InlineKind kind;
        Span content;
        Span markers[2];
        QString target;
    };

    struct Clickable {
        InlineKind kind;
        Span span;
        QString target;
    };

    static QList<Clickable> clickableSpans(const QString &text);

    // Single source of truth for inline markdown spans: the highlighter uses it
    // to style content and hide markers, and the editor uses it (via
    // Backend::hiddenRangesAt) to skip the caret over the hidden markers.
    static QList<InlineMarkup> inlineMarkup(const QString &text);

protected:
    void highlightBlock(const QString &text) override;

private:
    void rebuildFormats();
    void highlightMarkers(const QString &text);
    void highlightInline(const QString &text);
    void highlightSearch(const QString &text);
    void applyFocusDimming(const QString &text);

    bool m_darkMode = true;
    QString m_customBackground;
    QString m_customForeground;
    QString m_customAccent;
    QTextCharFormat m_markerFormat;
    QTextCharFormat m_hiddenMarkerFormat;
    QTextCharFormat m_checkboxMarkerFormat;
    QTextCharFormat m_checkedItemFormat;
    QTextCharFormat m_headingFormat;
    QTextCharFormat m_boldFormat;
    QTextCharFormat m_italicFormat;
    QTextCharFormat m_strikethroughFormat;
    QTextCharFormat m_codeFormat;
    QTextCharFormat m_quoteFormat;
    QTextCharFormat m_linkFormat;
    QString m_searchQuery;
    int m_currentMatchStart = -1;
    qreal m_textScale = 1.0;
    qreal m_headingSizes[6];
    QTextCharFormat m_searchFormat;
    QTextCharFormat m_currentSearchFormat;
    bool m_focusMode = false;
    int m_focusCursorPosition = -1;
    QColor m_dimmedColor;
};
