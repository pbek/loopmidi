import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    property string label: ""
    property string iconText: ""
    property color activeColor: "#7c3aed"
    property bool active: false
    property string shortcut: ""
    property string midiLearnTarget: ""
    property var midiLearnEngine: null
    property int midiBinding: -1
    property bool midiBindingIsNote: false
    signal clicked

    readonly property bool canMidiLearn: midiLearnTarget.length > 0 && midiLearnEngine !== null
    readonly property bool midiLearning: canMidiLearn && midiLearnEngine.midiLearnActive && midiLearnEngine.midiLearnTarget === midiLearnTarget
    readonly property bool hasMidiBinding: midiBinding >= 0
    readonly property string midiBindingText: hasMidiBinding ? (midiBindingIsNote ? "Note " : "CC ") + midiBinding : "unassigned"

    implicitWidth: 130
    implicitHeight: 44
    radius: 10
    color: active ? Qt.rgba(activeColor.r, activeColor.g, activeColor.b, 0.2) : "#1a1a2e"
    border.color: midiLearning ? "#f59e0b" : (active ? activeColor : "#2d2d3d")
    border.width: (active || midiLearning) ? 2 : 1

    Row {
        anchors.centerIn: parent
        spacing: 8
        Text {
            text: root.iconText
            font.pixelSize: 14
            color: root.active ? root.activeColor : "#94a3b8"
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            text: root.label
            font.pixelSize: 13
            font.bold: root.active
            color: root.active ? "white" : "#94a3b8"
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    Rectangle {
        visible: root.hasMidiBinding
        anchors {
            top: parent.top
            right: parent.right
            topMargin: 4
            rightMargin: 4
        }
        width: 16
        height: 16
        radius: 8
        color: "#312e81"
        border.color: "#a78bfa"

        Text {
            anchors.centerIn: parent
            text: "♪"
            color: "#ddd6fe"
            font.pixelSize: 10
            font.bold: true
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        cursorShape: Qt.PointingHandCursor
        onClicked: mouse => {
            if (mouse.button === Qt.RightButton && root.canMidiLearn) {
                midiLearnMenu.popup();
            } else if (mouse.button === Qt.LeftButton) {
                root.clicked();
            }
        }
        onPressed: mouse => {
            if (mouse.button === Qt.LeftButton)
                root.scale = 0.96;
        }
        onReleased: root.scale = 1.0
        onCanceled: root.scale = 1.0
    }

    Menu {
        id: midiLearnMenu

        background: Rectangle {
            color: "#1a1a2e"
            border.color: "#2d2d3d"
            radius: 8
        }

        MenuItem {
            text: "MIDI Learn (" + root.midiBindingText + ")"
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
            onTriggered: {
                if (root.canMidiLearn)
                    root.midiLearnEngine.startMidiLearn(root.midiLearnTarget);
            }
        }
    }

    Behavior on scale {
        NumberAnimation {
            duration: 80
        }
    }
    Behavior on color {
        ColorAnimation {
            duration: 150
        }
    }
    Behavior on border.color {
        ColorAnimation {
            duration: 150
        }
    }
}
