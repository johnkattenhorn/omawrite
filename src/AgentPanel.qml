import QtQuick
import QtQuick.Controls

// The dock Claude answers in, on the right of the writing. It holds no state
// of its own beyond what is being typed: the conversation, whether a turn is
// running and what it is doing all come from the AgentSession behind it.
Item {
    id: root

    property bool expanded: false
    property bool darkMode: true
    property real textScale: 1
    property color pageColor: darkMode ? "#101010" : "#ffffff"
    property color textColor: darkMode ? "#eeeeee" : "#222324"
    property color mutedColor: darkMode ? "#909191" : "#aeb1b5"
    property color accentColor: "#428bca"
    property color selectionFill: "#186a9a"

    property var messages: []
    property bool running: false
    property bool available: true
    property string activity: ""
    property string documentName: ""

    // Logical, like every other dimension here, so a dragged width survives a
    // change of desktop text size.
    property int logicalWidth: 380
    readonly property int defaultLogicalWidth: 380
    property int minimumLogicalWidth: 280
    property int maximumLogicalWidth: 720

    signal asked(string question)
    signal interrupted()
    signal newChatRequested()
    signal widthChangeRequested(int width)
    signal widthCommitted()
    signal dismissed()

    width: expanded ? Math.round(logicalWidth * root.textScale) : 0
    visible: width > 0
    clip: true

    onExpandedChanged: if (expanded) Qt.callLater(focusInput)

    function focusInput() {
        input.forceActiveFocus();
    }

    function submit() {
        var question = input.text.trim();
        if (question.length === 0 || root.running)
            return;
        input.clear();
        root.asked(question);
    }

    // Seconds spent on the turn in front of you, because a silent panel and a
    // working one look the same otherwise.
    property int elapsed: 0

    onRunningChanged: {
        elapsed = 0;
        if (running)
            Qt.callLater(scrollToEnd);
    }

    onMessagesChanged: Qt.callLater(scrollToEnd)

    function scrollToEnd() {
        history.positionViewAtEnd();
    }

    Timer {
        running: root.running
        interval: 1000
        repeat: true
        onTriggered: root.elapsed += 1
    }

    Rectangle {
        anchors.fill: parent
        color: root.pageColor
    }

    Rectangle {
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.mutedColor
        opacity: resizeHandle.containsMouse || resizeHandle.pressed ? 0.6 : 0.25
    }

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 13
        anchors.rightMargin: 12
        height: Math.round(40 * root.textScale)

        Label {
            anchors.left: parent.left
            anchors.right: headerActions.left
            anchors.rightMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            text: root.documentName.length > 0 ? "Claude · " + root.documentName : "Claude"
            color: root.mutedColor
            elide: Text.ElideMiddle
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(12 * root.textScale)
        }

        Row {
            id: headerActions
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            spacing: 12

            Label {
                objectName: "agentNewChat"
                text: "new"
                color: newChatArea.containsMouse ? root.textColor : root.mutedColor
                opacity: root.messages.length > 0 ? 1 : 0.35
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(11 * root.textScale)

                MouseArea {
                    id: newChatArea
                    anchors.centerIn: parent
                    width: parent.width + 12
                    height: 24
                    hoverEnabled: true
                    enabled: root.messages.length > 0
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.newChatRequested()
                }
            }

            Label {
                objectName: "agentClose"
                text: "×"
                color: closeArea.containsMouse ? root.textColor : root.mutedColor
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(14 * root.textScale)

                MouseArea {
                    id: closeArea
                    anchors.centerIn: parent
                    width: 24
                    height: 24
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.dismissed()
                }
            }
        }
    }

    // The conversation sits on the floor of the panel with older turns above
    // it, so the newest answer is where the eye already is: next to the input.
    Item {
        id: historyArea
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: composer.top
        anchors.leftMargin: 13
        anchors.rightMargin: 12

        ListView {
            id: history
            objectName: "agentHistory"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.min(contentHeight, parent.height)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: root.messages
            spacing: Math.round(12 * root.textScale)
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            delegate: Item {
                id: turn

                required property var modelData

                readonly property bool fromWriter: turn.modelData.role === "you"
                readonly property bool trouble: turn.modelData.role === "trouble"

                width: ListView.view.width
                height: bubble.height

                Rectangle {
                    id: bubble
                    width: parent.width
                    height: turnText.implicitHeight + (turn.fromWriter ? Math.round(16 * root.textScale) : 0)
                    radius: 6
                    color: turn.fromWriter ? root.selectionFill : "transparent"
                    opacity: turn.fromWriter ? 0.18 : 1

                    TextEdit {
                        id: turnText
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.margins: turn.fromWriter ? Math.round(8 * root.textScale) : 0
                        anchors.leftMargin: turn.fromWriter ? Math.round(10 * root.textScale) : 0
                        readOnly: true
                        selectByMouse: true
                        // Plain text on purpose: an answer is words, and rich
                        // text would let one reach for a remote image.
                        textFormat: TextEdit.PlainText
                        wrapMode: TextEdit.Wrap
                        text: (turn.fromWriter ? "› " : "") + turn.modelData.text
                        color: turn.trouble ? root.accentColor : root.textColor
                        opacity: turn.trouble ? 0.9 : 1
                        selectionColor: root.selectionFill
                        selectedTextColor: root.textColor
                        font.family: "iA Writer Mono S"
                        font.pixelSize: Math.round(13 * root.textScale)
                    }
                }
            }
        }

        Label {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            wrapMode: Text.Wrap
            visible: root.messages.length === 0
            text: root.available
                ? "Ask about the document in front of you. Claude reads this folder, and the editor answers omawrite --read while you type."
                : "The Claude command line is not installed, so there is nothing here to ask."
            color: root.mutedColor
            opacity: 0.7
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(12 * root.textScale)
        }
    }

    Item {
        id: composer
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: 13
        anchors.rightMargin: 12
        anchors.bottomMargin: Math.round(12 * root.textScale)
        height: statusRow.height + inputBox.height + Math.round(8 * root.textScale)

        Item {
            id: statusRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: root.running ? Math.round(20 * root.textScale) : 0
            visible: root.running

            Label {
                objectName: "agentActivity"
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                text: (root.activity.length > 0 ? root.activity : "Working")
                      + " " + root.elapsed + "s"
                color: root.mutedColor
                elide: Text.ElideRight
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(11 * root.textScale)
            }

            Label {
                objectName: "agentStop"
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                text: "stop"
                color: stopArea.containsMouse ? root.textColor : root.mutedColor
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(11 * root.textScale)

                MouseArea {
                    id: stopArea
                    anchors.centerIn: parent
                    width: parent.width + 12
                    height: 22
                    hoverEnabled: true
                    cursorShape: Qt.PointingHandCursor
                    onClicked: root.interrupted()
                }
            }
        }

        Rectangle {
            id: inputBox
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: Math.min(Math.round(160 * root.textScale),
                             Math.max(Math.round(34 * root.textScale),
                                      input.implicitHeight + Math.round(12 * root.textScale)))
            radius: 6
            color: "transparent"
            border.width: 1
            border.color: root.mutedColor
            opacity: input.activeFocus ? 0.85 : 0.5

            TextArea {
                id: input
                objectName: "agentInput"
                anchors.fill: parent
                anchors.margins: Math.round(6 * root.textScale)
                anchors.leftMargin: Math.round(8 * root.textScale)
                background: null
                padding: 0
                enabled: root.available
                wrapMode: TextArea.Wrap
                placeholderText: root.running ? "Working..." : "Ask Claude"
                placeholderTextColor: root.mutedColor
                color: root.textColor
                selectionColor: root.selectionFill
                selectedTextColor: root.textColor
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(13 * root.textScale)

                // Enter sends and Shift+Enter is a newline, the way every other
                // message box works. Escape stops a turn if one is running, and
                // otherwise hands the writing back.
                Keys.onPressed: function(event) {
                    if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                        if (event.modifiers & Qt.ShiftModifier)
                            return;
                        root.submit();
                        event.accepted = true;
                    } else if (event.key === Qt.Key_Escape) {
                        if (root.running)
                            root.interrupted();
                        else
                            root.dismissed();
                        event.accepted = true;
                    }
                }
            }
        }
    }

    MouseArea {
        id: resizeHandle
        objectName: "agentResizeHandle"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 6
        hoverEnabled: true
        cursorShape: Qt.SplitHCursor

        property real grabOffset: 0

        onPressed: function(mouse) {
            root.beginResize(mapToItem(root, mouse.x, mouse.y).x);
        }
        onPositionChanged: function(mouse) {
            if (pressed)
                root.resizeTo(mapToItem(root, mouse.x, mouse.y).x);
        }
        onReleased: root.widthCommitted()
        // A width dragged somewhere unhelpful is one gesture from the one it
        // started at.
        onDoubleClicked: {
            root.widthChangeRequested(root.defaultLogicalWidth);
            root.widthCommitted();
        }
    }

    // The handle rides the edge it moves, so measure from the right edge, which
    // stays put while the left one is being dragged.
    function beginResize(pointerX) {
        resizeHandle.grabOffset = pointerX;
    }

    function resizeTo(pointerX) {
        requestLogicalWidth((root.width - pointerX + resizeHandle.grabOffset) / root.textScale);
    }

    function requestLogicalWidth(width) {
        var clamped = Math.max(root.minimumLogicalWidth,
                               Math.min(root.maximumLogicalWidth, Math.round(width)));
        if (clamped !== root.logicalWidth)
            root.widthChangeRequested(clamped);
    }
}
