import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    property int stepIndex: 0
    property var stepData: null
    property bool isCurrentStep: false
    property bool isRecordingStep: false
    property bool isCursorStep: false
    property color accentColor: "#7c3aed"
    property color accentLightColor: "#a78bfa"
    property color recColor: "#ef4444"
    property color cursorColor: "#0ea5e9"
    property color stepEmptyColor: "#1e1e2e"
    property color stepBorderColor: "#2d2d3d"
    property color textPrimaryColor: "#f1f5f9"
    property color textMutedColor: "#64748b"

    signal deleteStep(int index)
    signal rerecordStep(int index)
    signal cursorClicked(int index)

    height: 80
    radius: 10

    readonly property bool hasNote: stepData && stepData.active
    readonly property int noteNum: hasNote ? stepData.note : 0
    readonly property int noteVel: hasNote ? stepData.velocity : 0
    readonly property int noteCount: (stepData && stepData.noteCount) ? stepData.noteCount : 0
    readonly property bool isChord: noteCount > 1

    // Note name helper
    readonly property var noteNames: ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
    readonly property string noteName: hasNote ? noteNames[noteNum % 12] + Math.floor(noteNum / 12 - 1) : "—"

    color: {
        if (isCurrentStep && isRecordingStep)
            return Qt.rgba(0.94, 0.27, 0.27, 0.25);
        if (isCurrentStep)
            return Qt.rgba(0.49, 0.23, 0.93, 0.35);
        if (isCursorStep)
            return Qt.rgba(0.055, 0.643, 0.914, 0.12);
        if (hasNote)
            return Qt.rgba(0.49, 0.23, 0.93, 0.15);
        return stepEmptyColor;
    }

    border.color: {
        if (isCurrentStep && isRecordingStep)
            return recColor;
        if (isCurrentStep)
            return accentColor;
        if (isCursorStep)
            return cursorColor;
        if (hasNote)
            return Qt.rgba(0.49, 0.23, 0.93, 0.5);
        return stepBorderColor;
    }
    border.width: (isCurrentStep || isCursorStep) ? 2 : 1

    // Velocity bar (height proportional to first/lowest note velocity)
    Rectangle {
        visible: hasNote
        anchors {
            bottom: parent.bottom
            left: parent.left
            right: parent.right
            bottomMargin: 0
        }
        height: Math.max(3, (noteVel / 127.0) * (parent.height - 8))
        radius: 10
        color: isCurrentStep ? accentColor : Qt.rgba(0.49, 0.23, 0.93, 0.4)
        Behavior on height {
            NumberAnimation {
                duration: 100
            }
        }
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
                if (isCurrentStep)
                    return accentLightColor;
                if (isCursorStep && !hasNote)
                    return cursorColor;
                if (hasNote)
                    return textPrimaryColor;
                return textMutedColor;
            }
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: String(stepIndex + 1).padStart(2, "0")
            font.pixelSize: 9
            font.letterSpacing: 1
            color: isCurrentStep ? accentColor : (isCursorStep ? cursorColor : textMutedColor)
            opacity: 0.7
        }
    }

    // Chord badge: shown when step contains >1 note
    Rectangle {
        visible: isChord
        anchors {
            top: parent.top
            right: parent.right
            topMargin: 5
            rightMargin: 5
        }
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

    // Cursor indicator: small teal triangle/arrow at top-left corner
    Rectangle {
        visible: isCursorStep && !isCurrentStep
        anchors {
            top: parent.top
            left: parent.left
            topMargin: 5
            leftMargin: 5
        }
        width: 6
        height: 6
        radius: 3
        color: cursorColor
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
            NumberAnimation {
                to: 0.6
                duration: 300
            }
            NumberAnimation {
                to: 0
                duration: 300
            }
        }
    }

    Behavior on color {
        ColorAnimation {
            duration: 100
        }
    }
    Behavior on border.color {
        ColorAnimation {
            duration: 100
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton)
                contextMenu.popup();
            else if (mouse.button === Qt.LeftButton)
                root.cursorClicked(root.stepIndex);
        }
    }

    Menu {
        id: contextMenu

        background: Rectangle {
            color: "#1a1a2e"
            border.color: "#2d2d3d"
            radius: 8
        }

        MenuItem {
            text: "Re-record step"
            contentItem: Text {
                text: parent.text
                color: "#a78bfa"
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
                leftPadding: 12
            }
            background: Rectangle {
                color: parent.hovered ? "#2d2d3d" : "transparent"
                radius: 6
            }
            onTriggered: root.rerecordStep(root.stepIndex)
        }

        MenuItem {
            text: "Delete step"
            contentItem: Text {
                text: parent.text
                color: "#ef4444"
                font.pixelSize: 13
                verticalAlignment: Text.AlignVCenter
                leftPadding: 12
            }
            background: Rectangle {
                color: parent.hovered ? "#2d2d3d" : "transparent"
                radius: 6
            }
            onTriggered: root.deleteStep(root.stepIndex)
        }
    }
}
