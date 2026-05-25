import QtQuick 2.15
import QtQuick.Layouts 1.15

// A row showing a MIDI learn binding
RowLayout {
    property string label: ""
    property string target: ""
    property var engine: null

    spacing: 6

    Text {
        Layout.preferredWidth: 50
        text: label
        color: "#94a3b8"
        font.pixelSize: 11
    }

    Rectangle {
        Layout.fillWidth: true
        height: 24
        radius: 5
        color: "#0d0d12"
        border.color: isLearning ? "#f59e0b" : "#2d2d3d"
        border.width: isLearning ? 2 : 1

        readonly property bool isLearning:
            engine && engine.midiLearnActive && engine.midiLearnTarget === target

        readonly property int binding: {
            if (!engine) return -1
            if (target === "record") return engine.recordButton
            if (target === "play")   return engine.playButton
            if (target === "stop")   return engine.stopButton
            if (target === "clear")  return engine.clearButton
            return -1
        }

        Text {
            anchors { fill: parent; leftMargin: 8 }
            verticalAlignment: Text.AlignVCenter
            text: {
                if (parent.isLearning) return "▶ move a knob/button..."
                return parent.binding >= 0 ? "CC " + parent.binding : "—"
            }
            color: parent.isLearning ? "#f59e0b" : (parent.binding >= 0 ? "#a78bfa" : "#475569")
            font.pixelSize: 10
            elide: Text.ElideRight
        }
    }

    // Learn button
    Rectangle {
        width: 22; height: 22
        radius: 5
        color: mouseArea.containsMouse ? "#1e1e2e" : "transparent"
        border.color: "#2d2d3d"

        Text {
            anchors.centerIn: parent
            text: "◉"
            font.pixelSize: 12
            color: "#64748b"
        }
        MouseArea {
            id: mouseArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: {
                if (!engine) return
                if (engine.midiLearnActive && engine.midiLearnTarget === target)
                    engine.cancelMidiLearn()
                else
                    engine.startMidiLearn(target)
            }
        }
    }
}
