import QtQuick 2.15
import QtQuick.Controls 2.15

Rectangle {
    id: root
    property string label: ""
    property string iconText: ""
    property color activeColor: "#7c3aed"
    property bool active: false
    property string shortcut: ""
    signal clicked

    implicitWidth: 130
    implicitHeight: 44
    radius: 10
    color: active ? Qt.rgba(activeColor.r, activeColor.g, activeColor.b, 0.2) : "#1a1a2e"
    border.color: active ? activeColor : "#2d2d3d"
    border.width: active ? 2 : 1

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

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
        onPressed: root.scale = 0.96
        onReleased: root.scale = 1.0
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
