#pragma once

#include <QObject>
#include <QTimer>
#include <QVector>
#include <QVariantList>
#include <QMutex>
#include <QString>
#include <memory>
#include <rtmidi/RtMidi.h>

struct NoteEvent {
    int note = 0;
    int velocity = 0;
    int channel = 0;
    double timestamp = 0.0;
};

struct MidiLearnTarget {
    enum Type { None, Record, Play, Stop, Clear };
    Type type = None;
};

class MidiEngine : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
    Q_PROPERTY(int currentStep READ currentStep NOTIFY currentStepChanged)
    Q_PROPERTY(QVariantList sequence READ sequence NOTIFY sequenceChanged)
    Q_PROPERTY(QStringList inputPorts READ inputPorts NOTIFY portsChanged)
    Q_PROPERTY(QStringList outputPorts READ outputPorts NOTIFY portsChanged)
    Q_PROPERTY(int selectedInputPort READ selectedInputPort WRITE setSelectedInputPort NOTIFY selectedInputPortChanged)
    Q_PROPERTY(int selectedOutputPort READ selectedOutputPort WRITE setSelectedOutputPort NOTIFY selectedOutputPortChanged)
    Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY bpmChanged)
    Q_PROPERTY(bool midiLearnActive READ midiLearnActive NOTIFY midiLearnActiveChanged)
    Q_PROPERTY(QString midiLearnTarget READ midiLearnTarget NOTIFY midiLearnTargetChanged)
    Q_PROPERTY(bool passthroughEnabled READ passthroughEnabled WRITE setPassthroughEnabled NOTIFY passthroughEnabledChanged)
    Q_PROPERTY(int recordButton READ recordButton NOTIFY midiBindingsChanged)
    Q_PROPERTY(int playButton READ playButton NOTIFY midiBindingsChanged)
    Q_PROPERTY(int stopButton READ stopButton NOTIFY midiBindingsChanged)
    Q_PROPERTY(int clearButton READ clearButton NOTIFY midiBindingsChanged)

public:
    explicit MidiEngine(QObject* parent = nullptr);
    ~MidiEngine() override;

    bool isRecording() const { return m_recording; }
    bool isPlaying() const { return m_playing; }
    int currentStep() const { return m_currentStep; }
    QVariantList sequence() const;
    QStringList inputPorts() const { return m_inputPorts; }
    QStringList outputPorts() const { return m_outputPorts; }
    int selectedInputPort() const { return m_selectedInputPort; }
    int selectedOutputPort() const { return m_selectedOutputPort; }
    double bpm() const { return m_bpm; }
    bool midiLearnActive() const { return m_midiLearnActive; }
    QString midiLearnTarget() const { return m_midiLearnTargetStr; }
    bool passthroughEnabled() const { return m_passthroughEnabled; }
    int recordButton() const { return m_recordButton; }
    int playButton() const { return m_playButton; }
    int stopButton() const { return m_stopButton; }
    int clearButton() const { return m_clearButton; }

    void setSelectedInputPort(int port);
    void setSelectedOutputPort(int port);
    void setBpm(double bpm);
    void setPassthroughEnabled(bool enabled);

public slots:
    void startRecording();
    void stopRecording();
    void startPlayback();
    void stopPlayback();
    void clearSequence();
    void refreshPorts();
    void startMidiLearn(const QString& target);
    void cancelMidiLearn();

signals:
    void recordingChanged();
    void playingChanged();
    void currentStepChanged();
    void sequenceChanged();
    void portsChanged();
    void selectedInputPortChanged();
    void selectedOutputPortChanged();
    void bpmChanged();
    void midiLearnActiveChanged();
    void midiLearnTargetChanged();
    void passthroughEnabledChanged();
    void midiBindingsChanged();
    void noteReceived(int note, int velocity, int channel);
    void errorOccurred(const QString& message);

public:
    void processIncomingMidi(const std::vector<unsigned char>& message);

private:
    void setupVirtualOutput();
    void openInputPort(int port);
    void openOutputPort(int port);
    void handleMidiLearn(int ccOrNote, bool isCC);
    void advanceStep();
    void sendNoteOn(int note, int velocity, int channel);
    void sendNoteOff(int note, int channel);
    void stopAllNotes();
    static void midiCallback(double deltatime, std::vector<unsigned char>* message, void* userData);

    std::unique_ptr<RtMidiIn>  m_midiIn;
    std::unique_ptr<RtMidiOut> m_midiOut;   // virtual output (LoopMidi port)
    std::unique_ptr<RtMidiOut> m_midiOutHW; // hardware output

    QVector<NoteEvent> m_sequence;
    int m_maxSteps = 16;
    bool m_recording = false;
    bool m_playing = false;
    int m_currentStep = -1;
    double m_bpm = 120.0;

    QTimer* m_stepTimer = nullptr;
    int m_lastPlayedNote = -1;
    int m_lastPlayedChannel = 0;

    QStringList m_inputPorts;
    QStringList m_outputPorts;
    int m_selectedInputPort = -1;
    int m_selectedOutputPort = -1;

    bool m_midiLearnActive = false;
    QString m_midiLearnTargetStr;
    MidiLearnTarget::Type m_midiLearnTargetType = MidiLearnTarget::None;

    bool m_passthroughEnabled = true;

    // MIDI CC/note bindings (-1 = unbound)
    int m_recordButton = -1;
    int m_playButton   = -1;
    int m_stopButton   = -1;
    int m_clearButton  = -1;

    QMutex m_mutex;
    bool m_virtualPortOpen = false;
};
