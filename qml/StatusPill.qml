import QtQuick 2.15

Rectangle {
    id: root
    property string label: ""
    property bool active: false
    property color activeColor: "#7c3aed"
    property bool clickable: false
    signal toggled

    implicitWidth: 54
    implicitHeight: 22
    radius: 11
    color: active ? Qt.rgba(activeColor.r, activeColor.g, activeColor.b, 0.2) : "#1e1e2e"
    border.color: active ? activeColor : "#2d2d3d"
    border.width: 1

    Row {
        anchors.centerIn: parent
        spacing: 4
        Rectangle {
            width: 6
            height: 6
            radius: 3
            color: root.active ? root.activeColor : "#334155"
            anchors.verticalCenter: parent.verticalCenter

            SequentialAnimation on opacity {
                running: root.active
                loops: Animation.Infinite
                NumberAnimation {
                    to: 0.3
                    duration: 600
                }
                NumberAnimation {
                    to: 1.0
                    duration: 600
                }
            }
        }
        Text {
            text: root.label
            font.pixelSize: 9
            font.letterSpacing: 1
            font.bold: true
            color: root.active ? "white" : "#475569"
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: root.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        enabled: root.clickable
        onClicked: root.toggled()
    }

    Behavior on color {
        ColorAnimation {
            duration: 200
        }
    }
}
