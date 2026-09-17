import QtCore
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import QtQuick.Window
import "EditorMutations.js" as EditorMutations

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: backend.fileName + " - Omawrite"

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color textColor: backend.themeForeground
    readonly property color strongTextColor: backend.themeForeground
    readonly property color mutedColor: darkMode ? "#909191" : "#aeb1b5"
    readonly property color selectionFill: backend.themeSelection
    // The desktop's text size knob (GNOME's text-scaling-factor, which
    // `omarchy display text size` drives) anchored so its 12px default leaves
    // the app at the sizes it was designed around.
    readonly property real textScale: backend.textScale
    readonly property int editorFontPixelSize: scaledSize(backend.editorFontSize)
    // Never wider than the Flickable's viewport, whatever the measure asks for:
    // a tiling compositor can resize the window below its minimum width.
    readonly property int availableEditorWidth: Math.min(
        Math.max(360, width - fileSidebar.width
                 - Math.round(writerFontMetrics.averageCharacterWidth * 20)),
        Math.max(0, width - fileSidebar.width - 48))
    readonly property int editorWidth: layoutSettings.editorColumns > 0
        ? Math.min(Math.round(writerFontMetrics.averageCharacterWidth
                              * Math.max(20, layoutSettings.editorColumns)),
                   availableEditorWidth)
        : availableEditorWidth
    property bool searchOpen: false
    property bool sidebarOpen: false
    property int sidebarLogicalWidth: 240
    property bool searchUpdating: false
    property var searchMatches: []
    property int searchMatchIndex: -1
    property bool replaceOpen: false
    property bool keyboardWaitingForDialog: false
    property bool closeAnyway: false
    property bool previewVisible: false

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: backend.themeAccent
    color: pageColor

    // Editor measure in average character widths. 65, the default, is the
    // measure Omawrite has always had; 0 lets the text fill the window instead,
    // keeping ten characters of margin on either side.
    Settings {
        id: layoutSettings
        category: "layout"
        property int editorColumns: 65
    }

    // If the work reached neither its file nor a draft there is nowhere left
    // to put it, so the first close is refused and says so; a second one is
    // taken as meaning it.
    onClosing: function(close) {
        if (closeAnyway || backend.saveBeforeClosing()) {
            backend.prepareForApplicationClose();
            return;
        }
        close.accepted = false;
        closeAnyway = true;
    }
    onActiveChanged: if (!active) backend.saveNow()

    function setSidebarOpen(open) {
        sidebarOpen = open;
        if (open)
            fileSidebar.focusList();
        else
            editor.forceActiveFocus();
    }

    function toggleSidebar() {
        setSidebarOpen(!sidebarOpen);
    }

    function requestOpen(url) {
        // Refuse to swap the document out from under work that could not be
        // written; the status line says why. Closing still goes through, on
        // the recovery draft saveBeforeLeaving leaves behind.
        if (!backend.saveBeforeLeaving())
            return;
        backend.open(url);
    }

    // Autosave means a tab never holds work its file does not, so closing one
    // writes it on the way out rather than asking about it.
    function requestCloseTab() {
        if (backend.saveBeforeLeaving())
            backend.closeActiveBuffer();
    }

    function scrollTabs(direction) {
        tabFlick.contentX = Math.max(0, Math.min(tabFlick.contentWidth - tabFlick.width,
                                                  tabFlick.contentX + direction * tabFlick.width * 0.75));
    }

    function selectAdjacentTab(direction) {
        if (backend.buffers.length < 2)
            return;

        for (var index = 0; index < backend.buffers.length; ++index) {
            if (backend.buffers[index].id !== backend.activeBufferId)
                continue;
            backend.selectBuffer(backend.buffers[(index + direction + backend.buffers.length)
                                                 % backend.buffers.length].id);
            return;
        }
    }

    function ensureActiveTabVisible() {
        for (var index = 0; index < backend.buffers.length; ++index) {
            if (backend.buffers[index].id !== backend.activeBufferId)
                continue;

            var tab = tabRepeater.itemAt(index);
            if (!tab) {
                activeTabVisibilityTimer.restart();
                return;
            }

            if (tab.x < tabFlick.contentX) {
                tabFlick.contentX = tab.x;
                return;
            }

            if (tab.x + tab.width > tabFlick.contentX + tabFlick.width)
                tabFlick.contentX = tab.x + tab.width - tabFlick.width;
            return;
        }
    }

    // A closing modal hands focus back to whatever held it before it opened,
    // so wait it out rather than race it.
    function handKeyboardToEditor() {
        if (externalChangeDialog.visible) {
            keyboardWaitingForDialog = true;
            return;
        }
        editor.forceActiveFocus();
    }

    function releaseKeyboardAfterDialog() {
        if (!keyboardWaitingForDialog)
            return;
        keyboardWaitingForDialog = false;
        editor.forceActiveFocus();
    }

    // The editor places the caret item when the cursor moves and never again
    // while the text is re-laid out under it — which is what loading a
    // document does, after the caret has been placed.
    property bool settlingCaret: false

    Component {
        id: caretShape

        Rectangle {
            width: 1
            color: win.strongTextColor
            opacity: editor.activeFocus ? 1 : 0
            x: editor.cursorRectangle.x
            y: editor.cursorRectangle.y
            height: editor.cursorRectangle.height
        }
    }

    function settleCaret() {
        if (!settlingCaret)
            return;

        // A fresh delegate is built against the finished text. Both
        // assignments land in one turn, so no frame is drawn without a caret.
        editor.cursorDelegate = null;
        editor.cursorDelegate = caretShape;
        editorFlick.ensureCursorVisible();
    }

    // The net for a relayout that arrives after the load is announced.
    Timer {
        id: caretSettleWindow
        interval: 400

        onTriggered: win.settlingCaret = false
    }

    FontMetrics {
        id: writerFontMetrics
        font.family: "iA Writer Mono S"
        font.pixelSize: win.editorFontPixelSize
    }

    // Every hardcoded size in the interface is expressed at text scale 1.
    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * win.textScale));
    }

    function followLinkAt(position) {
        var link = backend.linkAt(position)
        if (link.kind === "url") {
            backend.openExternalUrl(link.url)
            return true
        }
        if (link.kind === "file") {
            requestOpen(link.url)
            return true
        }
        if (link.kind === "missing") {
            backend.notifyMissingNote(link.target)
            return false
        }
        return false
    }

    function toggleFullScreen() {
        win.visibility = win.visibility === Window.FullScreen
            ? Window.Windowed
            : Window.FullScreen;
    }

    // Where the viewport sits through the document, 0 at the top and 1 at the
    // bottom. The two surfaces are different heights, so the place in the
    // writing is the only thing they can agree on.
    function scrollFraction() {
        var span = editorFlick.contentHeight - editorFlick.height;
        return span > 0 ? Math.max(0, Math.min(1, editorFlick.contentY / span)) : 0;
    }

    function scrollToFraction(fraction) {
        var span = editorFlick.contentHeight - editorFlick.height;
        editorFlick.scrollTo(span > 0 ? fraction * span : 0);
    }

    // What to show for the link under the pointer. A wikilink reads as it was
    // written; a missing one says so rather than showing a path that is not
    // there; everything else shows where it actually goes.
    function linkLabel(link) {
        if (!link || link.kind === "none")
            return "";
        if (link.kind === "missing")
            return "[[" + link.target + "]] — no note yet";
        if (link.target && String(link.target).length > 0
                && link.kind === "file" && link.url !== undefined)
            return "[[" + link.target + "]]";
        var destination = link.url === undefined ? "" : String(link.url);
        return destination.indexOf("file://") === 0
            ? destination.substring(7)
            : destination;
    }

    function togglePreview() {
        // Read before the swap, applied after: toggling used to drop the reader
        // at the top of the document, which on anything long means finding your
        // place again every time you look.
        var fraction = scrollFraction();
        previewVisible = !previewVisible;
        previewTimer.stop();
        if (previewVisible) {
            backend.setPreviewWidth(preview.width);
            backend.setPreviewMarkdown(editor.text);
        } else {
            editor.forceActiveFocus();
        }
        // The surface that just appeared has not been laid out yet, so its
        // height is only known once Qt has been round the loop.
        previewScrollRestore.fraction = fraction;
        previewScrollRestore.restart();
    }

    // Applied a beat after the swap: the surface that just became visible has
    // not been through a layout pass, so its height is not there to scroll
    // against until Qt has been round the loop once.
    Timer {
        id: previewScrollRestore
        property real fraction: 0
        interval: 0
        repeat: false
        onTriggered: win.scrollToFraction(fraction)
    }

    Timer {
        id: previewTimer
        interval: 150
        onTriggered: backend.setPreviewMarkdown(editor.text)
    }

    function updateSearch() {
        var matches = [];
        var query = searchField.text;
        if (query.length > 0) {
            var haystack = editor.text.toLocaleLowerCase();
            var needle = query.toLocaleLowerCase();
            var position = 0;
            while ((position = haystack.indexOf(needle, position)) !== -1) {
                matches.push(position);
                position += Math.max(1, needle.length);
            }
        }
        searchMatches = matches;
        searchMatchIndex = matches.length > 0 ? 0 : -1;
        showSearchMatch();
    }

    function showSearchMatch() {
        var start = searchMatchIndex >= 0 ? searchMatches[searchMatchIndex] : -1;
        searchUpdating = true;
        backend.setSearchHighlight(searchField.text, start);
        if (start >= 0) {
            editor.select(start, start + searchField.text.length);
            editorFlick.ensureCursorVisible();
        }
        searchUpdating = false;
    }

    function moveSearch(direction) {
        if (searchMatches.length === 0)
            return;
        searchMatchIndex = (searchMatchIndex + direction + searchMatches.length)
                           % searchMatches.length;
        showSearchMatch();
    }

    function closeSearch() {
        searchOpen = false;
        searchUpdating = true;
        backend.setSearchHighlight("", -1);
        editor.deselect();
        searchUpdating = false;
        replaceOpen = false;
        editor.forceActiveFocus();
    }

    function restoreActiveCursor() {
        activeBufferRestoreTimer.restart();
    }

    Timer {
        id: activeBufferRestoreTimer
        interval: 100
        repeat: false
        onTriggered: {
            if (editor.text !== backend.activeBufferText) {
                restart();
                return;
            }
            editor.cursorPosition = backend.activeCursorPosition;
            editorFlick.ensureCursorVisible();
            backend.finishActiveBufferRestore();
        }
    }

    Timer {
        id: activeTabVisibilityTimer
        interval: 0
        repeat: false
        onTriggered: win.ensureActiveTabVisible()
    }

    Connections {
        target: backend
        function onActiveBufferIdChanged() {
            activeTabVisibilityTimer.restart();
        }
    }

    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.ApplicationShortcut
        onActivated: backend.save()
    }

    Shortcut {
        objectName: "newTabShortcut"
        sequence: "Ctrl+T"
        context: Qt.WindowShortcut
        onActivated: backend.newBuffer()
    }

    Shortcut {
        objectName: "closeTabShortcut"
        sequence: "Ctrl+W"
        context: Qt.WindowShortcut
        onActivated: win.requestCloseTab()
    }

    Shortcut {
        objectName: "nextTabShortcut"
        sequence: "Ctrl+Tab"
        context: Qt.WindowShortcut
        onActivated: win.selectAdjacentTab(1)
    }

    Shortcut {
        objectName: "previousTabShortcut"
        sequence: "Ctrl+Shift+Tab"
        context: Qt.WindowShortcut
        onActivated: win.selectAdjacentTab(-1)
    }

    Shortcut {
        objectName: "moveTabLeftShortcut"
        sequence: "Ctrl+Shift+PgUp"
        context: Qt.WindowShortcut
        onActivated: backend.moveActiveBuffer(-1)
    }

    Shortcut {
        objectName: "moveTabRightShortcut"
        sequence: "Ctrl+Shift+PgDown"
        context: Qt.WindowShortcut
        onActivated: backend.moveActiveBuffer(1)
    }

    Shortcut {
        sequence: "Ctrl+H"
        context: Qt.ApplicationShortcut
        onActivated: {
            searchOpen = true;
            replaceOpen = true;
            searchField.forceActiveFocus();
            searchField.selectAll();
        }
    }

    Shortcut {
        sequences: ["Ctrl++", "Ctrl+="]
        context: Qt.ApplicationShortcut
        onActivated: backend.editorFontSize += 2
    }

    Shortcut {
        sequence: "Ctrl+-"
        context: Qt.ApplicationShortcut
        onActivated: backend.editorFontSize -= 2
    }

    Shortcut {
        sequence: "Ctrl+0"
        context: Qt.ApplicationShortcut
        onActivated: backend.resetEditorFontSize()
    }

    Shortcut {
        sequence: "Ctrl+B"
        context: Qt.WindowShortcut
        onActivated: editor.wrapSelection("**", "**")
    }

    Shortcut {
        sequence: "Ctrl+I"
        context: Qt.WindowShortcut
        onActivated: editor.wrapSelection("*", "*")
    }

    Shortcut {
        sequence: "Ctrl+Shift+X"
        context: Qt.WindowShortcut
        onActivated: editor.wrapSelection("~~", "~~")
    }

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.WindowShortcut
        onActivated: editor.insertLink()
    }

    Shortcut {
        sequence: "Ctrl+L"
        context: Qt.WindowShortcut
        onActivated: editor.toggleCheckbox()
    }

    Shortcut {
        sequences: ["Ctrl+Return", "Ctrl+Enter"]
        context: Qt.WindowShortcut
        onActivated: followLinkAt(editor.cursorPosition)
    }

    Shortcut {
        sequence: "Ctrl+?"
        context: Qt.ApplicationShortcut
        onActivated: shortcutsDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+E"
        context: Qt.ApplicationShortcut
        onActivated: win.toggleSidebar()
    }

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.ApplicationShortcut
        onActivated: backend.openDialog()
    }

    Shortcut {
        objectName: "newWindowShortcut"
        sequence: "Ctrl+N"
        context: Qt.WindowShortcut
        onActivated: backend.newWindow()
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        context: Qt.ApplicationShortcut
        onActivated: backend.saveAsDialog()
    }

    Shortcut {
        sequence: "Ctrl+P"
        context: Qt.ApplicationShortcut
        onActivated: backend.printDocument()
    }

    Shortcut {
        sequence: "Ctrl+Shift+P"
        context: Qt.ApplicationShortcut
        onActivated: win.togglePreview()
    }

    Shortcut {
        sequences: ["Meta+F", "F11"]
        context: Qt.ApplicationShortcut
        onActivated: toggleFullScreen()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        context: Qt.WindowShortcut
        onActivated: editor.undo()
    }

    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        context: Qt.WindowShortcut
        onActivated: editor.redo()
    }

    Shortcut {
        sequence: "Ctrl+F"
        context: Qt.ApplicationShortcut
        onActivated: {
            searchOpen = true;
            searchField.forceActiveFocus();
            searchField.selectAll();
        }
    }

    Shortcut {
        sequence: "Ctrl+G"
        context: Qt.ApplicationShortcut
        enabled: win.searchOpen
        onActivated: win.moveSearch(1)
    }

    Shortcut {
        sequence: "Ctrl+Shift+T"
        context: Qt.ApplicationShortcut
        onActivated: {
            backend.updateCursorPosition(editor.cursorPosition);
            backend.toggleFocusMode();
        }
    }

    Connections {
        target: backend

        function onPreviewChanged() {
            if (!win.previewVisible)
                return;
            var contentY = editorFlick.contentY;
            Qt.callLater(function() { editorFlick.scrollTo(editorFlick.clampContentY(contentY)); });
        }

        function onOpenDialogRequested() {
            openFileDialog.open();
        }

        function onSaveDialogRequested(suggestedUrl) {
            saveFileDialog.selectedFile = suggestedUrl;
            saveFileDialog.open();
        }

        function onDocumentLoaded() {
            editor.cursorPosition = editor.length;
            win.settlingCaret = true;
            win.settleCaret();
            caretSettleWindow.restart();
            win.handKeyboardToEditor();
        }

        function onExternalChangeDetected(deleted, locallyModified) {
            externalChangeDialog.deleted = deleted;
            externalChangeDialog.appeared = false;
            externalChangeDialog.locallyModified = locallyModified;
            externalChangeDialog.open();
        }

        function onExternalFileAppeared(locallyModified) {
            // This save is not going to happen, so whatever it was for cannot
            // follow it. Leaving the intent standing lets an unrelated save
            // minutes later close the window or open another document.
            win.awaitingPendingSave = false;
            win.pendingAction = "";
            externalChangeDialog.deleted = false;
            externalChangeDialog.appeared = true;
            externalChangeDialog.locallyModified = locallyModified;
            externalChangeDialog.open();
        }

        function onActiveBufferChanged() {
            win.restoreActiveCursor();
        }
    }

    Dialogs.FileDialog {
        id: openFileDialog
        title: "Open File"
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: win.requestOpen(selectedFile)
    }

    Dialogs.FileDialog {
        id: saveFileDialog
        title: "Save File"
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: backend.saveAs(selectedFile)
        onRejected: backend.fileDialogCanceled()
    }

    ExternalChangeDialog {
        id: externalChangeDialog
        objectName: "externalChangeDialog"
        darkMode: win.darkMode
        textScale: win.textScale
        textColor: win.textColor
        strongTextColor: win.strongTextColor
        containerWidth: win.width
        containerHeight: win.height

        onKeepRequested: backend.keepExternalVersion()
        onReloadRequested: backend.reloadFromDisk()
        onClosed: win.releaseKeyboardAfterDialog()
    }

    Dialog {
        id: shortcutsDialog
        modal: true
        title: "Keyboard shortcuts"
        standardButtons: Dialog.Close
        width: Math.min(win.scaledSize(380), win.width - 48)
        x: Math.round((win.width - width) / 2)
        y: Math.round((win.height - height) / 2)
        contentItem: Label {
            text: "Ctrl+S  Save\nCtrl+Shift+S  Save As\nCtrl+O  Open\nCtrl+E  Files\nCtrl+T  New Tab\nCtrl+W  Close Tab\nCtrl+Tab  Next Tab\nCtrl+Shift+Tab  Previous Tab\nCtrl+N  New Window\nCtrl+F  Find\nCtrl+H  Find and Replace\nCtrl+B  Bold\nCtrl+I  Italic\nCtrl+Shift+X  Strikethrough\nCtrl+K  Link\nCtrl+L  Checkbox\nCtrl+Click / Ctrl+Enter  Follow link or wikilink\nTab / Shift+Tab  Nest list item\nCtrl+Shift+P  Preview\nCtrl++ / Ctrl+-  Text size\nCtrl+0  Reset text size\nCtrl+P  Print\nF11 / Super+F  Fullscreen\nCtrl+?  Shortcuts\n\nIn the sidebar: Up/Down or j/k move, Enter opens,\nBackspace or h goes up, a new file, A new folder,\nEsc returns to writing"
            lineHeight: 1.5
        }
    }

    FileSidebar {
        id: fileSidebar
        objectName: "fileSidebar"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        expanded: win.sidebarOpen
        darkMode: win.darkMode
        textScale: win.textScale
        pageColor: win.pageColor
        textColor: win.textColor
        mutedColor: win.mutedColor
        accentColor: backend.themeAccent
        selectionFill: win.selectionFill
        folderUrl: backend.folderUrl
        folderName: backend.folderName
        folderHasParent: backend.folderHasParent
        entries: backend.folderEntries
        currentFileUrl: backend.fileUrl
        logicalWidth: win.sidebarLogicalWidth
        // Never let the panel squeeze the writing column below its minimum.
        maximumLogicalWidth: Math.max(minimumLogicalWidth,
                                      Math.round(win.width / win.textScale) - 420)

        onParentFolderRequested: backend.openParentFolder()
        onFolderRequested: function(folderUrl) { backend.setFolder(folderUrl); }
        // requestOpen guards unsaved work with the same dialog Ctrl+O uses.
        onFileRequested: function(fileUrl) { win.requestOpen(fileUrl); }
        onCreateDocumentRequested: function(name) {
            var created = backend.createDocument(name);
            if (created.toString() === "")
                return;
            win.requestOpen(created);
            fileSidebar.selectUrl(created);
        }
        onCreateFolderRequested: function(name) {
            var created = backend.createFolder(name);
            if (created.toString() !== "")
                fileSidebar.selectUrl(created);
        }
        onWidthChangeRequested: function(width) { win.sidebarLogicalWidth = width; }
        onWidthCommitted: backend.saveSidebarWidth(win.sidebarLogicalWidth)
        onDismissed: editor.forceActiveFocus()
    }

    Item {
        anchors.fill: parent
        anchors.leftMargin: fileSidebar.width

        Item {
            id: tabBar
            objectName: "tabBar"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 8
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            height: 28
            visible: backend.buffers.length > 1
            z: 2

            Flickable {
                id: tabFlick
                objectName: "tabFlick"
                anchors.fill: parent
                clip: true
                contentWidth: tabStrip.width
                contentHeight: height
                flickableDirection: Flickable.HorizontalFlick
                boundsBehavior: Flickable.StopAtBounds

                Row {
                    id: tabStrip
                    spacing: 4

                    Repeater {
                        id: tabRepeater
                        model: backend.buffers
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: tabLabel.implicitWidth + 20
                            height: 28
                            color: modelData.id === backend.activeBufferId
                                ? backend.themeAccent
                                : (win.darkMode ? "#252525" : "#e7e7e7")

                            Label {
                                id: tabLabel
                                anchors.centerIn: parent
                                text: (modelData.externalChanged ? "• " : "")
                                    + (modelData.modified ? "* " : "")
                                    + backend.bufferTitle(modelData, index)
                                color: modelData.id === backend.activeBufferId
                                    ? "white"
                                    : win.textColor
                                font.family: "iA Writer Mono S"
                                font.pixelSize: win.scaledSize(11)
                            }

                            MouseArea {
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.selectBuffer(modelData.id)
                            }
                        }
                    }
                }

                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(wheel) {
                        var delta = wheel.pixelDelta.x !== 0 ? wheel.pixelDelta.x : wheel.pixelDelta.y;
                        if (delta === 0)
                            delta = wheel.angleDelta.x !== 0 ? wheel.angleDelta.x : wheel.angleDelta.y;
                        win.scrollTabs(delta > 0 ? -1 : 1);
                        wheel.accepted = true;
                    }
                }
            }

            Rectangle {
                id: leftTabScroll
                objectName: "leftTabScroll"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: parent.height
                visible: tabFlick.contentX > 0
                color: win.darkMode ? "#181818" : "#f4f4f4"
                z: 1

                Label {
                    anchors.centerIn: parent
                    text: "‹"
                    color: win.textColor
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: win.scrollTabs(-1)
                }
            }

            Rectangle {
                id: rightTabScroll
                objectName: "rightTabScroll"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: 24
                height: parent.height
                visible: tabFlick.contentX + tabFlick.width < tabFlick.contentWidth
                color: win.darkMode ? "#181818" : "#f4f4f4"
                z: 1

                Label {
                    anchors.centerIn: parent
                    text: "›"
                    color: win.textColor
                }

                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: win.scrollTabs(1)
                }
            }
        }

        Flickable {
            id: editorFlick
            objectName: "editorViewport"
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footer.top
            anchors.leftMargin: 24
            anchors.rightMargin: 24
            clip: true
            contentWidth: width
            contentHeight: Math.max(height, (win.previewVisible ? preview : editor).y
                                    + (win.previewVisible ? preview.implicitHeight
                                                          : editor.implicitHeight) + 220)
            boundsBehavior: Flickable.StopAtBounds

            // Loading a document (recovery, or opening a file) jumps the
            // cursor to its saved position in the same call that attaches
            // it, which can scroll the viewport before the user has done
            // anything; suppress the linger for that one settling jump so
            // the bar doesn't flash on open.
            property bool settlingDocument: true
            ScrollBar.vertical: ScrollBar {
                policy: ScrollBar.AsNeeded
                // Wheel scrolling moves contentY directly rather than
                // flicking the Flickable, so the bar has to be told about
                // that activity; linger briefly after the last event.
                active: hovered || pressed || wheelScroll.running || scrollLinger.running
            }

            Timer {
                id: scrollLinger
                interval: 600
            }

            // Flickable turns a wheel notch into a flick sized by the small
            // application font, which crawls next to a browser. Reproduce
            // Chromium's wheel physics instead (cc::ScrollOffsetAnimationCurve):
            // each notch moves 3 lines of 40px towards a running target, the
            // animation gets shorter as the outstanding distance grows, and a
            // notch landing mid-animation carries the current velocity into
            // the new curve, so sustained spinning keeps picking up speed.
            readonly property real wheelStep: win.scaledSize(120)

            FrameAnimation {
                id: wheelScroll
                running: false

                property real startY: 0
                property real targetY: 0
                property real duration: 0.2
                // Cubic bezier easing; ease-in-out (0.42, 0, 0.58, 1) for a
                // fresh scroll, with y1 tilted on retarget so the curve's
                // initial slope matches the velocity it inherits.
                property real cx1: 0.42
                property real cy1: 0
                readonly property real cx2: 0.58
                readonly property real cy2: 1

                onTriggered: {
                    var x = elapsedTime / duration;
                    if (x >= 1) {
                        editorFlick.contentY = editorFlick.snapToPixel(targetY);
                        stop();
                        return;
                    }
                    editorFlick.contentY = editorFlick.snapToPixel(
                        startY + (targetY - startY) * curveY(solveCurve(x)));
                }

                function begin(from, to, dur, slope) {
                    startY = from;
                    targetY = to;
                    duration = dur;
                    cx1 = 0.42;
                    cy1 = 0.42 * Math.max(-1000, Math.min(1000, slope));
                    restart();
                }

                function retarget(newTarget) {
                    var s = solveCurve(Math.min(1, elapsedTime / duration));
                    var pos = startY + (targetY - startY) * curveY(s);
                    var delta = newTarget - pos;
                    if (Math.abs(delta) < 0.5) {
                        editorFlick.contentY = newTarget;
                        stop();
                        return;
                    }

                    var velocity = curveDY(s) / Math.max(1e-6, curveDX(s))
                        * (targetY - startY) / duration;
                    var dur = editorFlick.wheelDuration(delta);
                    // When already moving faster than the eased curve would,
                    // bound the duration by the time to target at the current
                    // velocity; the 2.5x covers the ease-out tail.
                    if (velocity !== 0 && delta / velocity > 0)
                        dur = Math.min(dur, delta / velocity * 2.5);
                    begin(pos, newTarget, dur, velocity * dur / delta);
                }

                // Cubic bezier through (0,0), (cx1,cy1), (cx2,cy2), (1,1),
                // evaluated by Newton-solving the curve parameter from x.
                function curveX(s) { return 3 * s * (1 - s) * ((1 - s) * cx1 + s * cx2) + s * s * s; }
                function curveY(s) { return 3 * s * (1 - s) * ((1 - s) * cy1 + s * cy2) + s * s * s; }
                function curveDX(s) { return 3 * (1 - s) * (1 - s) * cx1 + 6 * (1 - s) * s * (cx2 - cx1) + 3 * s * s * (1 - cx2); }
                function curveDY(s) { return 3 * (1 - s) * (1 - s) * cy1 + 6 * (1 - s) * s * (cy2 - cy1) + 3 * s * s * (1 - cy2); }

                function solveCurve(x) {
                    var s = x;
                    for (var i = 0; i < 8; ++i) {
                        var error = curveX(s) - x;
                        if (Math.abs(error) < 0.001)
                            break;
                        var d = curveDX(s);
                        if (Math.abs(d) < 1e-6)
                            break;
                        s = Math.max(0, Math.min(1, s - error / d));
                    }
                    return s;
                }
            }

            WheelHandler {
                // Wayland compositors route every pointer's scroll through
                // one seat device that Qt classifies as a touchpad, so the
                // device type cannot tell a mouse wheel from two-finger
                // scrolling. Distinguish by event shape instead: discrete
                // wheel notches arrive with only angleDelta set, while
                // finger scrolling carries pixel-precise pixelDelta.
                acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                onWheel: function(wheel) {
                    scrollLinger.restart();
                    if (wheel.pixelDelta.y !== 0)
                        editorFlick.scrollTo(editorFlick.clampContentY(editorFlick.contentY - wheel.pixelDelta.y));
                    else
                        editorFlick.scrollByWheel(wheel);
                    wheel.accepted = true;
                }
            }

            onMovementStarted: wheelScroll.stop()

            function scrollByWheel(wheel) {
                // High-resolution wheels report fractional notches; feed
                // those through the same animated path, like Chromium does
                // for every wheel-source event.
                var notches = wheel.angleDelta.y / 120;
                if (notches === 0)
                    return;

                if (wheelScroll.running) {
                    wheelScroll.retarget(clampContentY(wheelScroll.targetY - notches * wheelStep));
                    return;
                }

                var target = clampContentY(contentY - notches * wheelStep);
                if (target !== contentY)
                    wheelScroll.begin(contentY, target, wheelDuration(target - contentY), 0);
            }

            // Chromium's inverse-delta duration: 200ms for a single notch,
            // ramping down to 100ms once 480px are outstanding.
            function wheelDuration(delta) {
                var pixels = Math.abs(delta) / win.textScale;
                return Math.max(6, Math.min(12, 14 - pixels / 60)) / 60;
            }

            function clampContentY(y) {
                return Math.max(0, Math.min(Math.max(0, contentHeight - height), y));
            }

            // Whole device pixels keep natively hinted glyphs from
            // re-rasterizing mid-animation, which reads as shimmer.
            function snapToPixel(y) {
                return Math.round(y * Screen.devicePixelRatio) / Screen.devicePixelRatio;
            }

            // Jump to a position, abandoning any wheel animation still running.
            function scrollTo(y) {
                wheelScroll.stop();
                if (!settlingDocument)
                    scrollLinger.restart();
                contentY = snapToPixel(y);
            }

            // Keep the editing caret within the viewport so writing past the
            // bottom edge scrolls the page along with the text.
            function ensureCursorVisible() {
                if (backend.focusMode) {
                    var cursorCenter = editor.y + editor.cursorRectangle.y
                        + editor.cursorRectangle.height / 2;
                    scrollTo(clampContentY(cursorCenter - height / 2));
                    return;
                }
                var margin = win.editorFontPixelSize * 2;
                var cursorTop = editor.y + editor.cursorRectangle.y;
                var cursorBottom = cursorTop + editor.cursorRectangle.height;
                var maxContentY = Math.max(0, contentHeight - height);

                if (cursorBottom + margin > contentY + height)
                    scrollTo(Math.min(maxContentY, cursorBottom + margin - height));
                else if (cursorTop - margin < contentY)
                    scrollTo(Math.max(0, cursorTop - margin));
            }

            TextEdit {
                id: editor
                objectName: "sourceEditor"
                visible: !win.previewVisible
                // Whole pixels keep natively hinted glyphs crisp; pinning the
                // column to them elsewhere only makes it step when dragged.
                x: renderType === TextEdit.NativeRendering
                    ? Math.round((editorFlick.width - width) / 2)
                    : (editorFlick.width - width) / 2
                y: Math.max(72, Math.round(win.height * 0.05))
                width: win.editorWidth
                height: Math.max(editorFlick.height - y - 96, implicitHeight + 20)
                text: ""
                textFormat: TextEdit.PlainText
                wrapMode: TextEdit.Wrap
                selectByMouse: true
                persistentSelection: true
                activeFocusOnPress: true
                color: win.textColor
                selectedTextColor: win.strongTextColor
                selectionColor: win.selectionFill
                font.family: "iA Writer Mono S"
                font.pixelSize: win.editorFontPixelSize
                font.weight: Font.Normal
                property bool hoveringLink: false
                // Where the link under the pointer goes, shown above the footer
                // so a link can be read before it is followed.
                property string hoveredLinkLabel: ""
                // Native rendering hints glyphs to the pixel grid, which is
                // crispest at whole scale factors but misplaces and unevenly
                // rasterizes glyphs at fractional ones (and goes stale when
                // the compositor delivers the fractional scale after the
                // first frame). Fall back to Qt's scalable renderer there.
                renderType: Screen.devicePixelRatio % 1 === 0 ? TextEdit.NativeRendering : TextEdit.QtRendering
                cursorDelegate: caretShape
                onCursorRectangleChanged: editorFlick.ensureCursorVisible()
                onCursorPositionChanged: {
                    if (!backend.restoringActiveBuffer)
                        backend.updateActiveEditorState(cursorPosition, selectionStart, selectionEnd);
                }
                onSelectionStartChanged: {
                    if (!backend.restoringActiveBuffer)
                        backend.updateActiveEditorState(cursorPosition, selectionStart, selectionEnd);
                }
                onSelectionEndChanged: {
                    if (!backend.restoringActiveBuffer)
                        backend.updateActiveEditorState(cursorPosition, selectionStart, selectionEnd);
                }

                onContentSizeChanged: win.settleCaret()

                // TextEdit steals the pointer grab for selection, so TapHandler
                // never sees Ctrl+click. MouseArea can consume the press when it
                // hits a link and otherwise pass it through for caret/selection.
                MouseArea {
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton
                    hoverEnabled: true
                    propagateComposedEvents: true
                    preventStealing: true
                    cursorShape: editor.hoveringLink ? Qt.PointingHandCursor : Qt.IBeamCursor

                    function documentPosition(mx, my) {
                        return editor.positionAt(mx, my)
                    }

                    function updateHover(mx, my, modifiers) {
                        var link = backend.linkAt(documentPosition(mx, my))
                        // The cursor answers to Ctrl because Ctrl is what
                        // follows the link; the destination is worth reading
                        // either way, and reading it is what tells you whether
                        // to hold Ctrl at all.
                        editor.hoveringLink = (modifiers & Qt.ControlModifier)
                            && link.kind !== "none"
                        editor.hoveredLinkLabel = win.linkLabel(link)
                    }

                    onPositionChanged: function(mouse) {
                        updateHover(mouse.x, mouse.y, mouse.modifiers)
                    }

                    onExited: {
                        editor.hoveringLink = false
                        editor.hoveredLinkLabel = ""
                    }

                    onPressed: function(mouse) {
                        if ((mouse.modifiers & Qt.ControlModifier)
                                && followLinkAt(documentPosition(mouse.x, mouse.y))) {
                            mouse.accepted = true
                            return
                        }
                        mouse.accepted = false
                    }
                }

                function replaceSelectionWith(replacement) {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    EditorMutations.replaceRange(editor, start, end, replacement);
                }

                function wrapSelection(before, after) {
                    forceActiveFocus();
                    EditorMutations.toggleWrap(editor, before, after);
                }

                function insertLink() {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    var selected = text.slice(start, end);
                    var url = backend.clipboardUrl();
                    var label = selected.length > 0 ? selected : "link text";
                    var destination = url.length > 0 ? url : "https://";
                    var escapedLabel = escapeMarkdownLinkText(label);
                    var markdown = "[" + escapedLabel + "](" + escapeMarkdownLinkDestination(destination) + ")";
                    if (selected.length === 0) {
                        EditorMutations.replaceRange(editor, start, end, markdown,
                                                     1, 1 + escapedLabel.length);
                    } else if (url.length === 0) {
                        EditorMutations.replaceRange(editor, start, end, markdown,
                                                     escapedLabel.length + 3,
                                                     markdown.length - 1);
                    } else {
                        EditorMutations.replaceRange(editor, start, end, markdown);
                    }
                }

                function applyPlan(plan) {
                    EditorMutations.replaceRange(editor, plan.start, plan.end,
                                                 plan.replacement,
                                                 plan.selectionStartOffset,
                                                 plan.selectionEndOffset);
                }

                // Tab nests the list items the cursor or selection touches
                // under the item above them; Shift+Tab lifts them back out.
                // Outside a list Tab is left alone.
                function indentList(direction) {
                    var plan = EditorMutations.listIndentPlan(text, selectionStart,
                                                              selectionEnd, direction);
                    if (!plan)
                        return false;
                    applyPlan(plan);
                    return true;
                }

                property var checklistItems: []
                function rebuildChecklistItems() {
                    var items = [];
                    var offset = 0;
                    var lines = text.split("\n");
                    for (var i = 0; i < lines.length; ++i) {
                        var line = lines[i];
                        var match = line.match(/^(\s*[-+*]\s+)\[([ xX])\](?=\s|$)/);
                        if (match) {
                            items.push({
                                position: offset + match[1].length,
                                checked: match[2].toLowerCase() === "x"
                            });
                        }
                        offset += line.length + 1;
                    }
                    checklistItems = items;
                }

                function toggleCheckbox() {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    var lineStart = text.lastIndexOf("\n", Math.max(0, start - 1)) + 1;
                    var lineEnd = text.indexOf("\n", start);
                    if (lineEnd < 0)
                        lineEnd = text.length;
                    var line = text.slice(lineStart, lineEnd);
                    var match = line.match(/^(\s*)([-+*])(\s+)(?:\[([ xX])\])?(\s*)(.*)$/);
                    if (!match)
                        return;

                    var prefixLength = match[1].length + match[2].length + match[3].length;
                    var markerStart = lineStart + prefixLength;
                    var selectionLength = end - start;

                    if (match[4] !== undefined) {
                        var checked = match[4].toLowerCase() === "x";
                        var existingGap = match[5] || "";
                        var hasContent = (match[6] || "").length > 0;
                        var newGap = hasContent && existingGap.length === 0 ? " " : existingGap;
                        EditorMutations.replaceRange(editor, markerStart,
                                                     markerStart + 3,
                                                     (checked ? "[ ]" : "[x]"));
                    } else {
                        EditorMutations.replaceRange(editor, markerStart, markerStart,
                                                     "[ ] ");
                    }

                    cursorPosition = Math.min(text.length, start + selectionLength);
                    if (selectionLength > 0)
                        select(start, start + selectionLength);
                }

                function toggleCheckboxAt(position) {
                    var lineStart = text.lastIndexOf("\n", Math.max(0, position - 1)) + 1;
                    var lineEnd = text.indexOf("\n", position);
                    if (lineEnd < 0)
                        lineEnd = text.length;
                    var line = text.slice(lineStart, lineEnd);
                    var match = line.match(/^(\s*[-+*]\s+)\[([ xX])\](\s*)(.*)$/);
                    if (!match)
                        return;

                    var checkboxStart = lineStart + match[1].length;
                    var existingGap = match[3] || "";
                    var hasContent = (match[4] || "").length > 0;
                    var newGap = hasContent && existingGap.length === 0 ? " " : existingGap;
                    var oldSelectionStart = Math.min(selectionStart, selectionEnd);
                    var oldSelectionEnd = Math.max(selectionStart, selectionEnd);
                    EditorMutations.replaceRange(editor, checkboxStart,
                                                 checkboxStart + 3,
                                                 (match[2].toLowerCase() === "x" ? "[ ]" : "[x]"));
                    if (oldSelectionStart !== oldSelectionEnd)
                        select(oldSelectionStart, oldSelectionEnd);
                    else
                        cursorPosition = oldSelectionEnd;
                    forceActiveFocus();
                }

                function smartReturn(softBreak) {
                    if (softBreak) {
                        replaceSelectionWith("\n");
                        return;
                    }
                    // Lists and quotes carry themselves onto the next line;
                    // anywhere else Return starts a fresh paragraph.
                    var plan = EditorMutations.returnPlan(text, selectionStart, selectionEnd);
                    if (plan) {
                        applyPlan(plan);
                        return;
                    }

                    // A new paragraph stands apart from the one above it by a
                    // blank line, which is the second break here. A line that
                    // is already blank has nothing to stand apart from, so
                    // that break would only be a gap nobody asked for.
                    // The break lands on what the selection leaves behind, which
                    // is not the caret's line when it was dragged right to left.
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    var head = text.slice(text.lastIndexOf("\n", start - 1) + 1, start);
                    var lineEnd = text.indexOf("\n", end);
                    var rest = lineEnd < 0 ? text.slice(end) : text.slice(end, lineEnd);
                    replaceSelectionWith(/^\s*$/.test(head) && /^\s*$/.test(rest)
                                         ? "\n" : "\n\n");
                }

                function escapeMarkdownLinkText(linkText) {
                    return linkText.replace(/\\/g, "\\\\")
                                   .replace(/\[/g, "\\[")
                                   .replace(/\]/g, "\\]");
                }

                function escapeMarkdownLinkDestination(linkUrl) {
                    return linkUrl.replace(/\\/g, "\\\\")
                                  .replace(/\(/g, "\\(")
                                  .replace(/\)/g, "\\)");
                }

                function pasteClipboardUrlAsMarkdownLink() {
                    var start = Math.min(selectionStart, selectionEnd);
                    var end = Math.max(selectionStart, selectionEnd);
                    if (start === end)
                        return false;

                    var url = backend.clipboardUrl();
                    if (url === "")
                        return false;

                    var selected = text.slice(start, end);
                    var leading = selected.match(/^\s*/)[0];
                    var trailing = selected.match(/\s*$/)[0];
                    var linkText = selected.slice(leading.length,
                                                  selected.length - trailing.length);
                    if (linkText === "")
                        return false;

                    replaceSelectionWith(leading + "[" + escapeMarkdownLinkText(linkText) + "]("
                                         + escapeMarkdownLinkDestination(url) + ")" + trailing);
                    return true;
                }

                // An image on the clipboard is written beside the document and
                // referred to by a relative path, which is the only shape the
                // preview will load. Runs after the link paste, so a selection
                // with a URL behind it still becomes a link rather than a copy.
                function pasteClipboardImage() {
                    var path = backend.saveClipboardImage();
                    if (path === "")
                        return false;

                    replaceSelectionWith("![](" + escapeMarkdownLinkDestination(path) + ")");
                    return true;
                }

                function pasteClipboardAsPlainText() {
                    var pastedText = backend.clipboardText();
                    if (pastedText.length > 0)
                        replaceSelectionWith(pastedText);
                }

                function skipHiddenForward(position) {
                    var pos = position;
                    var ranges = backend.hiddenRangesAt(pos);
                    for (var i = 0; i < ranges.length; i++) {
                        if (pos >= ranges[i].start && pos < ranges[i].end) {
                            pos = ranges[i].end;
                            i = -1;
                        }
                    }
                    return pos;
                }

                function skipHiddenBackward(position) {
                    var pos = position;
                    var ranges = backend.hiddenRangesAt(pos);
                    for (var i = ranges.length - 1; i >= 0; i--) {
                        if (pos > ranges[i].start && pos <= ranges[i].end) {
                            pos = ranges[i].start;
                            i = ranges.length;
                        }
                    }
                    return pos;
                }

                function moveCursorVisibly(direction) {
                    if (selectionStart !== selectionEnd) {
                        cursorPosition = direction > 0
                            ? Math.max(selectionStart, selectionEnd)
                            : Math.min(selectionStart, selectionEnd);
                        return;
                    }

                    var pos = Math.max(0, Math.min(text.length, cursorPosition + direction));
                    cursorPosition = direction > 0
                        ? skipHiddenForward(pos)
                        : skipHiddenBackward(pos);
                }

                function movePage(direction, extendSelection) {
                    var pageStep = Math.max(win.editorFontPixelSize,
                                            editorFlick.height - win.editorFontPixelSize * 2);
                    var rect = cursorRectangle;
                    var targetY = rect.y + rect.height / 2 + direction * pageStep;
                    var target = positionAt(rect.x, Math.max(0, targetY));
                    if (extendSelection)
                        moveCursorSelection(target, TextEdit.SelectCharacters);
                    else
                        cursorPosition = target;
                }

                function deleteParagraphBreakBehindCursor() {
                    if (selectionStart !== selectionEnd || cursorPosition < 2)
                        return false;

                    if (text.slice(cursorPosition - 2, cursorPosition) !== "\n\n")
                        return false;

                    var start = cursorPosition - 2;
                    var lineEnd = text.indexOf("\n", cursorPosition);
                    var line = lineEnd < 0 ? text.slice(cursorPosition)
                                           : text.slice(cursorPosition, lineEnd);
                    // Something on the caret's line and the pair above it is a
                    // paragraph break and nothing else, either the one Return
                    // wrote to end a paragraph or the one the writer is now
                    // closing to join what it separates. Both breaks go.
                    if (/^\s*$/.test(line)) {
                        // On a blank line Return writes a single break, so up
                        // here the pair may be that break and one that was
                        // already in the document. Above a blank line Return
                        // writes a single break too, and so wrote neither of
                        // these.
                        var above = text.slice(text.lastIndexOf("\n", start - 1) + 1, start);
                        if (/^\s*$/.test(above))
                            return false;
                        // Past that the two Returns leave the same text and the
                        // same caret, and no reading of either says which was
                        // pressed. What decides instead is that the gap is left
                        // standing: both breaks go only while a blank line of
                        // it survives them. The end of the document below the
                        // caret leaves it none...
                        if (lineEnd < 0)
                            return false;
                        // ...and so does the next paragraph, which the pair
                        // would otherwise be pulled up against.
                        var belowEnd = text.indexOf("\n", lineEnd + 1);
                        var below = belowEnd < 0 ? text.slice(lineEnd + 1)
                                                 : text.slice(lineEnd + 1, belowEnd);
                        if (!/^\s*$/.test(below))
                            return false;
                    }

                    remove(start, cursorPosition);
                    cursorPosition = start;
                    return true;
                }

                Keys.priority: Keys.BeforeItem
                Keys.onPressed: function(event) {
                    var pasteKey = (event.key === Qt.Key_V)
                        && (event.modifiers & Qt.ControlModifier)
                        && !(event.modifiers & (Qt.AltModifier | Qt.MetaModifier | Qt.ShiftModifier));
                    var shiftInsert = (event.key === Qt.Key_Insert)
                        && (event.modifiers & Qt.ShiftModifier)
                        && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier));
                    if (pasteKey || shiftInsert) {
                        if (!pasteClipboardUrlAsMarkdownLink() && !pasteClipboardImage())
                            pasteClipboardAsPlainText();
                        event.accepted = true;
                        return;
                    }

                    var returnKey = event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
                    var commandModifier = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier);
                    if (returnKey && !commandModifier) {
                        smartReturn(event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    } else if (!commandModifier && event.key === Qt.Key_Backspace
                               && deleteParagraphBreakBehindCursor()) {
                        event.accepted = true;
                    } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                               && event.key === Qt.Key_Right) {
                        moveCursorVisibly(1);
                        event.accepted = true;
                    } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                               && event.key === Qt.Key_Left) {
                        moveCursorVisibly(-1);
                        event.accepted = true;
                    } else if (!commandModifier
                               && (event.key === Qt.Key_Tab || event.key === Qt.Key_Backtab)) {
                        var outdent = event.key === Qt.Key_Backtab
                            || (event.modifiers & Qt.ShiftModifier);
                        if (indentList(outdent ? -1 : 1))
                            event.accepted = true;
                    } else if (!commandModifier
                               && (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp)) {
                        movePage(event.key === Qt.Key_PageDown ? 1 : -1,
                                 event.modifiers & Qt.ShiftModifier);
                        event.accepted = true;
                    }
                }

                onTextChanged: {
                    rebuildChecklistItems();
                    if (win.searchUpdating)
                        return;
                    if (backend.restoringActiveBuffer) {
                        activeBufferRestoreTimer.restart();
                        return;
                    }
                    var contentChanged = backend.editorTextChanged();
                    backend.updateActiveEditorState(cursorPosition, selectionStart, selectionEnd);
                    if (contentChanged) {
                        // A failed close only confirms discarding the text that
                        // was on screen for that attempt. New writing must get
                        // its own chance to be saved.
                        win.closeAnyway = false;
                        win.settlingCaret = false;
                    }
                    if (win.searchOpen && contentChanged)
                        win.updateSearch();
                    if (win.previewVisible)
                        previewTimer.restart();
                }

                Repeater {
                    id: checklistRepeater
                    model: editor.checklistItems
                    delegate: Item {
                        required property var modelData
                        readonly property real checkboxSize: Math.max(12, editor.font.pixelSize * 0.72)
                        readonly property rect markerRect: {
                            // positionToRectangle() is a plain method call, not a
                            // tracked property, so this binding otherwise only
                            // re-evaluates when modelData changes. Loading a
                            // document reformats line-height right after the
                            // checklist items are first computed, which moves
                            // every line but doesn't touch modelData - reading
                            // implicitHeight (unused otherwise) gives the binding
                            // a real dependency on layout, so boxes correct
                            // themselves instead of staying misplaced until the
                            // next edit rebuilds the list.
                            var _layoutDependency = editor.implicitHeight;
                            return editor.positionToRectangle(modelData.position);
                        }
                        x: markerRect.x
                        y: markerRect.y + Math.max(0, (markerRect.height - checkboxSize) / 2)
                        width: checkboxSize
                        height: checkboxSize
                        z: 10

                        Rectangle {
                            anchors.fill: parent
                            radius: Math.max(2, width * 0.15)
                            color: modelData.checked ? win.textColor : "transparent"
                            border.width: 1
                            border.color: win.mutedColor

                            Text {
                                anchors.centerIn: parent
                                text: "\u2713"
                                visible: modelData.checked
                                color: win.pageColor
                                font.pixelSize: parent.height * 0.72
                                font.weight: Font.Bold
                                verticalAlignment: Text.AlignVCenter
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: editor.toggleCheckboxAt(modelData.position)
                        }
                    }
                }

                Text {
                    anchors.left: parent.left
                    anchors.top: parent.top
                    text: "# Start writing"
                    visible: editor.text.length === 0 && !editor.activeFocus
                    color: win.mutedColor
                    font.family: editor.font.family
                    font.pixelSize: editor.font.pixelSize
                    font.weight: editor.font.weight
                }

                Component.onCompleted: {
                    backend.attachDocument(textDocument);
                    win.restoreActiveCursor();
                    rebuildChecklistItems();
                    forceActiveFocus();
                    editorFlick.settlingDocument = false;
                }
            }

            TextEdit {
                id: preview
                objectName: "renderedPreview"
                x: Math.round((editorFlick.width - width) / 2)
                y: Math.max(42, Math.round(win.height * 0.05))
                width: win.editorWidth
                height: Math.max(editorFlick.height - y - 96, implicitHeight + 20)
                visible: win.previewVisible
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.Wrap
                color: win.textColor
                font.family: "iA Writer Mono S"
                font.pixelSize: win.editorFontPixelSize
                renderType: editor.renderType
                onWidthChanged: backend.setPreviewWidth(width)
                onLinkActivated: function(link) { backend.openExternalUrl(link) }
                Component.onCompleted: {
                    backend.attachPreviewDocument(textDocument);
                    backend.setPreviewWidth(width);
                }
            }
        }

        // Above the footer rather than inside it: the footer is a fixed row of
        // controls, and a path is as long as it is. Outside the editor's own
        // clip, so a link near the bottom edge is still readable.
        Rectangle {
            id: linkLabel
            objectName: "linkLabel"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: footer.top
            height: visible ? linkLabelText.implicitHeight + win.scaledSize(10) : 0
            visible: editor.hoveredLinkLabel.length > 0 && !win.previewVisible
            color: win.pageColor

            Text {
                id: linkLabelText
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 24
                anchors.rightMargin: 24
                text: editor.hoveredLinkLabel
                elide: Text.ElideMiddle
                color: win.mutedColor
                opacity: 0.75
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(11)
            }
        }

        Rectangle {
            id: footer
            objectName: "footer"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.max(36, win.scaledSize(36))
            color: win.pageColor

            Row {
                id: footerStatus
                anchors.left: parent.left
                anchors.bottom: parent.bottom
                anchors.leftMargin: 12
                anchors.bottomMargin: 10
                spacing: 12
                opacity: 0.55

                FooterIconButton {
                    objectName: "saveButton"
                    iconName: "save"
                    iconColor: win.mutedColor
                    tooltip: "Save"
                    onClicked: backend.save()
                }

                FooterIconButton {
                    objectName: "openButton"
                    iconName: "open"
                    iconColor: win.mutedColor
                    tooltip: "Open"
                    onClicked: backend.openDialog()
                }

                Label {
                    text: backend.status
                    color: win.mutedColor
                    font.family: "iA Writer Mono S"
                    font.pixelSize: win.scaledSize(11)
                    visible: text !== ""
                    elide: Text.ElideRight
                    width: Math.min(360, win.width / 3)
                    height: win.scaledSize(16)
                    verticalAlignment: Text.AlignVCenter
                }
            }

            FooterIconButton {
                objectName: "filesButton"
                iconName: "files"
                iconColor: win.mutedColor
                tooltip: "Files"
                onClicked: win.setSidebarOpen(!win.sidebarOpen)
            }

            FooterIconButton {
                objectName: "modeToggle"
                iconName: "preview"
                iconColor: win.mutedColor
                tooltip: win.previewVisible ? "Editor" : "Preview"
                onClicked: win.togglePreview()
            }

            Label {
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                anchors.rightMargin: 12
                anchors.bottomMargin: 10
                text: backend.wordCount + (backend.wordCount === 1 ? " Word" : " Words")
                color: win.mutedColor
                opacity: 0.75
                font.family: "iA Writer Mono S"
                font.pixelSize: win.scaledSize(11)
            }
        }

        Pane {
            anchors.top: parent.top
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.topMargin: 12
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            height: win.scaledSize(win.replaceOpen ? 104 : 56)
            visible: win.searchOpen
            z: 10
            leftPadding: 16
            rightPadding: 8
            topPadding: 0
            bottomPadding: 0
            Material.elevation: 8

            background: Rectangle {
                radius: 9
                color: win.darkMode ? "#22221f" : "#fffef2"
            }

            RowLayout {
                anchors.fill: parent
                spacing: 8

                Item {
                    Layout.fillWidth: true
                    Layout.fillHeight: true

                    TextInput {
                        id: searchField
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        height: win.replaceOpen ? parent.height / 2 : parent.height
                        verticalAlignment: TextInput.AlignVCenter
                        selectByMouse: true
                        color: win.textColor
                        selectionColor: win.selectionFill
                        selectedTextColor: win.strongTextColor
                        font.pixelSize: win.scaledSize(17)
                        clip: true
                        onTextChanged: win.updateSearch()
                        Keys.onReturnPressed: function(event) {
                            win.moveSearch((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
                            event.accepted = true;
                        }
                        Keys.onEscapePressed: function(event) {
                            win.closeSearch();
                            event.accepted = true;
                        }
                    }

                    TextInput {
                        id: replaceField
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: parent.height / 2
                        visible: win.replaceOpen
                        verticalAlignment: TextInput.AlignVCenter
                        color: win.textColor
                        selectionColor: win.selectionFill
                        selectedTextColor: win.strongTextColor
                        font.pixelSize: win.scaledSize(17)
                        Keys.onReturnPressed: replaceCurrentButton.clicked()
                    }

                    Label {
                        anchors.verticalCenter: replaceField.verticalCenter
                        text: "Replace with"
                        visible: win.replaceOpen && replaceField.text.length === 0
                        color: win.mutedColor
                        font.pixelSize: win.scaledSize(17)
                    }

                    Label {
                        anchors.verticalCenter: searchField.verticalCenter
                        text: "Find"
                        visible: searchField.text.length === 0
                        color: win.mutedColor
                        font.pixelSize: win.scaledSize(17)
                    }
                }

                Label {
                    Layout.preferredWidth: win.scaledSize(58)
                    Layout.fillHeight: true
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    text: win.searchMatches.length === 0
                        ? "0/0"
                        : (win.searchMatchIndex + 1) + "/" + win.searchMatches.length
                    color: win.darkMode ? win.textColor : "#62635f"
                    font.pixelSize: win.scaledSize(16)
                }

                Button {
                    id: replaceCurrentButton
                    visible: win.replaceOpen
                    text: "Replace"
                    onClicked: {
                        if (win.searchMatchIndex < 0) return;
                        var start = win.searchMatches[win.searchMatchIndex];
                        EditorMutations.replaceRange(editor, start,
                                                     start + searchField.text.length,
                                                     replaceField.text);
                        win.updateSearch();
                    }
                }

                Button {
                    visible: win.replaceOpen
                    text: "All"
                    onClicked: {
                        if (searchField.text.length === 0) return;
                        for (var i = win.searchMatches.length - 1; i >= 0; --i) {
                            var start = win.searchMatches[i];
                            EditorMutations.replaceRange(editor, start,
                                                         start + searchField.text.length,
                                                         replaceField.text);
                        }
                        win.updateSearch();
                    }
                }

                Rectangle {
                    Layout.preferredWidth: 1
                    Layout.preferredHeight: 34
                    color: win.darkMode ? "#6f6f62" : "#d5d56e"
                }

                SearchIconButton {
                    iconName: "up"
                    iconColor: win.darkMode ? win.textColor : "#62635f"
                    onClicked: win.moveSearch(-1)
                }

                SearchIconButton {
                    iconName: "down"
                    iconColor: win.darkMode ? win.textColor : "#62635f"
                    onClicked: win.moveSearch(1)
                }

                SearchIconButton {
                    iconName: "close"
                    iconColor: win.darkMode ? win.textColor : "#62635f"
                    onClicked: win.closeSearch()
                }
            }
        }
    }

    Component.onCompleted: {
        sidebarLogicalWidth = backend.sidebarWidth();
        var geometry = backend.windowGeometry();
        if (geometry.x >= 0) x = geometry.x;
        if (geometry.y >= 0) y = geometry.y;
        width = geometry.width;
        height = geometry.height;
        if (geometry.maximized) showMaximized();
    }

    Component.onDestruction: backend.saveWindowGeometry(x, y, width, height, visibility === Window.Maximized)

}
