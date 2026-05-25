import QtQuick 2.15

Rectangle {
    id: root
    property string label: ""
    property string iconText: ""
    signal clicked()

    implicitHeight: 32
    radius: 6
    color: mouseArea.containsMouse ? "#1e1e2e" : "#13131f"
    border.color: "#2d2d3d"

    Row {
        anchors.centerIn: parent
        spacing: 6
        Text { text: root.iconText; font.pixelSize: 13; color: "#64748b"; anchors.verticalCenter: parent.verticalCenter }
        Text { text: root.label;   font.pixelSize: 11; color: "#94a3b8"; anchors.verticalCenter: parent.verticalCenter }
    }

    MouseArea {
        id: mouseArea
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: root.clicked()
    }
}
