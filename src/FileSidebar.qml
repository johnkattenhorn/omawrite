import QtQuick
import QtQuick.Controls

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
    property string folderName: ""
    property bool folderHasParent: false
    property var entries: []
    property url currentFileUrl

    signal parentFolderRequested()
    signal folderRequested(url folderUrl)
    signal fileRequested(url fileUrl)

    readonly property int rowHeight: Math.round(26 * root.textScale)

    width: expanded ? Math.round(240 * root.textScale) : 0
    visible: width > 0
    clip: true

    // The editor sits behind the panel rather than beside it, so the
    // background has to be opaque to keep text from showing through.
    Rectangle {
        anchors.fill: parent
        color: root.pageColor
    }

    Rectangle {
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: 1
        color: root.mutedColor
        opacity: 0.25
    }

    Item {
        id: header
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: 12
        anchors.rightMargin: 13
        height: Math.round(40 * root.textScale)

        Label {
            anchors.fill: parent
            verticalAlignment: Text.AlignVCenter
            text: (root.folderHasParent ? "‹ " : "") + root.folderName
            color: headerArea.containsMouse ? root.textColor : root.mutedColor
            elide: Text.ElideMiddle
            font.family: "iA Writer Mono S"
            font.pixelSize: Math.round(12 * root.textScale)
        }

        MouseArea {
            id: headerArea
            anchors.fill: parent
            enabled: root.folderHasParent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.parentFolderRequested()
        }
    }

    Label {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.leftMargin: 12
        anchors.rightMargin: 13
        height: root.rowHeight
        verticalAlignment: Text.AlignVCenter
        visible: root.entries.length === 0
        text: "Nothing here yet"
        color: root.mutedColor
        opacity: 0.7
        font.family: "iA Writer Mono S"
        font.pixelSize: Math.round(13 * root.textScale)
    }

    ListView {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: header.bottom
        anchors.bottom: parent.bottom
        anchors.rightMargin: 1
        anchors.bottomMargin: Math.round(32 * root.textScale)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.entries
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        delegate: Item {
            id: row

            required property var modelData

            width: ListView.view.width
            height: root.rowHeight

            readonly property bool current:
                !row.modelData.isDir
                && row.modelData.url.toString() === root.currentFileUrl.toString()

            Rectangle {
                anchors.fill: parent
                color: root.selectionFill
                opacity: rowArea.containsMouse ? 0.25 : 0
            }

            Label {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                verticalAlignment: Text.AlignVCenter
                text: row.modelData.isDir ? row.modelData.name + "/" : row.modelData.name
                color: row.current ? root.accentColor
                                   : (row.modelData.isDir ? root.mutedColor : root.textColor)
                elide: Text.ElideRight
                font.family: "iA Writer Mono S"
                font.pixelSize: Math.round(13 * root.textScale)
            }

            MouseArea {
                id: rowArea
                anchors.fill: parent
                hoverEnabled: true
                cursorShape: Qt.PointingHandCursor
                onClicked: {
                    if (row.modelData.isDir)
                        root.folderRequested(row.modelData.url);
                    else
                        root.fileRequested(row.modelData.url);
                }
            }
        }
    }
}
