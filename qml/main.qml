import QtQuick 2.15
import QtQuick.Window 2.15
import QtQuick.Controls 2.15
import QtQuick.Dialogs
import QtQuick.Layouts 1.15
import LoopMidi 1.0

Window {
    id: root
    visible: true
    width: 1600
    height: 720
    minimumWidth: 1300
    minimumHeight: 620
    title: "LoopMidi"
    color: "#0d0d12"

    MidiEngine {
        id: engine
        onErrorOccurred: msg => errorBanner.show(msg)
        onNoteReceived: (note, vel, ch) => noteViz.flash(note, vel)
        onAvailableSurgePatchesChanged: root.surgePatchModel = availableSurgePatches
        onActiveTrackChanged: root.clearStepSelection()
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
    property var surgePatchModel: []
    property bool showInstrumentSlot: false
    property int selectedStepStart: -1
    property int selectedStepEnd: -1
    property int stepSelectionAnchor: -1

    function clearStepSelection() {
        selectedStepStart = -1;
        selectedStepEnd = -1;
        stepSelectionAnchor = -1;
    }

    function stepSelectionContains(index) {
        return selectedStepStart >= 0 && index >= selectedStepStart && index <= selectedStepEnd;
    }

    function selectStepRange(index) {
        const anchor = stepSelectionAnchor >= 0 ? stepSelectionAnchor : index;
        selectedStepStart = Math.min(anchor, index);
        selectedStepEnd = Math.max(anchor, index);
        stepSelectionAnchor = anchor;
    }

    function deleteSelectedOrCursorSteps() {
        if (selectedStepStart >= 0) {
            for (let i = selectedStepStart; i <= selectedStepEnd; ++i)
                engine.clearStep(i);
            return;
        }

        if (engine.cursorStep >= 0)
            engine.clearStep(engine.cursorStep);
    }

    function moveStepRange(fromIndex, count, toIndex) {
        if (toIndex >= fromIndex && toIndex < fromIndex + count)
            return;

        engine.moveSteps(fromIndex, count, toIndex);
        selectedStepStart = Math.min(toIndex, engine.sequence.length - count);
        selectedStepEnd = selectedStepStart + count - 1;
        stepSelectionAnchor = selectedStepStart;
    }

    function stepIndexAtWindowPosition(windowX, windowY, sourceIndex) {
        for (let i = 0; i < stepRepeater.count; ++i) {
            if (i === sourceIndex)
                continue;

            const item = stepRepeater.itemAt(i);
            if (!item)
                continue;

            const local = item.mapFromItem(null, windowX, windowY);
            if (local.x >= 0 && local.x <= item.width && local.y >= 0 && local.y <= item.height)
                return i;
        }

        return -1;
    }

    function moveStepRangeAtWindowPosition(fromIndex, count, windowX, windowY) {
        const targetIndex = stepIndexAtWindowPosition(windowX, windowY, fromIndex);
        if (targetIndex >= 0)
            moveStepRange(fromIndex, count, targetIndex);
    }

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

    FileDialog {
        id: saveProjectDialog
        title: "Save LoopMidi Project"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "loopmidi"
        nameFilters: ["LoopMidi projects (*.loopmidi)", "JSON files (*.json)", "All files (*)"]
        onAccepted: engine.saveProject(selectedFile.toString())
    }

    function openSaveProjectDialog() {
        var folder = engine.projectDialogDirectory;
        saveProjectDialog.currentFolder = folder;
        saveProjectDialog.currentFile = folder + "/" + engine.projectFileName;
        saveProjectDialog.open();
    }

    function selectedPluginIndex() {
        for (var i = 0; i < engine.availablePlugins.length; ++i) {
            var plugin = engine.availablePlugins[i];
            if (plugin.pluginId === engine.activeInstrumentPluginId && plugin.pluginFormat === engine.activeInstrumentFormat)
                return i;
            if (plugin.name === engine.activeInstrumentPluginId && plugin.pluginFormat === engine.activeInstrumentFormat)
                return i;
        }
        return -1;
    }

    FileDialog {
        id: loadProjectDialog
        title: "Load LoopMidi Project"
        fileMode: FileDialog.OpenFile
        nameFilters: ["LoopMidi projects (*.loopmidi *.json)", "All files (*)"]
        currentFolder: engine.projectDialogDirectory
        onAccepted: engine.loadProject(selectedFile.toString())
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
                    spacing: 8

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
                        labelText: "PROJECT"
                    }

                    Column {
                        Layout.fillWidth: true
                        spacing: 6
                        Text {
                            text: "Name"
                            color: root.textMuted
                            font.pixelSize: 11
                        }
                        TextField {
                            id: projectNameField
                            width: parent.width
                            text: engine.projectName
                            selectByMouse: true
                            font.pixelSize: 12
                            color: root.textPrimary
                            placeholderText: "Project name"
                            placeholderTextColor: root.textMuted
                            onEditingFinished: engine.projectName = text
                            Connections {
                                target: engine
                                function onProjectNameChanged() {
                                    if (!projectNameField.activeFocus)
                                        projectNameField.text = engine.projectName;
                                }
                            }
                            background: Rectangle {
                                color: root.cardBg
                                border.color: projectNameField.activeFocus ? root.accentLight : root.stepBorder
                                radius: 6
                            }
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        LoopButton {
                            Layout.fillWidth: true
                            label: "Save"
                            iconText: "↓"
                            onClicked: {
                                engine.projectName = projectNameField.text;
                                if (engine.projectFilePath.length > 0)
                                    engine.saveProject(engine.projectFilePath);
                                else
                                    root.openSaveProjectDialog();
                            }
                        }
                        LoopButton {
                            Layout.fillWidth: true
                            label: "Save As"
                            iconText: "↧"
                            onClicked: {
                                engine.projectName = projectNameField.text;
                                root.openSaveProjectDialog();
                            }
                        }
                    }

                    LoopButton {
                        Layout.fillWidth: true
                        label: "Load Project"
                        iconText: "↑"
                        onClicked: loadProjectDialog.open()
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
                                text: "TRACK " + (engine.activeTrack + 1)
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
                                        return engine.recordAllBeats ? "● RECORDING ALL BEATS TO TRACK " + (engine.activeTrack + 1) : "● RECORDING CURRENT BEAT TO TRACK " + (engine.activeTrack + 1);
                                    if (engine.playing)
                                        return "▶ PLAYING ALL TRACKS";
                                    if (engine.cursorStep >= 0)
                                        return "CURSOR — step " + (engine.cursorStep + 1) + "  (click again to clear)";
                                    return "STOPPED — click a step to set cursor";
                                }
                                color: {
                                    if (engine.recording)
                                        return root.recColor;
                                    if (engine.playing)
                                        return root.playColor;
                                    if (engine.cursorStep >= 0)
                                        return "#0ea5e9";
                                    return root.textMuted;
                                }
                                font.pixelSize: 13
                                font.bold: engine.recording || engine.playing
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Text {
                                text: "RECORD TARGET"
                                font.pixelSize: 10
                                font.letterSpacing: 2
                                color: root.textMuted
                            }

                            Repeater {
                                model: engine.trackCount
                                delegate: Rectangle {
                                    property bool selected: index === engine.activeTrack
                                    width: 86
                                    height: 30
                                    radius: 8
                                    color: selected ? Qt.rgba(0.49, 0.23, 0.93, 0.28) : root.cardBg
                                    border.color: selected ? root.accentLight : root.stepBorder
                                    border.width: selected ? 2 : 1

                                    Text {
                                        anchors.centerIn: parent
                                        text: "Track " + (index + 1)
                                        font.pixelSize: 12
                                        font.bold: parent.selected
                                        color: parent.selected ? root.accentLight : root.textMuted
                                    }

                                    MouseArea {
                                        anchors.fill: parent
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: engine.activeTrack = index
                                    }
                                }
                            }

                            Rectangle {
                                Layout.preferredWidth: 1
                                height: 24
                                color: root.stepBorder
                            }

                            Text {
                                text: engine.recordAllBeats ? "ALL BEATS" : "CURRENT BEAT"
                                font.pixelSize: 10
                                font.letterSpacing: 2
                                color: engine.recordAllBeats ? root.accentLight : "#0ea5e9"
                            }

                            Switch {
                                checked: engine.recordAllBeats
                                onCheckedChanged: engine.recordAllBeats = checked
                                background: Item {
                                    implicitWidth: 44
                                    implicitHeight: 22
                                }
                                indicator: Rectangle {
                                    implicitWidth: 44
                                    implicitHeight: 22
                                    radius: 11
                                    color: parent.checked ? root.accent : "#0ea5e9"
                                    border.color: parent.checked ? root.accentLight : "#38bdf8"
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

                            Text {
                                text: "MIDI CH"
                                font.pixelSize: 10
                                font.letterSpacing: 2
                                color: root.textMuted
                            }

                            SpinBox {
                                id: midiChannelSpinBox
                                from: 1
                                to: 16
                                value: engine.activeTrackMidiChannel
                                editable: true
                                Layout.preferredWidth: 92
                                onValueModified: engine.activeTrackMidiChannel = value
                                contentItem: TextInput {
                                    text: midiChannelSpinBox.textFromValue(midiChannelSpinBox.value, midiChannelSpinBox.locale)
                                    font.pixelSize: 12
                                    font.bold: true
                                    color: root.textPrimary
                                    selectionColor: root.accent
                                    selectedTextColor: "white"
                                    horizontalAlignment: Qt.AlignHCenter
                                    verticalAlignment: Qt.AlignVCenter
                                    readOnly: !midiChannelSpinBox.editable
                                    validator: midiChannelSpinBox.validator
                                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                                    leftPadding: 26
                                    rightPadding: 26
                                }
                                up.indicator: Rectangle {
                                    x: midiChannelSpinBox.width - width
                                    width: 28
                                    height: midiChannelSpinBox.height
                                    radius: 7
                                    color: upMouseArea.pressed ? Qt.rgba(0.49, 0.23, 0.93, 0.35) : "transparent"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "+"
                                        font.pixelSize: 14
                                        font.bold: true
                                        color: parent.enabled ? root.accentLight : root.textMuted
                                    }

                                    MouseArea {
                                        id: upMouseArea
                                        anchors.fill: parent
                                        enabled: midiChannelSpinBox.value < midiChannelSpinBox.to
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            midiChannelSpinBox.value = Math.min(midiChannelSpinBox.to, midiChannelSpinBox.value + 1);
                                            engine.activeTrackMidiChannel = midiChannelSpinBox.value;
                                        }
                                    }
                                }
                                down.indicator: Rectangle {
                                    width: 28
                                    height: midiChannelSpinBox.height
                                    radius: 7
                                    color: downMouseArea.pressed ? Qt.rgba(0.49, 0.23, 0.93, 0.35) : "transparent"

                                    Text {
                                        anchors.centerIn: parent
                                        text: "-"
                                        font.pixelSize: 16
                                        font.bold: true
                                        color: parent.enabled ? root.accentLight : root.textMuted
                                    }

                                    MouseArea {
                                        id: downMouseArea
                                        anchors.fill: parent
                                        enabled: midiChannelSpinBox.value > midiChannelSpinBox.from
                                        hoverEnabled: true
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: {
                                            midiChannelSpinBox.value = Math.max(midiChannelSpinBox.from, midiChannelSpinBox.value - 1);
                                            engine.activeTrackMidiChannel = midiChannelSpinBox.value;
                                        }
                                    }
                                }
                                background: Rectangle {
                                    color: root.cardBg
                                    border.color: midiChannelSpinBox.activeFocus ? root.accentLight : root.stepBorder
                                    border.width: midiChannelSpinBox.activeFocus ? 2 : 1
                                    radius: 7
                                }
                                Connections {
                                    target: engine
                                    function onActiveTrackMidiChannelChanged() {
                                        midiChannelSpinBox.value = engine.activeTrackMidiChannel;
                                    }
                                }
                            }

                            LoopButton {
                                Layout.preferredWidth: 100
                                label: root.showInstrumentSlot ? "Hide Slot" : "Show Slot"
                                iconText: root.showInstrumentSlot ? "-" : "+"
                                onClicked: root.showInstrumentSlot = !root.showInstrumentSlot
                            }

                            Item {
                                Layout.fillWidth: true
                            }
                        }

                        Rectangle {
                            visible: root.showInstrumentSlot
                            Layout.fillWidth: true
                            Layout.preferredWidth: 1180
                            Layout.maximumWidth: 1180
                            Layout.alignment: Qt.AlignHCenter
                            height: 122
                            radius: 10
                            color: root.cardBg
                            border.color: root.stepBorder

                            ColumnLayout {
                                anchors {
                                    fill: parent
                                    margins: 10
                                }
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 8

                                    Column {
                                        spacing: 3
                                        Layout.preferredWidth: 128
                                        Layout.alignment: Qt.AlignVCenter
                                        Text {
                                            text: "INSTRUMENT SLOT"
                                            font.pixelSize: 10
                                            font.letterSpacing: 2
                                            color: root.textMuted
                                        }
                                        Text {
                                            text: "Track " + (engine.activeTrack + 1)
                                            font.pixelSize: 18
                                            font.bold: true
                                            color: root.accentLight
                                        }
                                    }

                                    Column {
                                        spacing: 5
                                        Layout.preferredWidth: 236
                                        Layout.alignment: Qt.AlignBottom
                                        Text {
                                            text: "Plugin"
                                            font.pixelSize: 10
                                            color: root.textMuted
                                        }
                                        ComboBox {
                                            id: instrumentPluginCombo
                                            width: parent.width
                                            model: engine.availablePlugins
                                            textRole: "label"
                                            currentIndex: root.selectedPluginIndex()
                                            displayText: currentIndex >= 0 ? currentText : engine.activeInstrumentPluginId
                                            onActivated: engine.setActiveInstrumentFromAvailablePlugin(currentIndex)
                                            background: Rectangle {
                                                color: root.panelBg
                                                border.color: instrumentPluginCombo.activeFocus ? root.accentLight : root.stepBorder
                                                radius: 6
                                            }
                                            contentItem: Text {
                                                leftPadding: 10
                                                rightPadding: 10
                                                text: parent.displayText.length > 0 ? parent.displayText : "No plugins found"
                                                color: parent.displayText.length > 0 ? root.textPrimary : root.textMuted
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                            delegate: ItemDelegate {
                                                width: ListView.view ? ListView.view.width : implicitWidth
                                                contentItem: Text {
                                                    text: modelData.label
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
                                            Connections {
                                                target: engine
                                                function onActiveInstrumentChanged() {
                                                    instrumentPluginCombo.currentIndex = root.selectedPluginIndex();
                                                }
                                                function onAvailablePluginsChanged() {
                                                    instrumentPluginCombo.currentIndex = root.selectedPluginIndex();
                                                }
                                            }
                                        }
                                    }

                                    LoopButton {
                                        Layout.preferredWidth: 66
                                        Layout.alignment: Qt.AlignBottom
                                        label: "Scan"
                                        iconText: "↺"
                                        onClicked: {
                                            engine.scanPlugins();
                                            engine.scanSurgePatches();
                                        }
                                    }

                                    Column {
                                        spacing: 5
                                        Layout.preferredWidth: 86
                                        Layout.alignment: Qt.AlignBottom
                                        Text {
                                            text: "Format"
                                            font.pixelSize: 10
                                            color: root.textMuted
                                        }
                                        ComboBox {
                                            id: instrumentFormatCombo
                                            property var formatOptions: ["LV2", "CLAP", "VST3"]
                                            width: parent.width
                                            model: formatOptions
                                            currentIndex: Math.max(0, formatOptions.indexOf(engine.activeInstrumentFormat))
                                            onActivated: engine.activeInstrumentFormat = formatOptions[currentIndex]
                                            Connections {
                                                target: engine
                                                function onActiveInstrumentChanged() {
                                                    instrumentFormatCombo.currentIndex = Math.max(0, instrumentFormatCombo.formatOptions.indexOf(engine.activeInstrumentFormat));
                                                }
                                            }
                                            background: Rectangle {
                                                color: root.panelBg
                                                border.color: root.stepBorder
                                                radius: 6
                                            }
                                            contentItem: Text {
                                                leftPadding: 10
                                                text: parent.displayText
                                                color: root.textPrimary
                                                font.pixelSize: 12
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                    }

                                    Column {
                                        spacing: 5
                                        Layout.preferredWidth: 250
                                        Layout.alignment: Qt.AlignBottom
                                        Text {
                                            text: "Surge patch"
                                            font.pixelSize: 10
                                            color: root.textMuted
                                        }
                                        ComboBox {
                                            id: surgePatchCombo
                                            width: parent.width
                                            model: root.surgePatchModel
                                            textRole: "label"
                                            currentIndex: engine.activeSurgePatchIndex
                                            displayText: currentIndex >= 0 ? currentText : engine.activeInstrumentPresetName
                                            onActivated: engine.setActiveInstrumentFromSurgePatch(currentIndex)
                                            Component.onCompleted: root.surgePatchModel = engine.availableSurgePatches
                                            Connections {
                                                target: engine
                                                function onActiveInstrumentChanged() {
                                                    surgePatchCombo.currentIndex = engine.activeSurgePatchIndex;
                                                }
                                                function onAvailableSurgePatchesChanged() {
                                                    root.surgePatchModel = engine.availableSurgePatches;
                                                    surgePatchCombo.currentIndex = engine.activeSurgePatchIndex;
                                                }
                                            }
                                            background: Rectangle {
                                                color: root.panelBg
                                                border.color: surgePatchCombo.activeFocus ? root.accentLight : root.stepBorder
                                                radius: 6
                                            }
                                            contentItem: Text {
                                                leftPadding: 10
                                                rightPadding: 10
                                                text: parent.displayText.length > 0 ? parent.displayText : "No Surge patches found"
                                                color: parent.displayText.length > 0 ? root.textPrimary : root.textMuted
                                                font.pixelSize: 12
                                                elide: Text.ElideRight
                                                verticalAlignment: Text.AlignVCenter
                                            }
                                        }
                                    }

                                    Column {
                                        spacing: 5
                                        Layout.preferredWidth: 94
                                        Layout.alignment: Qt.AlignBottom
                                        Text {
                                            text: "Program"
                                            font.pixelSize: 10
                                            color: root.textMuted
                                        }
                                        SpinBox {
                                            id: instrumentProgramSpinBox
                                            from: 0
                                            to: 2047
                                            value: engine.activeInstrumentProgram
                                            editable: true
                                            width: parent.width
                                            onValueModified: engine.activeInstrumentProgram = value
                                            contentItem: TextInput {
                                                text: instrumentProgramSpinBox.textFromValue(instrumentProgramSpinBox.value, instrumentProgramSpinBox.locale)
                                                font.pixelSize: 12
                                                font.bold: true
                                                color: root.textPrimary
                                                selectionColor: root.accent
                                                selectedTextColor: "white"
                                                horizontalAlignment: Qt.AlignHCenter
                                                verticalAlignment: Qt.AlignVCenter
                                                readOnly: !instrumentProgramSpinBox.editable
                                                validator: instrumentProgramSpinBox.validator
                                                inputMethodHints: Qt.ImhFormattedNumbersOnly
                                                leftPadding: 26
                                                rightPadding: 26
                                            }
                                            up.indicator: Rectangle {
                                                x: instrumentProgramSpinBox.width - width
                                                width: 28
                                                height: instrumentProgramSpinBox.height
                                                radius: 7
                                                color: progUpMouseArea.pressed ? Qt.rgba(0.49, 0.23, 0.93, 0.35) : "transparent"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "+"
                                                    font.pixelSize: 14
                                                    font.bold: true
                                                    color: parent.enabled ? root.accentLight : root.textMuted
                                                }

                                                MouseArea {
                                                    id: progUpMouseArea
                                                    anchors.fill: parent
                                                    enabled: instrumentProgramSpinBox.value < instrumentProgramSpinBox.to
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        instrumentProgramSpinBox.value = Math.min(instrumentProgramSpinBox.to, instrumentProgramSpinBox.value + 1);
                                                        engine.activeInstrumentProgram = instrumentProgramSpinBox.value;
                                                    }
                                                }
                                            }
                                            down.indicator: Rectangle {
                                                width: 28
                                                height: instrumentProgramSpinBox.height
                                                radius: 7
                                                color: progDownMouseArea.pressed ? Qt.rgba(0.49, 0.23, 0.93, 0.35) : "transparent"

                                                Text {
                                                    anchors.centerIn: parent
                                                    text: "-"
                                                    font.pixelSize: 16
                                                    font.bold: true
                                                    color: parent.enabled ? root.accentLight : root.textMuted
                                                }

                                                MouseArea {
                                                    id: progDownMouseArea
                                                    anchors.fill: parent
                                                    enabled: instrumentProgramSpinBox.value > instrumentProgramSpinBox.from
                                                    hoverEnabled: true
                                                    cursorShape: Qt.PointingHandCursor
                                                    onClicked: {
                                                        instrumentProgramSpinBox.value = Math.max(instrumentProgramSpinBox.from, instrumentProgramSpinBox.value - 1);
                                                        engine.activeInstrumentProgram = instrumentProgramSpinBox.value;
                                                    }
                                                }
                                            }
                                            background: Rectangle {
                                                color: root.cardBg
                                                border.color: instrumentProgramSpinBox.activeFocus ? root.accentLight : root.stepBorder
                                                border.width: instrumentProgramSpinBox.activeFocus ? 2 : 1
                                                radius: 7
                                            }
                                            Connections {
                                                target: engine
                                                function onActiveInstrumentChanged() {
                                                    instrumentProgramSpinBox.value = engine.activeInstrumentProgram;
                                                }
                                            }
                                        }
                                    }

                                    Column {
                                        spacing: 5
                                        Layout.preferredWidth: 156
                                        Layout.alignment: Qt.AlignBottom
                                        Text {
                                            text: "Slot name"
                                            font.pixelSize: 10
                                            color: root.textMuted
                                        }
                                        TextField {
                                            id: instrumentNameField
                                            width: parent.width
                                            text: engine.activeInstrumentName
                                            selectByMouse: true
                                            font.pixelSize: 12
                                            color: root.textPrimary
                                            placeholderText: "Surge-XT Track"
                                            placeholderTextColor: root.textMuted
                                            onEditingFinished: engine.activeInstrumentName = text
                                            Connections {
                                                target: engine
                                                function onActiveInstrumentChanged() {
                                                    if (!instrumentNameField.activeFocus)
                                                        instrumentNameField.text = engine.activeInstrumentName;
                                                }
                                            }
                                            background: Rectangle {
                                                color: root.panelBg
                                                border.color: instrumentNameField.activeFocus ? root.accentLight : root.stepBorder
                                                radius: 6
                                            }
                                        }
                                    }

                                    Item {
                                        Layout.fillWidth: true
                                    }

                                    Column {
                                        spacing: 6
                                        Layout.alignment: Qt.AlignBottom
                                        Text {
                                            text: "Enabled"
                                            font.pixelSize: 10
                                            color: root.textMuted
                                        }
                                        Switch {
                                            checked: engine.activeInstrumentEnabled
                                            onCheckedChanged: engine.activeInstrumentEnabled = checked
                                            background: Item {
                                                implicitWidth: 44
                                                implicitHeight: 22
                                            }
                                            indicator: Rectangle {
                                                implicitWidth: 44
                                                implicitHeight: 22
                                                radius: 11
                                                color: parent.checked ? root.accent : root.panelBg
                                                border.color: parent.checked ? root.accentLight : root.stepBorder
                                                Rectangle {
                                                    x: parent.parent.checked ? parent.width - width - 2 : 2
                                                    y: 2
                                                    width: 18
                                                    height: 18
                                                    radius: 9
                                                    color: "white"
                                                }
                                            }
                                        }
                                    }
                                }

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Text {
                                        Layout.fillWidth: true
                                        text: engine.pluginHostStatus
                                        font.pixelSize: 11
                                        color: engine.pluginHostRunning ? root.playColor : root.textMuted
                                        elide: Text.ElideRight
                                    }

                                    Text {
                                        text: "AUTO AUDIO"
                                        font.pixelSize: 10
                                        font.letterSpacing: 2
                                        color: root.textMuted
                                    }

                                    Switch {
                                        checked: engine.pluginHostAutoConnectAudio
                                        onCheckedChanged: engine.pluginHostAutoConnectAudio = checked
                                        background: Item {
                                            implicitWidth: 44
                                            implicitHeight: 22
                                        }
                                        indicator: Rectangle {
                                            implicitWidth: 44
                                            implicitHeight: 22
                                            radius: 11
                                            color: parent.checked ? root.accent : root.panelBg
                                            border.color: parent.checked ? root.accentLight : root.stepBorder
                                            Rectangle {
                                                x: parent.parent.checked ? parent.width - width - 2 : 2
                                                y: 2
                                                width: 18
                                                height: 18
                                                radius: 9
                                                color: "white"
                                            }
                                        }
                                    }

                                    LoopButton {
                                        Layout.preferredWidth: 120
                                        label: engine.pluginHostRunning ? "Restart Host" : "Start Host"
                                        iconText: "▶"
                                        onClicked: engine.startPluginHost()
                                    }

                                    LoopButton {
                                        Layout.preferredWidth: 100
                                        label: "Stop Host"
                                        iconText: "■"
                                        onClicked: engine.stopPluginHost()
                                    }
                                }
                            }
                        }

                        // Step grid
                        Text {
                            Layout.fillWidth: true
                            text: "Click a beat to select it for recording or Delete. Shift-click an end beat to select a range, then drag or Delete it."
                            font.pixelSize: 11
                            color: root.textMuted
                            opacity: 0.75
                        }

                        GridLayout {
                            id: stepGrid
                            Layout.fillWidth: true
                            columns: 8
                            rowSpacing: 10
                            columnSpacing: 10

                            Repeater {
                                id: stepRepeater
                                model: 16
                                delegate: StepCell {
                                    Layout.fillWidth: true
                                    stepIndex: index
                                    stepData: engine.sequence.length > index ? engine.sequence[index] : null
                                    isCurrentStep: engine.currentStep === index
                                    isRecordingStep: engine.recording && engine.recordingStep === index
                                    isCursorStep: engine.cursorStep === index
                                    isSelected: root.stepSelectionContains(index)
                                    dragRangeStart: root.stepSelectionContains(index) ? root.selectedStepStart : index
                                    dragRangeCount: root.stepSelectionContains(index) ? root.selectedStepEnd - root.selectedStepStart + 1 : 1
                                    accentColor: root.accent
                                    accentLightColor: root.accentLight
                                    recColor: root.recColor
                                    stepEmptyColor: root.stepEmpty
                                    stepBorderColor: root.stepBorder
                                    textPrimaryColor: root.textPrimary
                                    textMutedColor: root.textMuted
                                    onDeleteStep: idx => engine.clearStep(idx)
                                    onRerecordStep: idx => engine.recordStep(idx)
                                    onMoveSteps: (fromIndex, count, toIndex) => root.moveStepRange(fromIndex, count, toIndex)
                                    onDragReleased: (fromIndex, count, windowX, windowY) => root.moveStepRangeAtWindowPosition(fromIndex, count, windowX, windowY)
                                    onCursorClicked: (idx, modifiers) => {
                                        if (modifiers & Qt.ShiftModifier) {
                                            root.selectStepRange(idx);
                                            return;
                                        }

                                        root.clearStepSelection();
                                        root.stepSelectionAnchor = idx;
                                        // Toggle off if clicking the same cell again
                                        if (engine.cursorStep === idx)
                                            engine.setCursorStep(-1);
                                        else
                                            engine.setCursorStep(idx);
                                    }
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
                            midiLearnTarget: engine.recording ? "stop" : "record"
                            midiLearnEngine: engine
                            midiBinding: engine.recording ? engine.stopButton : engine.recordButton
                            midiBindingIsNote: engine.recording ? engine.stopButtonIsNote : engine.recordButtonIsNote
                            onClicked: engine.recording ? engine.stopRecording() : engine.startRecording()
                        }

                        // Play / Stop
                        TransportButton {
                            label: engine.playing ? "Stop" : "Play"
                            activeColor: root.playColor
                            active: engine.playing
                            iconText: engine.playing ? "■" : "▶"
                            shortcut: "Space"
                            midiLearnTarget: engine.playing ? "stop" : "play"
                            midiLearnEngine: engine
                            midiBinding: engine.playing ? engine.stopButton : engine.playButton
                            midiBindingIsNote: engine.playing ? engine.stopButtonIsNote : engine.playButtonIsNote
                            onClicked: engine.playing ? engine.stopPlayback() : engine.startPlayback()
                        }

                        // Clear
                        TransportButton {
                            label: "Clear Track"
                            activeColor: "#f97316"
                            active: false
                            iconText: "✕"
                            shortcut: "C"
                            midiLearnTarget: "clear"
                            midiLearnEngine: engine
                            midiBinding: engine.clearButton
                            midiBindingIsNote: engine.clearButtonIsNote
                            onClicked: engine.clearSequence()
                        }

                        // Tap Tempo
                        TransportButton {
                            label: "Tap BPM"
                            activeColor: "#0ea5e9"
                            active: false
                            iconText: "♩"
                            midiLearnTarget: "tapTempo"
                            midiLearnEngine: engine
                            midiBinding: engine.tapTempoButton
                            midiBindingIsNote: engine.tapTempoButtonIsNote
                            onClicked: engine.tapTempo()
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
                                background: Item {
                                    implicitWidth: 44
                                    implicitHeight: 22
                                }
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
        sequence: "Delete"
        enabled: !projectNameField.activeFocus && !midiChannelSpinBox.activeFocus && !instrumentProgramSpinBox.activeFocus && !instrumentNameField.activeFocus
        onActivated: root.deleteSelectedOrCursorSteps()
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
