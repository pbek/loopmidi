import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    property int stepIndex: 0
    property var stepData: null
    property bool isCurrentStep: false
    property bool isRecordingStep: false
    property color accentColor: "#7c3aed"
    property color accentLightColor: "#a78bfa"
    property color recColor: "#ef4444"
    property color stepEmptyColor: "#1e1e2e"
    property color stepBorderColor: "#2d2d3d"
    property color textPrimaryColor: "#f1f5f9"
    property color textMutedColor: "#64748b"

    height: 80
    radius: 10

    readonly property bool hasNote:   stepData && stepData.active
    readonly property int  noteNum:   hasNote ? stepData.note : 0
    readonly property int  noteVel:   hasNote ? stepData.velocity : 0
    readonly property int  noteCount: (stepData && stepData.noteCount) ? stepData.noteCount : 0
    readonly property bool isChord:   noteCount > 1

    // Note name helper
    readonly property var    noteNames: ["C","C#","D","D#","E","F","F#","G","G#","A","A#","B"]
    readonly property string noteName:  hasNote ? noteNames[noteNum % 12] + Math.floor(noteNum / 12 - 1) : "—"

    color: {
        if (isCurrentStep && isRecordingStep) return Qt.rgba(0.94, 0.27, 0.27, 0.25)
        if (isCurrentStep) return Qt.rgba(0.49, 0.23, 0.93, 0.35)
        if (hasNote) return Qt.rgba(0.49, 0.23, 0.93, 0.15)
        return stepEmptyColor
    }

    border.color: {
        if (isCurrentStep && isRecordingStep) return recColor
        if (isCurrentStep) return accentColor
        if (hasNote) return Qt.rgba(0.49, 0.23, 0.93, 0.5)
        return stepBorderColor
    }
    border.width: isCurrentStep ? 2 : 1

    // Velocity bar (height proportional to first/lowest note velocity)
    Rectangle {
        visible: hasNote
        anchors { bottom: parent.bottom; left: parent.left; right: parent.right; bottomMargin: 0 }
        height: Math.max(3, (noteVel / 127.0) * (parent.height - 8))
        radius: 10
        color: isCurrentStep ? accentColor : Qt.rgba(0.49, 0.23, 0.93, 0.4)
        Behavior on height { NumberAnimation { duration: 100 } }
    }

    Column {
        anchors.centerIn: parent
        spacing: 4

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.noteName
            font.pixelSize: hasNote ? 16 : 12
            font.bold: hasNote
            color: {
                if (isCurrentStep) return accentLightColor
                if (hasNote) return textPrimaryColor
                return textMutedColor
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: String(stepIndex + 1).padStart(2, "0")
            font.pixelSize: 9
            font.letterSpacing: 1
            color: isCurrentStep ? accentColor : textMutedColor
            opacity: 0.7
        }
    }

    // Chord badge: shown when step contains >1 note
    Rectangle {
        visible: isChord
        anchors { top: parent.top; right: parent.right; topMargin: 5; rightMargin: 5 }
        width: chordLabel.implicitWidth + 8
        height: 16
        radius: 8
        color: isCurrentStep ? accentColor : Qt.rgba(0.49, 0.23, 0.93, 0.7)

        Text {
            id: chordLabel
            anchors.centerIn: parent
            text: "×" + noteCount
            font.pixelSize: 9
            font.bold: true
            color: "white"
        }
    }

    // Pulse animation for current step
    Rectangle {
        id: pulse
        anchors.fill: parent
        radius: parent.radius
        color: "transparent"
        border.color: isCurrentStep ? accentLightColor : "transparent"
        border.width: 1
        opacity: 0

        SequentialAnimation on opacity {
            running: isCurrentStep
            loops: Animation.Infinite
            NumberAnimation { to: 0.6; duration: 300 }
            NumberAnimation { to: 0;   duration: 300 }
        }
    }

    Behavior on color       { ColorAnimation { duration: 100 } }
    Behavior on border.color { ColorAnimation { duration: 100 } }
}
