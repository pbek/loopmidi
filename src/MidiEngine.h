#pragma once

#include <QMutex>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVector>
#include <QtQml/qqmlregistration.h>
#include <memory>
#include <rtmidi/RtMidi.h>

struct NoteEvent {
  int note = 0;
  int velocity = 0;
  int channel = 0;
};

// One step = zero or more simultaneous notes (a chord)
using ChordStep = QVector<NoteEvent>;

struct MidiLearnTarget {
  enum Type { None, Record, Play, Stop, Clear, TapTempo };
  Type type = None;
};

struct InstrumentSlot {
  bool enabled = true;
  QString name;
  QString pluginFormat;
  QString pluginId;
  QString pluginPath;
  QString presetName;
};

struct AvailablePlugin {
  QString name;
  QString pluginFormat;
  QString pluginId;
  QString path;
};

class MidiEngine : public QObject {
  Q_OBJECT
  QML_ELEMENT
  Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
  Q_PROPERTY(bool playing READ isPlaying NOTIFY playingChanged)
  Q_PROPERTY(int currentStep READ currentStep NOTIFY currentStepChanged)
  Q_PROPERTY(int recordingStep READ recordingStep NOTIFY recordingStepChanged)
  Q_PROPERTY(QVariantList sequence READ sequence NOTIFY sequenceChanged)
  Q_PROPERTY(int trackCount READ trackCount CONSTANT)
  Q_PROPERTY(int activeTrack READ activeTrack WRITE setActiveTrack NOTIFY
                 activeTrackChanged)
  Q_PROPERTY(int activeTrackMidiChannel READ activeTrackMidiChannel WRITE
                 setActiveTrackMidiChannel NOTIFY activeTrackMidiChannelChanged)
  Q_PROPERTY(QVariantList instrumentRack READ instrumentRack NOTIFY
                 instrumentRackChanged)
  Q_PROPERTY(QVariantList availablePlugins READ availablePlugins NOTIFY
                 availablePluginsChanged)
  Q_PROPERTY(QString activeInstrumentName READ activeInstrumentName WRITE
                 setActiveInstrumentName NOTIFY activeInstrumentChanged)
  Q_PROPERTY(QString activeInstrumentFormat READ activeInstrumentFormat WRITE
                 setActiveInstrumentFormat NOTIFY activeInstrumentChanged)
  Q_PROPERTY(
      QString activeInstrumentPluginId READ activeInstrumentPluginId WRITE
          setActiveInstrumentPluginId NOTIFY activeInstrumentChanged)
  Q_PROPERTY(QString activeInstrumentPluginPath READ activeInstrumentPluginPath
                 NOTIFY activeInstrumentChanged)
  Q_PROPERTY(
      QString activeInstrumentPresetName READ activeInstrumentPresetName WRITE
          setActiveInstrumentPresetName NOTIFY activeInstrumentChanged)
  Q_PROPERTY(bool activeInstrumentEnabled READ activeInstrumentEnabled WRITE
                 setActiveInstrumentEnabled NOTIFY activeInstrumentChanged)
  Q_PROPERTY(
      bool pluginHostRunning READ pluginHostRunning NOTIFY pluginHostChanged)
  Q_PROPERTY(
      QString pluginHostStatus READ pluginHostStatus NOTIFY pluginHostChanged)
  Q_PROPERTY(bool pluginHostAutoConnectAudio READ pluginHostAutoConnectAudio
                 WRITE setPluginHostAutoConnectAudio NOTIFY pluginHostChanged)
  Q_PROPERTY(bool recordAllBeats READ recordAllBeats WRITE setRecordAllBeats
                 NOTIFY recordAllBeatsChanged)
  Q_PROPERTY(QStringList inputPorts READ inputPorts NOTIFY portsChanged)
  Q_PROPERTY(QStringList outputPorts READ outputPorts NOTIFY portsChanged)
  Q_PROPERTY(int selectedInputPort READ selectedInputPort WRITE
                 setSelectedInputPort NOTIFY selectedInputPortChanged)
  Q_PROPERTY(int selectedOutputPort READ selectedOutputPort WRITE
                 setSelectedOutputPort NOTIFY selectedOutputPortChanged)
  Q_PROPERTY(double bpm READ bpm WRITE setBpm NOTIFY bpmChanged)
  Q_PROPERTY(QString projectName READ projectName WRITE setProjectName NOTIFY
                 projectNameChanged)
  Q_PROPERTY(QString projectFilePath READ projectFilePath NOTIFY
                 projectFilePathChanged)
  Q_PROPERTY(
      QString projectFileName READ projectFileName NOTIFY projectNameChanged)
  Q_PROPERTY(
      bool midiLearnActive READ midiLearnActive NOTIFY midiLearnActiveChanged)
  Q_PROPERTY(QString midiLearnTarget READ midiLearnTarget NOTIFY
                 midiLearnTargetChanged)
  Q_PROPERTY(bool passthroughEnabled READ passthroughEnabled WRITE
                 setPassthroughEnabled NOTIFY passthroughEnabledChanged)
  Q_PROPERTY(int recordButton READ recordButton NOTIFY midiBindingsChanged)
  Q_PROPERTY(int playButton READ playButton NOTIFY midiBindingsChanged)
  Q_PROPERTY(int stopButton READ stopButton NOTIFY midiBindingsChanged)
  Q_PROPERTY(int clearButton READ clearButton NOTIFY midiBindingsChanged)
  Q_PROPERTY(int tapTempoButton READ tapTempoButton NOTIFY midiBindingsChanged)
  Q_PROPERTY(bool recordButtonIsNote READ recordButtonIsNote NOTIFY
                 midiBindingsChanged)
  Q_PROPERTY(
      bool playButtonIsNote READ playButtonIsNote NOTIFY midiBindingsChanged)
  Q_PROPERTY(
      bool stopButtonIsNote READ stopButtonIsNote NOTIFY midiBindingsChanged)
  Q_PROPERTY(
      bool clearButtonIsNote READ clearButtonIsNote NOTIFY midiBindingsChanged)
  Q_PROPERTY(bool tapTempoButtonIsNote READ tapTempoButtonIsNote NOTIFY
                 midiBindingsChanged)
  Q_PROPERTY(
      int stepRecordTarget READ stepRecordTarget NOTIFY stepRecordTargetChanged)
  Q_PROPERTY(int cursorStep READ cursorStep NOTIFY cursorStepChanged)

public:
  explicit MidiEngine(QObject *parent = nullptr);
  ~MidiEngine() override;

  bool isRecording() const { return m_recording; }
  bool isPlaying() const { return m_playing; }
  int currentStep() const { return m_currentStep; }
  int recordingStep() const { return m_recordStep; }
  QVariantList sequence() const;
  int trackCount() const { return m_trackCount; }
  int activeTrack() const { return m_activeTrack; }
  int activeTrackMidiChannel() const;
  QVariantList instrumentRack() const;
  QVariantList availablePlugins() const;
  QString activeInstrumentName() const;
  QString activeInstrumentFormat() const;
  QString activeInstrumentPluginId() const;
  QString activeInstrumentPluginPath() const;
  QString activeInstrumentPresetName() const;
  bool activeInstrumentEnabled() const;
  bool pluginHostRunning() const { return m_pluginHostRunning; }
  QString pluginHostStatus() const { return m_pluginHostStatus; }
  bool pluginHostAutoConnectAudio() const {
    return m_pluginHostAutoConnectAudio;
  }
  bool recordAllBeats() const { return m_recordAllBeats; }
  QStringList inputPorts() const { return m_inputPorts; }
  QStringList outputPorts() const { return m_outputPorts; }
  int selectedInputPort() const { return m_selectedInputPort; }
  int selectedOutputPort() const { return m_selectedOutputPort; }
  double bpm() const { return m_bpm; }
  QString projectName() const { return m_projectName; }
  QString projectFilePath() const { return m_projectFilePath; }
  QString projectFileName() const;
  bool midiLearnActive() const { return m_midiLearnActive; }
  QString midiLearnTarget() const { return m_midiLearnTargetStr; }
  bool passthroughEnabled() const { return m_passthroughEnabled; }
  int recordButton() const { return m_recordButton; }
  int playButton() const { return m_playButton; }
  int stopButton() const { return m_stopButton; }
  int clearButton() const { return m_clearButton; }
  int tapTempoButton() const { return m_tapTempoButton; }
  bool recordButtonIsNote() const { return m_recordButtonIsNote; }
  bool playButtonIsNote() const { return m_playButtonIsNote; }
  bool stopButtonIsNote() const { return m_stopButtonIsNote; }
  bool clearButtonIsNote() const { return m_clearButtonIsNote; }
  bool tapTempoButtonIsNote() const { return m_tapTempoButtonIsNote; }
  int stepRecordTarget() const { return m_stepRecordTarget; }
  int cursorStep() const { return m_cursorStep; }

  void setSelectedInputPort(int port);
  void setSelectedOutputPort(int port);
  void setBpm(double bpm);
  void setProjectName(const QString &name);
  void setPassthroughEnabled(bool enabled);
  void setActiveTrack(int track);
  void setActiveTrackMidiChannel(int channel);
  void setActiveInstrumentName(const QString &name);
  void setActiveInstrumentFormat(const QString &format);
  void setActiveInstrumentPluginId(const QString &pluginId);
  void setActiveInstrumentPresetName(const QString &presetName);
  void setActiveInstrumentEnabled(bool enabled);
  void setPluginHostAutoConnectAudio(bool enabled);
  void setRecordAllBeats(bool enabled);

public slots:
  void startRecording();
  void stopRecording();
  void startPlayback();
  void stopPlayback();
  void clearSequence();
  void clearStep(int index);
  void recordStep(int index);
  void setCursorStep(int index);
  void tapTempo();
  void refreshPorts();
  void scanPlugins();
  void setActiveInstrumentFromAvailablePlugin(int index);
  void startPluginHost();
  void stopPluginHost();
  bool saveProject(const QString &filePath);
  bool loadProject(const QString &filePath);
  void startMidiLearn(const QString &target);
  void cancelMidiLearn();

signals:
  void recordingChanged();
  void playingChanged();
  void currentStepChanged();
  void recordingStepChanged();
  void sequenceChanged();
  void activeTrackChanged();
  void activeTrackMidiChannelChanged();
  void activeInstrumentChanged();
  void instrumentRackChanged();
  void availablePluginsChanged();
  void pluginHostChanged();
  void recordAllBeatsChanged();
  void portsChanged();
  void selectedInputPortChanged();
  void selectedOutputPortChanged();
  void bpmChanged();
  void projectNameChanged();
  void projectFilePathChanged();
  void midiLearnActiveChanged();
  void midiLearnTargetChanged();
  void passthroughEnabledChanged();
  void midiBindingsChanged();
  void noteReceived(int note, int velocity, int channel);
  void errorOccurred(const QString &message);
  void stepRecordTargetChanged();
  void cursorStepChanged();

public:
  void processIncomingMidi(const std::vector<unsigned char> &message);

private:
  void setupVirtualOutput();
  void openInputPort(int port);
  void openOutputPort(int port);
  void handleMidiLearn(int ccOrNote, bool isCC);
  void advanceStep();
  void commitChordStep(); // close current chord window, move to next step
  void sendNoteOn(int note, int velocity, int channel);
  void sendNoteOff(int note, int channel);
  void stopAllNotes();
  void pollPorts();
  void loadSettings();
  void saveSettings() const;
  void tryRestoreSavedPorts();
  bool triggerBoundAction(int value, bool isNote);
  InstrumentSlot defaultInstrumentSlot(int trackIndex) const;
  InstrumentSlot activeInstrumentSlot() const;
  void emitInstrumentChanges();
  QStringList pluginSearchPaths(const QString &format) const;
  void addAvailablePlugin(const QString &name, const QString &format,
                          const QString &pluginId, const QString &path);
  bool resolveInstrumentSlotPlugin(int trackIndex);
  bool jackServerAvailable(QString *errorMessage) const;
  QString pluginHostExecutable(const QString &format) const;
  QString pluginHostClientName(int trackIndex) const;
  QString pwJackExecutable() const;
  void appendPluginHostOutput(QProcess *process, int trackIndex,
                              const QString &output);
  void connectPluginHostAudio(const QString &clientName);
  void connectPluginHostMidi(const QString &clientName);
  QString normalizedProjectPath(const QString &filePath) const;
  static QString defaultProjectName();
  static QString projectNameToFileName(const QString &name);
  static void midiCallback(double deltatime,
                           std::vector<unsigned char> *message, void *userData);

  std::unique_ptr<RtMidiIn> m_midiIn;
  std::unique_ptr<RtMidiOut> m_midiOut;   // virtual output (LoopMidi port)
  std::unique_ptr<RtMidiOut> m_midiOutHW; // hardware output

  // Tracks: each track has m_maxSteps chord steps.
  QVector<QVector<ChordStep>> m_tracks;
  QVector<int> m_trackMidiChannels;
  QVector<InstrumentSlot> m_instrumentRack;
  QVector<AvailablePlugin> m_availablePlugins;
  QVector<QProcess *> m_pluginHostProcesses;
  bool m_pluginHostRunning = false;
  bool m_pluginHostAutoConnectAudio = true;
  bool m_stoppingPluginHost = false;
  QString m_pluginHostStatus = QStringLiteral("Plugin host stopped");
  int m_trackCount = 4;
  int m_activeTrack = 0;
  int m_maxSteps = 16;
  bool m_recording = false;
  bool m_playing = false;
  int m_currentStep = -1;
  int m_recordStep = -1;
  bool m_recordAllBeats = true;
  double m_bpm = 120.0;
  QString m_projectName;
  QString m_projectFilePath;

  QTimer *m_stepTimer = nullptr;
  QTimer *m_hotplugTimer = nullptr;
  QTimer *m_chordTimer = nullptr; // 30 ms window to group simultaneous notes

  // Notes currently sounding during playback (for Note-Off on next tick)
  QVector<NoteEvent> m_activePlaybackNotes;

  QStringList m_inputPorts;
  QStringList m_outputPorts;
  int m_selectedInputPort = -1;
  int m_selectedOutputPort = -1;
  QString m_savedInputName;
  QString m_savedOutputName;

  bool m_midiLearnActive = false;
  QString m_midiLearnTargetStr;
  MidiLearnTarget::Type m_midiLearnTargetType = MidiLearnTarget::None;

  bool m_passthroughEnabled = true;

  // MIDI CC/note bindings (-1 = unbound)
  int m_recordButton = -1;
  int m_playButton = -1;
  int m_stopButton = -1;
  int m_clearButton = -1;
  int m_tapTempoButton = -1;
  bool m_recordButtonIsNote = false;
  bool m_playButtonIsNote = false;
  bool m_stopButtonIsNote = false;
  bool m_clearButtonIsNote = false;
  bool m_tapTempoButtonIsNote = false;

  QVector<qint64> m_tapTempoTimes;

  // Single-step re-record target (-1 = none)
  int m_stepRecordTarget = -1;

  // Manual cursor: where the next recording will start (-1 = auto = first empty
  // step)
  int m_cursorStep = -1;

  QMutex m_mutex;
  bool m_virtualPortOpen = false;
};
