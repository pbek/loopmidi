#include "MidiEngine.h"
#include <QDebug>
#include <QMutexLocker>

static const int kChordWindowMs = 30; // notes within this window are grouped into one step

static void midiCallbackStatic(double deltatime, std::vector<unsigned char>* message, void* userData) {
    auto* engine = static_cast<MidiEngine*>(userData);
    if (message && !message->empty()) {
        engine->processIncomingMidi(*message);
    }
}

MidiEngine::MidiEngine(QObject* parent) : QObject(parent) {
    try {
        m_midiIn    = std::make_unique<RtMidiIn>();
        m_midiOut   = std::make_unique<RtMidiOut>();
        m_midiOutHW = std::make_unique<RtMidiOut>();
    } catch (RtMidiError& e) {
        emit errorOccurred(QString::fromStdString(e.getMessage()));
    }

    m_stepTimer = new QTimer(this);
    connect(m_stepTimer, &QTimer::timeout, this, &MidiEngine::advanceStep);

    m_hotplugTimer = new QTimer(this);
    m_hotplugTimer->setInterval(1000);
    connect(m_hotplugTimer, &QTimer::timeout, this, &MidiEngine::pollPorts);
    m_hotplugTimer->start();

    // Chord grouping timer: single-shot, fires after kChordWindowMs to close a chord step
    m_chordTimer = new QTimer(this);
    m_chordTimer->setSingleShot(true);
    m_chordTimer->setInterval(kChordWindowMs);
    connect(m_chordTimer, &QTimer::timeout, this, &MidiEngine::commitChordStep);

    m_sequence.resize(m_maxSteps);

    setupVirtualOutput();
    loadSettings();
    refreshPorts();
    tryRestoreSavedPorts();
}

MidiEngine::~MidiEngine() {
    stopPlayback();
    if (m_midiIn)    try { m_midiIn->closePort();    } catch (...) {}
    if (m_midiOut)   try { m_midiOut->closePort();   } catch (...) {}
    if (m_midiOutHW) try { m_midiOutHW->closePort(); } catch (...) {}
}

// ── Virtual port ─────────────────────────────────────────────────────────────

void MidiEngine::setupVirtualOutput() {
    if (!m_midiOut) return;
    try {
        m_midiOut->openVirtualPort("LoopMidi Output");
        m_virtualPortOpen = true;
        qDebug() << "Virtual MIDI port 'LoopMidi Output' opened";
    } catch (RtMidiError& e) {
        qWarning() << "Could not open virtual MIDI port:" << QString::fromStdString(e.getMessage());
        emit errorOccurred(QString("Virtual port: ") + QString::fromStdString(e.getMessage()));
    }
}

// ── Port management ──────────────────────────────────────────────────────────

void MidiEngine::refreshPorts() {
    m_inputPorts.clear();
    m_outputPorts.clear();

    if (m_midiIn) {
        int count = static_cast<int>(m_midiIn->getPortCount());
        for (int i = 0; i < count; ++i)
            m_inputPorts << QString::fromStdString(m_midiIn->getPortName(i));
    }

    try {
        RtMidiOut tmpOut;
        int count = static_cast<int>(tmpOut.getPortCount());
        for (int i = 0; i < count; ++i)
            m_outputPorts << QString::fromStdString(tmpOut.getPortName(i));
    } catch (...) {}

    emit portsChanged();
}

void MidiEngine::pollPorts() {
    QStringList newInputs, newOutputs;

    if (m_midiIn) {
        try {
            int count = static_cast<int>(m_midiIn->getPortCount());
            for (int i = 0; i < count; ++i)
                newInputs << QString::fromStdString(m_midiIn->getPortName(i));
        } catch (...) {}
    }
    try {
        RtMidiOut tmpOut;
        int count = static_cast<int>(tmpOut.getPortCount());
        for (int i = 0; i < count; ++i)
            newOutputs << QString::fromStdString(tmpOut.getPortName(i));
    } catch (...) {}

    if (newInputs == m_inputPorts && newOutputs == m_outputPorts)
        return;

    // Determine whether the currently-selected ports still exist in the new lists
    QString activeInputName, activeOutputName;
    if (m_selectedInputPort >= 0 && m_selectedInputPort < m_inputPorts.size())
        activeInputName = m_inputPorts[m_selectedInputPort];
    if (m_selectedOutputPort >= 0 && m_selectedOutputPort < m_outputPorts.size())
        activeOutputName = m_outputPorts[m_selectedOutputPort];

    // If the active input disappeared, close the port
    if (!activeInputName.isEmpty() && !newInputs.contains(activeInputName)) {
        if (m_midiIn && m_midiIn->isPortOpen())
            try { m_midiIn->closePort(); } catch (...) {}
        activeInputName.clear();
    }
    // If the active output disappeared, close it
    if (!activeOutputName.isEmpty() && !newOutputs.contains(activeOutputName)) {
        if (m_midiOutHW && m_midiOutHW->isPortOpen())
            try { m_midiOutHW->closePort(); } catch (...) {}
        activeOutputName.clear();
    }

    m_inputPorts  = newInputs;
    m_outputPorts = newOutputs;

    // Re-derive indices from names so the ComboBox selection survives list changes
    int newInIdx  = activeInputName.isEmpty()  ? -1 : m_inputPorts.indexOf(activeInputName);
    int newOutIdx = activeOutputName.isEmpty() ? -1 : m_outputPorts.indexOf(activeOutputName);

    bool inChanged  = (newInIdx  != m_selectedInputPort);
    bool outChanged = (newOutIdx != m_selectedOutputPort);
    m_selectedInputPort  = newInIdx;
    m_selectedOutputPort = newOutIdx;

    emit portsChanged();
    if (inChanged)  emit selectedInputPortChanged();
    if (outChanged) emit selectedOutputPortChanged();

    tryRestoreSavedPorts();
}

void MidiEngine::openInputPort(int port) {
    if (!m_midiIn) return;
    try {
        if (m_midiIn->isPortOpen()) m_midiIn->closePort();
        m_midiIn->openPort(static_cast<unsigned int>(port));
        m_midiIn->ignoreTypes(false, true, true);
        m_midiIn->setCallback(&midiCallbackStatic, this);
        qDebug() << "Opened input port" << port;
    } catch (RtMidiError& e) {
        emit errorOccurred(QString::fromStdString(e.getMessage()));
    }
}

void MidiEngine::openOutputPort(int port) {
    if (!m_midiOutHW) return;
    try {
        if (m_midiOutHW->isPortOpen()) m_midiOutHW->closePort();
        m_midiOutHW->openPort(static_cast<unsigned int>(port));
        qDebug() << "Opened output port" << port;
    } catch (RtMidiError& e) {
        emit errorOccurred(QString::fromStdString(e.getMessage()));
    }
}

void MidiEngine::setSelectedInputPort(int port) {
    if (m_selectedInputPort == port) return;
    m_selectedInputPort = port;
    if (port >= 0) openInputPort(port);
    emit selectedInputPortChanged();
    saveSettings();
}

void MidiEngine::setSelectedOutputPort(int port) {
    if (m_selectedOutputPort == port) return;
    m_selectedOutputPort = port;
    if (port >= 0) openOutputPort(port);
    emit selectedOutputPortChanged();
    saveSettings();
}

// ── Transport & BPM ──────────────────────────────────────────────────────────

void MidiEngine::setBpm(double bpm) {
    if (qFuzzyCompare(m_bpm, bpm)) return;
    m_bpm = bpm;
    if (m_stepTimer->isActive()) {
        int ms = static_cast<int>(60000.0 / m_bpm / 4.0);
        m_stepTimer->setInterval(ms);
    }
    emit bpmChanged();
    saveSettings();
}

void MidiEngine::setPassthroughEnabled(bool enabled) {
    if (m_passthroughEnabled == enabled) return;
    m_passthroughEnabled = enabled;
    emit passthroughEnabledChanged();
}

void MidiEngine::startRecording() {
    if (m_recording) return;
    clearSequence();
    m_recording = true;
    m_currentStep = 0;
    emit recordingChanged();
    emit currentStepChanged();
}

void MidiEngine::stopRecording() {
    if (!m_recording) return;
    m_chordTimer->stop();
    m_recording = false;
    emit recordingChanged();
}

void MidiEngine::startPlayback() {
    if (m_playing) return;
    m_playing = true;
    m_currentStep = 0;
    int ms = static_cast<int>(60000.0 / m_bpm / 4.0);
    m_stepTimer->start(ms);
    emit playingChanged();
    emit currentStepChanged();
}

void MidiEngine::stopPlayback() {
    if (!m_playing) return;
    m_stepTimer->stop();
    m_playing = false;
    stopAllNotes();
    m_currentStep = -1;
    emit playingChanged();
    emit currentStepChanged();
}

void MidiEngine::clearSequence() {
    stopAllNotes();
    for (auto& step : m_sequence)
        step.clear();
    emit sequenceChanged();
}

// ── MIDI Learn ───────────────────────────────────────────────────────────────

void MidiEngine::startMidiLearn(const QString& target) {
    m_midiLearnTargetStr = target;
    if      (target == "record") m_midiLearnTargetType = MidiLearnTarget::Record;
    else if (target == "play")   m_midiLearnTargetType = MidiLearnTarget::Play;
    else if (target == "stop")   m_midiLearnTargetType = MidiLearnTarget::Stop;
    else if (target == "clear")  m_midiLearnTargetType = MidiLearnTarget::Clear;
    else                         m_midiLearnTargetType = MidiLearnTarget::None;

    m_midiLearnActive = true;
    emit midiLearnActiveChanged();
    emit midiLearnTargetChanged();
}

void MidiEngine::cancelMidiLearn() {
    m_midiLearnActive = false;
    m_midiLearnTargetStr.clear();
    m_midiLearnTargetType = MidiLearnTarget::None;
    emit midiLearnActiveChanged();
    emit midiLearnTargetChanged();
}

void MidiEngine::handleMidiLearn(int ccOrNote, bool /*isCC*/) {
    switch (m_midiLearnTargetType) {
        case MidiLearnTarget::Record: m_recordButton = ccOrNote; break;
        case MidiLearnTarget::Play:   m_playButton   = ccOrNote; break;
        case MidiLearnTarget::Stop:   m_stopButton   = ccOrNote; break;
        case MidiLearnTarget::Clear:  m_clearButton  = ccOrNote; break;
        default: break;
    }
    m_midiLearnActive = false;
    m_midiLearnTargetStr.clear();
    m_midiLearnTargetType = MidiLearnTarget::None;
    emit midiBindingsChanged();
    emit midiLearnActiveChanged();
    emit midiLearnTargetChanged();
    saveSettings();
}

// ── Sequence QVariantList (read by QML) ──────────────────────────────────────

QVariantList MidiEngine::sequence() const {
    QVariantList result;
    for (const auto& step : m_sequence) {
        QVariantMap map;
        bool active = !step.isEmpty();
        map["active"]     = active;
        map["noteCount"]  = step.size();
        if (active) {
            // Expose the lowest note as the "primary" note for the cell label
            const NoteEvent* primary = &step[0];
            for (const auto& ev : step)
                if (ev.note < primary->note) primary = &ev;
            map["note"]     = primary->note;
            map["velocity"] = primary->velocity;
            map["channel"]  = primary->channel;
        } else {
            map["note"]     = 0;
            map["velocity"] = 0;
            map["channel"]  = 0;
        }
        result << map;
    }
    return result;
}

// ── MIDI input processing ────────────────────────────────────────────────────

void MidiEngine::processIncomingMidi(const std::vector<unsigned char>& msg) {
    if (msg.empty()) return;

    unsigned char status  = msg[0];
    unsigned char data1   = msg.size() > 1 ? msg[1] : 0;
    unsigned char data2   = msg.size() > 2 ? msg[2] : 0;
    int channel           = (status & 0x0F);
    unsigned char msgType = (status & 0xF0);

    bool isNoteOn  = (msgType == 0x90) && (data2 > 0);
    bool isNoteOff = (msgType == 0x80) || ((msgType == 0x90) && (data2 == 0));
    bool isCC      = (msgType == 0xB0);
    Q_UNUSED(isNoteOff)

    // MIDI Learn
    if (m_midiLearnActive) {
        if (isCC)       handleMidiLearn(static_cast<int>(data1), true);
        else if (isNoteOn) handleMidiLearn(static_cast<int>(data1), false);
        return;
    }

    // Control bindings (CC)
    if (isCC) {
        int cc = static_cast<int>(data1);
        if (cc == m_recordButton && m_recordButton >= 0) {
            if (data2 > 0) { if (!m_recording) startRecording(); else stopRecording(); }
            return;
        }
        if (cc == m_playButton && m_playButton >= 0) {
            if (data2 > 0) { if (!m_playing) startPlayback(); else stopPlayback(); }
            return;
        }
        if (cc == m_stopButton && m_stopButton >= 0) {
            if (data2 > 0) { stopPlayback(); stopRecording(); }
            return;
        }
        if (cc == m_clearButton && m_clearButton >= 0) {
            if (data2 > 0) clearSequence();
            return;
        }
    }

    // Passthrough to virtual output
    if (m_passthroughEnabled && m_midiOut && m_virtualPortOpen) {
        try {
            std::vector<unsigned char> fwd(msg.begin(), msg.end());
            m_midiOut->sendMessage(&fwd);
        } catch (...) {}
    }

    // Recording: group Note-On events within kChordWindowMs into the same step
    if (m_recording && isNoteOn) {
        QMutexLocker locker(&m_mutex);
        if (m_currentStep < m_maxSteps) {
            m_sequence[m_currentStep].append(NoteEvent{
                static_cast<int>(data1),
                static_cast<int>(data2),
                channel
            });
            // All signal emissions and timer ops must happen on the main thread
            QMetaObject::invokeMethod(this, [this]() {
                emit sequenceChanged();
                m_chordTimer->start(kChordWindowMs);
            }, Qt::QueuedConnection);
        }
    }

    if (isNoteOn)
        QMetaObject::invokeMethod(this, [this, data1, data2, channel]() {
            emit noteReceived(data1, data2, channel);
        }, Qt::QueuedConnection);
}

// Called when chord window closes: advance to the next step (always on main thread)
void MidiEngine::commitChordStep() {
    if (!m_recording) return;
    m_currentStep++;
    emit currentStepChanged();
    if (m_currentStep >= m_maxSteps) {
        m_recording = false;
        emit recordingChanged();
        QMetaObject::invokeMethod(this, "startPlayback", Qt::QueuedConnection);
    }
}

// ── Playback step advance ────────────────────────────────────────────────────

void MidiEngine::advanceStep() {
    if (!m_playing) return;

    // Stop all notes from the previous step
    for (const auto& ev : m_activePlaybackNotes)
        sendNoteOff(ev.note, ev.channel);
    m_activePlaybackNotes.clear();

    const ChordStep& step = m_sequence[m_currentStep];
    for (const auto& ev : step) {
        sendNoteOn(ev.note, ev.velocity, ev.channel);
        m_activePlaybackNotes.append(ev);
    }

    m_currentStep = (m_currentStep + 1) % m_maxSteps;
    emit currentStepChanged();
}

// ── Note send helpers ────────────────────────────────────────────────────────

void MidiEngine::sendNoteOn(int note, int velocity, int channel) {
    if (!m_midiOut || !m_virtualPortOpen) return;
    try {
        std::vector<unsigned char> msg = {
            static_cast<unsigned char>(0x90 | (channel & 0x0F)),
            static_cast<unsigned char>(note & 0x7F),
            static_cast<unsigned char>(velocity & 0x7F)
        };
        m_midiOut->sendMessage(&msg);
    } catch (...) {}
}

void MidiEngine::sendNoteOff(int note, int channel) {
    if (!m_midiOut || !m_virtualPortOpen) return;
    try {
        std::vector<unsigned char> msg = {
            static_cast<unsigned char>(0x80 | (channel & 0x0F)),
            static_cast<unsigned char>(note & 0x7F),
            0x00
        };
        m_midiOut->sendMessage(&msg);
    } catch (...) {}
}

void MidiEngine::stopAllNotes() {
    for (const auto& ev : m_activePlaybackNotes)
        sendNoteOff(ev.note, ev.channel);
    m_activePlaybackNotes.clear();

    if (m_midiOut && m_virtualPortOpen) {
        try {
            for (int ch = 0; ch < 16; ++ch) {
                std::vector<unsigned char> msg = {
                    static_cast<unsigned char>(0xB0 | ch), 123, 0
                };
                m_midiOut->sendMessage(&msg);
            }
        } catch (...) {}
    }
}

// ── Persistence ──────────────────────────────────────────────────────────────

void MidiEngine::loadSettings() {
    QSettings s;
    m_bpm             = s.value("bpm", 120.0).toDouble();
    m_savedInputName  = s.value("inputPortName").toString();
    m_savedOutputName = s.value("outputPortName").toString();
    m_recordButton    = s.value("recordButton", -1).toInt();
    m_playButton      = s.value("playButton",   -1).toInt();
    m_stopButton      = s.value("stopButton",   -1).toInt();
    m_clearButton     = s.value("clearButton",  -1).toInt();
    if (m_bpm < 40.0)  m_bpm = 40.0;
    if (m_bpm > 240.0) m_bpm = 240.0;
}

void MidiEngine::saveSettings() const {
    QSettings s;
    s.setValue("bpm", m_bpm);
    if (m_selectedInputPort >= 0 && m_selectedInputPort < m_inputPorts.size())
        s.setValue("inputPortName", m_inputPorts[m_selectedInputPort]);
    if (m_selectedOutputPort >= 0 && m_selectedOutputPort < m_outputPorts.size())
        s.setValue("outputPortName", m_outputPorts[m_selectedOutputPort]);
    s.setValue("recordButton", m_recordButton);
    s.setValue("playButton",   m_playButton);
    s.setValue("stopButton",   m_stopButton);
    s.setValue("clearButton",  m_clearButton);
}

void MidiEngine::tryRestoreSavedPorts() {
    if (m_selectedInputPort < 0 && !m_savedInputName.isEmpty()) {
        int idx = m_inputPorts.indexOf(m_savedInputName);
        if (idx >= 0) {
            m_selectedInputPort = idx;
            openInputPort(idx);
            emit selectedInputPortChanged();
        }
    }
    if (m_selectedOutputPort < 0 && !m_savedOutputName.isEmpty()) {
        int idx = m_outputPorts.indexOf(m_savedOutputName);
        if (idx >= 0) {
            m_selectedOutputPort = idx;
            openOutputPort(idx);
            emit selectedOutputPortChanged();
        }
    }
}
