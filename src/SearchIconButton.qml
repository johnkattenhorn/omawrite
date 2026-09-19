import QtQuick
import QtQuick.Controls
import QtQuick.Window

Button {
    id: control

    property string iconName
    // The Material Design Icons codepoint to draw instead, when the machine
    // has a Nerd Font to draw it with. See FooterIconButton.
    property int glyph: 0
    property color iconColor: "#666666"

    readonly property string nerdFamily: {
        const installed = Qt.fontFamilies();
        const preferred = ["Symbols Nerd Font Mono", "Symbols Nerd Font",
                           "JetBrainsMono Nerd Font"];
        for (var i = 0; i < preferred.length; i++) {
            if (installed.indexOf(preferred[i]) >= 0)
                return preferred[i];
        }
        for (var j = 0; j < installed.length; j++) {
            if (installed[j].indexOf("Nerd Font") >= 0)
                return installed[j];
        }
        return "";
    }
    readonly property bool drawsGlyph: glyph > 0 && nerdFamily !== ""

    implicitWidth: 42
    implicitHeight: 42
    padding: 0

    Keys.onReturnPressed: clicked()
    Keys.onEnterPressed: clicked()

    background: Rectangle {
        color: "transparent"
    }

    contentItem: Item {
        Text {
            anchors.centerIn: parent
            visible: control.drawsGlyph
            text: control.drawsGlyph ? String.fromCodePoint(control.glyph) : ""
            color: control.iconColor
            font.family: control.nerdFamily
            font.pixelSize: 20
        }

        Canvas {
            id: iconCanvas
            visible: !control.drawsGlyph
            anchors.centerIn: parent

            // Canvas rasterizes one surface pixel per logical pixel and gets
            // upscaled blurry on hidpi screens. Draw at the physical size and
            // scale the item back down so surface pixels match screen pixels.
            readonly property real dpr: Screen.devicePixelRatio
            width: 20 * dpr
            height: 20 * dpr
            scale: 1 / dpr
            onDprChanged: requestPaint()

            onPaint: {
                var context = getContext("2d");
                context.setTransform(dpr, 0, 0, dpr, 0, 0);
                context.clearRect(0, 0, width, height);
                context.strokeStyle = control.iconColor;
                context.lineWidth = 2;
                context.lineCap = "square";
                context.lineJoin = "miter";
                context.beginPath();
                if (control.iconName === "close") {
                    context.moveTo(4, 4);
                    context.lineTo(16, 16);
                    context.moveTo(16, 4);
                    context.lineTo(4, 16);
                } else if (control.iconName === "up") {
                    context.moveTo(4, 13);
                    context.lineTo(10, 7);
                    context.lineTo(16, 13);
                } else {
                    context.moveTo(4, 7);
                    context.lineTo(10, 13);
                    context.lineTo(16, 7);
                }
                context.stroke();
            }

            Connections {
                target: control
                function onIconColorChanged() { iconCanvas.requestPaint(); }
                function onIconNameChanged() { iconCanvas.requestPaint(); }
            }
        }
    }
}
