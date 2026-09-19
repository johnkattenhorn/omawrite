import QtQuick
import QtQuick.Controls
import QtQuick.Window

Item {
    id: control

    property string iconName
    // A Material Design Icons codepoint, drawn from a Nerd Font: the range
    // the Omarchy shell draws its own icons from, so a verb here looks like
    // the same verb on the desktop. Zero leaves the drawn icon in place, and
    // a machine with no Nerd Font falls back to it too.
    property int glyph: 0
    property color iconColor: "#666666"
    property string tooltip
    // Some glyphs sit low in their cell -- the robot's antenna makes it
    // bottom-heavy -- and need lifting to line up with the drawn icons.
    property real glyphOffset: 0

    signal clicked()

    // The face to draw a glyph with: the symbols-only Nerd Font when the
    // machine has one, the shell's own Nerd Font when it does not, and
    // nothing at all when neither is installed -- an icon nobody can render
    // is a box, so the drawn one stands in.
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

    width: 16
    height: 16

    ToolTip.visible: hitArea.containsMouse && tooltip.length > 0
    ToolTip.text: tooltip

    Text {
        id: glyphText
        anchors.centerIn: parent
        anchors.verticalCenterOffset: control.glyphOffset
        visible: control.drawsGlyph
        text: control.drawsGlyph ? String.fromCodePoint(control.glyph) : ""
        color: control.iconColor
        font.family: control.nerdFamily
        font.pixelSize: Math.round(control.height)
    }

    Canvas {
        id: iconCanvas
        visible: !control.drawsGlyph

        // Canvas rasterizes one surface pixel per logical pixel and gets
        // upscaled blurry on hidpi screens. Draw at the physical size and
        // scale the item back down so surface pixels match screen pixels.
        readonly property real dpr: Screen.devicePixelRatio
        width: control.width * dpr
        height: control.height * dpr
        transformOrigin: Item.TopLeft
        scale: 1 / dpr
        onDprChanged: requestPaint()

        onPaint: {
            var context = getContext("2d");
            context.setTransform(dpr, 0, 0, dpr, 0, 0);
            context.clearRect(0, 0, width, height);
            context.strokeStyle = control.iconColor;
            context.lineWidth = 1.4;
            context.lineCap = "round";
            context.lineJoin = "round";
            context.beginPath();
            if (control.iconName === "save") {
                context.moveTo(2.5, 2.5);
                context.lineTo(10.5, 2.5);
                context.lineTo(13.5, 5.5);
                context.lineTo(13.5, 13.5);
                context.lineTo(2.5, 13.5);
                context.closePath();
                context.moveTo(5.5, 2.5);
                context.lineTo(5.5, 6);
                context.lineTo(10, 6);
                context.lineTo(10, 2.5);
                context.moveTo(4.5, 13.5);
                context.lineTo(4.5, 9.5);
                context.lineTo(11.5, 9.5);
                context.lineTo(11.5, 13.5);
            } else if (control.iconName === "preview") {
                context.ellipse(2, 5, 12, 7);
                context.moveTo(2, 8.5);
                context.quadraticCurveTo(8, 1.5, 14, 8.5);
                context.quadraticCurveTo(8, 15.5, 2, 8.5);
                context.moveTo(6.5, 8.5);
                context.arc(8, 8.5, 1.5, 0, Math.PI * 2);
            } else if (control.iconName === "assistant") {
                // A speech bubble with its tail on the writing side, for a
                // machine with no Nerd Font to draw the robot with.
                context.moveTo(2.5, 4.5);
                context.lineTo(13.5, 4.5);
                context.lineTo(13.5, 11);
                context.lineTo(6.5, 11);
                context.lineTo(4, 13.5);
                context.lineTo(4, 11);
                context.lineTo(2.5, 11);
                context.closePath();
                context.moveTo(5.5, 7.5);
                context.lineTo(10.5, 7.5);
            } else if (control.iconName === "open") {
                context.moveTo(2.5, 13);
                context.lineTo(2.5, 3.5);
                context.lineTo(6.5, 3.5);
                context.lineTo(8.5, 5.5);
                context.lineTo(13.5, 5.5);
                context.lineTo(13.5, 13);
                context.closePath();
            } else {
                context.moveTo(2.5, 3.5);
                context.lineTo(13.5, 3.5);
                context.lineTo(13.5, 12.5);
                context.lineTo(2.5, 12.5);
                context.closePath();
                context.moveTo(6.5, 3.5);
                context.lineTo(6.5, 12.5);
            }
            context.stroke();
        }

        Connections {
            target: control
            function onIconColorChanged() { iconCanvas.requestPaint(); }
            function onIconNameChanged() { iconCanvas.requestPaint(); }
        }
    }

    MouseArea {
        id: hitArea
        anchors.centerIn: parent
        width: 28
        height: 28
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: control.clicked()
    }
}
