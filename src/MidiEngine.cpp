#include "MidiEngine.h"
#include <QDebug>
#include <QMutexLocker>

static void midiCallbackStatic(double deltatime, std::vector<unsigned char>* message, void* userData) {
    auto* engine = static_cast<MidiEngine*>(userData);
    if (message && !message->empty()) {
        engine->processIncomingMidi(*message);
    }
}

MidiEngine::MidiEngine(QObject* parent) : QObject(parent) {
    try {
        m_midiIn  = std::make_unique<RtMidiIn>();
        m_midiOut = std::make_unique<RtMidiOut>();
        m_midiOutHW = std::make_unique<RtMidiOut>();
    } catch (RtMidiError& e) {
        emit errorOccurred(QString::fromStdString(e.getMessage()));
    }

    m_stepTimer = new QTimer(this);
    connect(m_stepTimer, &QTimer::timeout, this, &MidiEngine::advanceStep);

    // Initialize sequence with 16 empty steps
    m_sequence.resize(m_maxSteps);

    setupVirtualOutput();
    refreshPorts();
}

MidiEngine::~MidiEngine() {
    stopPlayback();
    if (m_midiIn) {
        try { m_midiIn->closePort(); } catch (...) {}
    }
    if (m_midiOut) {
        try { m_midiOut->closePort(); } catch (...) {}
    }
    if (m_midiOutHW) {
        try { m_midiOutHW->closePort(); } catch (...) {}
    }
}

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

void MidiEngine::refreshPorts() {
    m_inputPorts.clear();
    m_outputPorts.clear();

    if (m_midiIn) {
        int count = static_cast<int>(m_midiIn->getPortCount());
        for (int i = 0; i < count; ++i) {
            m_inputPorts << QString::fromStdString(m_midiIn->getPortName(i));
        }
    }

    // For output list, create a temporary RtMidiOut to query ports
    try {
        RtMidiOut tmpOut;
        int count = static_cast<int>(tmpOut.getPortCount());
        for (int i = 0; i < count; ++i) {
            m_outputPorts << QString::fromStdString(tmpOut.getPortName(i));
        }
    } catch (...) {}

    emit portsChanged();
}

void MidiEngine::openInputPort(int port) {
    if (!m_midiIn) return;
    try {
        if (m_midiIn->isPortOpen()) m_midiIn->closePort();
        m_midiIn->openPort(static_cast<unsigned int>(port));
        m_midiIn->ignoreTypes(false, true, true); // don't ignore sysex off, ignore timing/sensing
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
}

void MidiEngine::setSelectedOutputPort(int port) {
    if (m_selectedOutputPort == port) return;
    m_selectedOutputPort = port;
    if (port >= 0) openOutputPort(port);
    emit selectedOutputPortChanged();
}

void MidiEngine::setBpm(double bpm) {
    if (qFuzzyCompare(m_bpm, bpm)) return;
    m_bpm = bpm;
    if (m_stepTimer->isActive()) {
        int ms = static_cast<int>(60000.0 / m_bpm / 4.0); // 16th notes
        m_stepTimer->setInterval(ms);
    }
    emit bpmChanged();
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
    for (auto& ev : m_sequence) {
        ev = NoteEvent{};
    }
    emit sequenceChanged();
}

void MidiEngine::startMidiLearn(const QString& target) {
    m_midiLearnTargetStr = target;
    if (target == "record")       m_midiLearnTargetType = MidiLearnTarget::Record;
    else if (target == "play")    m_midiLearnTargetType = MidiLearnTarget::Play;
    else if (target == "stop")    m_midiLearnTargetType = MidiLearnTarget::Stop;
    else if (target == "clear")   m_midiLearnTargetType = MidiLearnTarget::Clear;
    else                          m_midiLearnTargetType = MidiLearnTarget::None;

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

QVariantList MidiEngine::sequence() const {
    QVariantList result;
    for (const auto& ev : m_sequence) {
        QVariantMap map;
        map["note"] = ev.note;
        map["velocity"] = ev.velocity;
        map["channel"] = ev.channel;
        map["active"] = (ev.velocity > 0);
        result << map;
    }
    return result;
}

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

    // MIDI Learn
    if (m_midiLearnActive) {
        if (isCC) {
            handleMidiLearn(static_cast<int>(data1), true);
        } else if (isNoteOn) {
            handleMidiLearn(static_cast<int>(data1), false);
        }
        return;
    }

    // Check control bindings (CC only)
    if (isCC) {
        int cc = static_cast<int>(data1);
        if (cc == m_recordButton && m_recordButton >= 0) {
            if (data2 > 0) {
                if (!m_recording) startRecording(); else stopRecording();
            }
            return;
        }
        if (cc == m_playButton && m_playButton >= 0) {
            if (data2 > 0) {
                if (!m_playing) startPlayback(); else stopPlayback();
            }
            return;
        }
        if (cc == m_stopButton && m_stopButton >= 0) {
            if (data2 > 0) {
                stopPlayback();
                stopRecording();
            }
            return;
        }
        if (cc == m_clearButton && m_clearButton >= 0) {
            if (data2 > 0) clearSequence();
            return;
        }
    }

    // Passthrough: forward to virtual output
    if (m_passthroughEnabled && m_midiOut && m_virtualPortOpen) {
        try {
            std::vector<unsigned char> fwd(msg.begin(), msg.end());
            m_midiOut->sendMessage(&fwd);
        } catch (...) {}
    }

    // Recording
    if (m_recording && isNoteOn) {
        QMutexLocker locker(&m_mutex);
        if (m_currentStep < m_maxSteps) {
            m_sequence[m_currentStep] = NoteEvent{
                static_cast<int>(data1),
                static_cast<int>(data2),
                channel,
                0.0
            };
            m_currentStep++;
            emit sequenceChanged();
            emit currentStepChanged();
            if (m_currentStep >= m_maxSteps) {
                m_recording = false;
                emit recordingChanged();
                // Auto-start playback
                QMetaObject::invokeMethod(this, "startPlayback", Qt::QueuedConnection);
            }
        }
    }

    // Emit for UI visualization
    if (isNoteOn) {
        emit noteReceived(data1, data2, channel);
    }
}

void MidiEngine::handleMidiLearn(int ccOrNote, bool isCC) {
    Q_UNUSED(isCC)
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
}

void MidiEngine::advanceStep() {
    if (!m_playing) return;

    // Stop previous note
    if (m_lastPlayedNote >= 0) {
        sendNoteOff(m_lastPlayedNote, m_lastPlayedChannel);
        m_lastPlayedNote = -1;
    }

    const NoteEvent& ev = m_sequence[m_currentStep];
    if (ev.velocity > 0) {
        sendNoteOn(ev.note, ev.velocity, ev.channel);
        m_lastPlayedNote = ev.note;
        m_lastPlayedChannel = ev.channel;
    }

    m_currentStep = (m_currentStep + 1) % m_maxSteps;
    emit currentStepChanged();
}

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
    if (m_lastPlayedNote >= 0) {
        sendNoteOff(m_lastPlayedNote, m_lastPlayedChannel);
        m_lastPlayedNote = -1;
    }
    // Send all-notes-off on all channels
    if (m_midiOut && m_virtualPortOpen) {
        try {
            for (int ch = 0; ch < 16; ++ch) {
                std::vector<unsigned char> msg = {
                    static_cast<unsigned char>(0xB0 | ch),
                    123, 0
                };
                m_midiOut->sendMessage(&msg);
            }
        } catch (...) {}
    }
}
