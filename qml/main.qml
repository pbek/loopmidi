import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Layouts 1.15
import LoopMidi 1.0

Window {
    id: root
    visible: true
    width: 1000
    height: 720
    minimumWidth: 860
    minimumHeight: 620
    title: "LoopMidi"
    color: "#0d0d12"

    MidiEngine {
        id: engine
        onErrorOccurred: msg => errorBanner.show(msg)
        onNoteReceived: (note, vel, ch) => noteViz.flash(note, vel)
    }

    // ── Fonts / palette ─────────────────────────────────────────────────────
    readonly property color accent: "#7c3aed"
    readonly property color accentLight: "#a78bfa"
    readonly property color recColor: "#ef4444"
    readonly property color playColor: "#22c55e"
    readonly property color stepActive: "#7c3aed"
    readonly property color stepEmpty: "#1e1e2e"
    readonly property color stepBorder: "#2d2d3d"
    readonly property color textPrimary: "#f1f5f9"
    readonly property color textMuted: "#64748b"
    readonly property color panelBg: "#13131f"
    readonly property color cardBg: "#1a1a2e"

    // ── Error banner ────────────────────────────────────────────────────────
    Rectangle {
        id: errorBannerRect
        anchors {
            left: parent.left
            right: parent.right
            top: parent.top
        }
        height: 0
        color: "#7f1d1d"
        clip: true
        z: 100

        Text {
            id: errorText
            anchors.centerIn: parent
            color: "#fca5a5"
            font.pixelSize: 13
        }

        Behavior on height {
            NumberAnimation {
                duration: 200
            }
        }

        Timer {
            id: errorHideTimer
            interval: 4000
            onTriggered: errorBannerRect.height = 0
        }

        function show(msg) {
            errorText.text = msg;
            height = 36;
            errorHideTimer.restart();
        }
    }
    QtObject {
        id: errorBanner
        function show(m) {
            errorBannerRect.show(m);
        }
    }

    // ── Main layout ─────────────────────────────────────────────────────────
    ColumnLayout {
        anchors {
            fill: parent
            margins: 0
        }
        spacing: 0

        // Header
        Rectangle {
            Layout.fillWidth: true
            height: 56
            color: "#0d0d12"
            border {
                color: "#1e1e2e"
                width: 1
            }

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: 20
                    rightMargin: 20
                }
                spacing: 12

                // Logo / name
                Row {
                    spacing: 10
                    Rectangle {
                        width: 32
                        height: 32
                        radius: 8
                        gradient: Gradient {
                            orientation: Gradient.Horizontal
                            GradientStop {
                                position: 0
                                color: "#7c3aed"
                            }
                            GradientStop {
                                position: 1
                                color: "#6d28d9"
                            }
                        }
                        Text {
                            anchors.centerIn: parent
                            text: "⟳"
                            font.pixelSize: 18
                            color: "white"
                        }
                    }
                    Column {
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 0
                        Text {
                            text: "LoopMidi"
                            font.pixelSize: 18
                            font.bold: true
                            color: root.textPrimary
                        }
                        Text {
                            text: "MIDI Loop Sequencer"
                            font.pixelSize: 10
                            color: root.textMuted
                        }
                    }
                }

                Item {
                    Layout.fillWidth: true
                }

                // Status pills
                StatusPill {
                    label: "REC"
                    active: engine.recording
                    activeColor: root.recColor
                }
                StatusPill {
                    label: "PLAY"
                    active: engine.playing
                    activeColor: root.playColor
                }
                StatusPill {
                    label: "THRU"
                    active: engine.passthroughEnabled
                    activeColor: "#0ea5e9"
                    onToggled: engine.passthroughEnabled = !engine.passthroughEnabled
                    clickable: true
                }
            }
        }

        // Body
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            // ── Left sidebar ────────────────────────────────────────────────
            Rectangle {
                Layout.fillHeight: true
                width: 220
                color: root.panelBg
                border {
                    color: "#1e1e2e"
                    width: 1
                }

                ColumnLayout {
                    anchors {
                        fill: parent
                        margins: 16
                    }
                    spacing: 16

                    SectionLabel {
                        labelText: "MIDI PORTS"
                    }

                    // Input port
                    Column {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: "Input"
                            color: root.textMuted
                            font.pixelSize: 11
                        }
                        ComboBox {
                            id: inputPortCombo
                            width: parent.width
                            model: engine.inputPorts
                            currentIndex: engine.selectedInputPort
                            onActivated: engine.setSelectedInputPort(currentIndex)
                            Connections {
                                target: engine
                                function onSelectedInputPortChanged() {
                                    inputPortCombo.currentIndex = engine.selectedInputPort;
                                }
                                function onPortsChanged() {
                                    inputPortCombo.currentIndex = engine.selectedInputPort;
                                }
                            }
                            background: Rectangle {
                                color: root.cardBg
                                border.color: root.stepBorder
                                radius: 6
                            }
                            contentItem: Text {
                                leftPadding: 10
                                text: parent.displayText
                                color: root.textPrimary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            delegate: ItemDelegate {
                                width: ListView.view ? ListView.view.width : implicitWidth
                                contentItem: Text {
                                    text: modelData
                                    color: root.textPrimary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                background: Rectangle {
                                    color: hovered ? root.accent : root.cardBg
                                    radius: 4
                                }
                            }
                            popup.background: Rectangle {
                                color: root.cardBg
                                border.color: root.stepBorder
                                radius: 6
                            }
                        }
                    }

                    // Output port
                    Column {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: "Output (hardware)"
                            color: root.textMuted
                            font.pixelSize: 11
                        }
                        ComboBox {
                            id: outputPortCombo
                            width: parent.width
                            model: engine.outputPorts
                            currentIndex: engine.selectedOutputPort
                            onActivated: engine.setSelectedOutputPort(currentIndex)
                            Connections {
                                target: engine
                                function onSelectedOutputPortChanged() {
                                    outputPortCombo.currentIndex = engine.selectedOutputPort;
                                }
                                function onPortsChanged() {
                                    outputPortCombo.currentIndex = engine.selectedOutputPort;
                                }
                            }
                            background: Rectangle {
                                color: root.cardBg
                                border.color: root.stepBorder
                                radius: 6
                            }
                            contentItem: Text {
                                leftPadding: 10
                                text: parent.displayText
                                color: root.textPrimary
                                font.pixelSize: 12
                                elide: Text.ElideRight
                                verticalAlignment: Text.AlignVCenter
                            }
                            delegate: ItemDelegate {
                                width: ListView.view ? ListView.view.width : implicitWidth
                                contentItem: Text {
                                    text: modelData
                                    color: root.textPrimary
                                    font.pixelSize: 12
                                    elide: Text.ElideRight
                                }
                                background: Rectangle {
                                    color: hovered ? root.accent : root.cardBg
                                    radius: 4
                                }
                            }
                            popup.background: Rectangle {
                                color: root.cardBg
                                border.color: root.stepBorder
                                radius: 6
                            }
                        }
                    }

                    LoopButton {
                        Layout.fillWidth: true
                        label: "Refresh Ports"
                        iconText: "↺"
                        onClicked: engine.refreshPorts()
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.stepBorder
                    }
                    SectionLabel {
                        labelText: "TRANSPORT"
                    }

                    // BPM
                    Column {
                        Layout.fillWidth: true
                        spacing: 6
                        Row {
                            spacing: 4
                            Text {
                                text: "BPM"
                                color: root.textMuted
                                font.pixelSize: 11
                                anchors.verticalCenter: parent.verticalCenter
                            }
                            Item {
                                width: 1
                                height: 1
                            }
                            Text {
                                text: Math.round(engine.bpm)
                                color: root.accentLight
                                font.pixelSize: 13
                                font.bold: true
                                anchors.verticalCenter: parent.verticalCenter
                            }
                        }
                        Slider {
                            id: bpmSlider
                            width: parent.width
                            from: 40
                            to: 240
                            value: engine.bpm
                            onMoved: engine.bpm = value
                            Connections {
                                target: engine
                                function onBpmChanged() {
                                    if (!bpmSlider.pressed)
                                        bpmSlider.value = engine.bpm;
                                }
                            }
                            background: Rectangle {
                                x: parent.leftPadding
                                y: parent.topPadding + parent.availableHeight / 2 - height / 2
                                width: parent.availableWidth
                                height: 4
                                radius: 2
                                color: root.stepBorder
                                Rectangle {
                                    width: parent.parent.visualPosition * parent.width
                                    height: parent.height
                                    radius: 2
                                    color: root.accent
                                }
                            }
                            handle: Rectangle {
                                x: parent.leftPadding + parent.visualPosition * (parent.availableWidth - width)
                                y: parent.topPadding + parent.availableHeight / 2 - height / 2
                                width: 14
                                height: 14
                                radius: 7
                                color: root.accentLight
                                border.color: root.accent
                                border.width: 2
                            }
                        }
                    }

                    Rectangle {
                        Layout.fillWidth: true
                        height: 1
                        color: root.stepBorder
                    }
                    SectionLabel {
                        labelText: "MIDI LEARN"
                    }

                    MidiLearnRow {
                        label: "Record"
                        target: "record"
                        engine: engine
                    }
                    MidiLearnRow {
                        label: "Play"
                        target: "play"
                        engine: engine
                    }
                    MidiLearnRow {
                        label: "Stop"
                        target: "stop"
                        engine: engine
                    }
                    MidiLearnRow {
                        label: "Clear"
                        target: "clear"
                        engine: engine
                    }

                    Item {
                        Layout.fillHeight: true
                    }

                    // Virtual port info
                    Rectangle {
                        Layout.fillWidth: true
                        height: 44
                        radius: 8
                        color: "#0f2027"
                        border.color: "#164e63"
                        Column {
                            anchors {
                                fill: parent
                                margins: 8
                            }
                            spacing: 2
                            Text {
                                text: "Virtual Port"
                                color: "#38bdf8"
                                font.pixelSize: 10
                                font.bold: true
                            }
                            Text {
                                text: "LoopMidi Output"
                                color: root.textMuted
                                font.pixelSize: 10
                                elide: Text.ElideRight
                                width: parent.width
                            }
                        }
                    }
                }
            }

            // ── Center: sequencer ───────────────────────────────────────────
            ColumnLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Step grid area
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: root.panelBg

                    ColumnLayout {
                        anchors {
                            fill: parent
                            margins: 24
                        }
                        spacing: 20

                        // Title row
                        RowLayout {
                            Layout.fillWidth: true
                            Text {
                                text: "SEQUENCE"
                                font.pixelSize: 12
                                font.letterSpacing: 3
                                color: root.textMuted
                            }
                            Item {
                                Layout.fillWidth: true
                            }
                            Text {
                                text: {
                                    if (engine.recording)
                                        return "● RECORDING — play notes on your keyboard";
                                    if (engine.playing)
                                        return "▶ PLAYING";
                                    return "STOPPED";
                                }
                                color: {
                                    if (engine.recording)
                                        return root.recColor;
                                    if (engine.playing)
                                        return root.playColor;
                                    return root.textMuted;
                                }
                                font.pixelSize: 13
                                font.bold: engine.recording || engine.playing
                            }
                        }

                        // Step grid
                        GridLayout {
                            id: stepGrid
                            Layout.fillWidth: true
                            columns: 8
                            rowSpacing: 10
                            columnSpacing: 10

                            Repeater {
                                model: 16
                                delegate: StepCell {
                                    Layout.fillWidth: true
                                    stepIndex: index
                                    stepData: engine.sequence.length > index ? engine.sequence[index] : null
                                    isCurrentStep: engine.currentStep === index
                                    isRecordingStep: engine.recording && engine.currentStep === index
                                    accentColor: root.accent
                                    accentLightColor: root.accentLight
                                    recColor: root.recColor
                                    stepEmptyColor: root.stepEmpty
                                    stepBorderColor: root.stepBorder
                                    textPrimaryColor: root.textPrimary
                                    textMutedColor: root.textMuted
                                }
                            }
                        }

                        // Note visualizer
                        Rectangle {
                            Layout.fillWidth: true
                            height: 64
                            radius: 10
                            color: root.cardBg
                            border.color: root.stepBorder
                            clip: true

                            Text {
                                anchors {
                                    left: parent.left
                                    verticalCenter: parent.verticalCenter
                                    leftMargin: 16
                                }
                                text: "LIVE INPUT"
                                font.pixelSize: 10
                                font.letterSpacing: 2
                                color: root.textMuted
                            }

                            NoteVisualizer {
                                id: noteViz
                                anchors {
                                    fill: parent
                                    leftMargin: 90
                                    margins: 8
                                }
                                barColor: root.accent
                            }
                        }

                        Item {
                            Layout.fillHeight: true
                        }
                    }
                }

                // Transport controls bar
                Rectangle {
                    Layout.fillWidth: true
                    height: 72
                    color: "#0d0d12"
                    border {
                        color: "#1e1e2e"
                        width: 1
                    }

                    RowLayout {
                        anchors {
                            fill: parent
                            leftMargin: 24
                            rightMargin: 24
                        }
                        spacing: 12

                        // Record
                        TransportButton {
                            label: engine.recording ? "Stop Rec" : "Record"
                            activeColor: root.recColor
                            active: engine.recording
                            iconText: engine.recording ? "■" : "●"
                            shortcut: "R"
                            onClicked: engine.recording ? engine.stopRecording() : engine.startRecording()
                        }

                        // Play / Stop
                        TransportButton {
                            label: engine.playing ? "Stop" : "Play"
                            activeColor: root.playColor
                            active: engine.playing
                            iconText: engine.playing ? "■" : "▶"
                            shortcut: "Space"
                            onClicked: engine.playing ? engine.stopPlayback() : engine.startPlayback()
                        }

                        // Clear
                        TransportButton {
                            label: "Clear"
                            activeColor: "#f97316"
                            active: false
                            iconText: "✕"
                            shortcut: "C"
                            onClicked: engine.clearSequence()
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        // BPM control in transport bar
                        Column {
                            spacing: 2
                            Text {
                                text: "BPM"
                                font.pixelSize: 9
                                font.letterSpacing: 2
                                color: root.textMuted
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Row {
                                spacing: 4
                                anchors.horizontalCenter: parent.horizontalCenter
                                // Decrease BPM
                                Rectangle {
                                    width: 22
                                    height: 22
                                    radius: 5
                                    color: bpmMinusMouse.containsMouse ? "#2d2d3d" : root.cardBg
                                    border.color: root.stepBorder
                                    Text {
                                        anchors.centerIn: parent
                                        text: "−"
                                        font.pixelSize: 14
                                        color: root.accentLight
                                    }
                                    MouseArea {
                                        id: bpmMinusMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: engine.bpm = Math.max(40, engine.bpm - 5)
                                    }
                                    Behavior on color {
                                        ColorAnimation {
                                            duration: 80
                                        }
                                    }
                                }
                                // BPM value (scroll to adjust)
                                Rectangle {
                                    width: 52
                                    height: 22
                                    radius: 5
                                    color: root.cardBg
                                    border.color: root.stepBorder
                                    Text {
                                        anchors.centerIn: parent
                                        text: Math.round(engine.bpm)
                                        font.pixelSize: 13
                                        font.bold: true
                                        color: root.accentLight
                                    }
                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.SizeVerCursor
                                        onWheel: wheel => {
                                            var delta = wheel.angleDelta.y > 0 ? 1 : -1;
                                            engine.bpm = Math.max(40, Math.min(240, engine.bpm + delta));
                                        }
                                    }
                                }
                                // Increase BPM
                                Rectangle {
                                    width: 22
                                    height: 22
                                    radius: 5
                                    color: bpmPlusMouse.containsMouse ? "#2d2d3d" : root.cardBg
                                    border.color: root.stepBorder
                                    Text {
                                        anchors.centerIn: parent
                                        text: "+"
                                        font.pixelSize: 14
                                        color: root.accentLight
                                    }
                                    MouseArea {
                                        id: bpmPlusMouse
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: engine.bpm = Math.min(240, engine.bpm + 5)
                                    }
                                    Behavior on color {
                                        ColorAnimation {
                                            duration: 80
                                        }
                                    }
                                }
                            }
                        }

                        // Step counter
                        Column {
                            spacing: 2
                            Text {
                                text: "STEP"
                                font.pixelSize: 9
                                font.letterSpacing: 2
                                color: root.textMuted
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Text {
                                text: {
                                    var s = engine.currentStep;
                                    return (s >= 0 ? s + 1 : "-") + " / 16";
                                }
                                font.pixelSize: 22
                                font.bold: true
                                color: root.accentLight
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                        }

                        // Passthrough toggle
                        Column {
                            spacing: 4
                            Text {
                                text: "PASSTHROUGH"
                                font.pixelSize: 9
                                font.letterSpacing: 2
                                color: root.textMuted
                                anchors.horizontalCenter: parent.horizontalCenter
                            }
                            Switch {
                                anchors.horizontalCenter: parent.horizontalCenter
                                checked: engine.passthroughEnabled
                                onCheckedChanged: engine.passthroughEnabled = checked
                                indicator: Rectangle {
                                    implicitWidth: 44
                                    implicitHeight: 22
                                    radius: 11
                                    color: parent.checked ? "#0ea5e9" : root.stepBorder
                                    border.color: parent.checked ? "#38bdf8" : root.stepBorder
                                    Rectangle {
                                        x: parent.parent.checked ? parent.width - width - 2 : 2
                                        y: 2
                                        width: 18
                                        height: 18
                                        radius: 9
                                        color: "white"
                                        Behavior on x {
                                            NumberAnimation {
                                                duration: 150
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    // Keyboard shortcuts
    Shortcut {
        sequence: "R"
        onActivated: engine.recording ? engine.stopRecording() : engine.startRecording()
    }
    Shortcut {
        sequence: "Space"
        onActivated: engine.playing ? engine.stopPlayback() : engine.startPlayback()
    }
    Shortcut {
        sequence: "C"
        onActivated: engine.clearSequence()
    }
    Shortcut {
        sequence: "Escape"
        onActivated: {
            engine.stopRecording();
            engine.stopPlayback();
        }
    }
    Shortcut {
        sequence: "Ctrl+Q"
        onActivated: app.quit()
    }
}
