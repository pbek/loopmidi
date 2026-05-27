#include "MidiEngine.h"
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QUrl>

static const int kChordWindowMs =
    30; // notes within this window are grouped into one step
static const int kTapTempoResetMs = 2000;
static const int kTapTempoMaxTaps = 4;
static const char *kProjectFormat = "LoopMidiProject";
static const int kProjectVersion = 1;

static void midiCallbackStatic(double deltatime,
                               std::vector<unsigned char> *message,
                               void *userData) {
  auto *engine = static_cast<MidiEngine *>(userData);
  if (message && !message->empty()) {
    engine->processIncomingMidi(*message);
  }
}

MidiEngine::MidiEngine(QObject *parent) : QObject(parent) {
  m_projectName = defaultProjectName();

  try {
    m_midiIn = std::make_unique<RtMidiIn>();
    m_midiOut = std::make_unique<RtMidiOut>();
    m_midiOutHW = std::make_unique<RtMidiOut>();
  } catch (RtMidiError &e) {
    emit errorOccurred(QString::fromStdString(e.getMessage()));
  }

  m_stepTimer = new QTimer(this);
  connect(m_stepTimer, &QTimer::timeout, this, &MidiEngine::advanceStep);

  m_hotplugTimer = new QTimer(this);
  m_hotplugTimer->setInterval(1000);
  connect(m_hotplugTimer, &QTimer::timeout, this, &MidiEngine::pollPorts);
  m_hotplugTimer->start();

  // Chord grouping timer: single-shot, fires after kChordWindowMs to close a
  // chord step
  m_chordTimer = new QTimer(this);
  m_chordTimer->setSingleShot(true);
  m_chordTimer->setInterval(kChordWindowMs);
  connect(m_chordTimer, &QTimer::timeout, this, &MidiEngine::commitChordStep);

  m_tracks.resize(m_trackCount);
  for (auto &track : m_tracks)
    track.resize(m_maxSteps);
  m_trackMidiChannels.resize(m_trackCount);
  for (int i = 0; i < m_trackCount; ++i)
    m_trackMidiChannels[i] = qMin(i, 15);

  setupVirtualOutput();
  loadSettings();
  refreshPorts();
  tryRestoreSavedPorts();
}

MidiEngine::~MidiEngine() {
  stopPlayback();
  if (m_midiIn)
    try {
      m_midiIn->closePort();
    } catch (...) {
    }
  if (m_midiOut)
    try {
      m_midiOut->closePort();
    } catch (...) {
    }
  if (m_midiOutHW)
    try {
      m_midiOutHW->closePort();
    } catch (...) {
    }
}

// ── Virtual port ─────────────────────────────────────────────────────────────

void MidiEngine::setupVirtualOutput() {
  if (!m_midiOut)
    return;
  try {
    m_midiOut->openVirtualPort("LoopMidi Output");
    m_virtualPortOpen = true;
    qDebug() << "Virtual MIDI port 'LoopMidi Output' opened";
  } catch (RtMidiError &e) {
    qWarning() << "Could not open virtual MIDI port:"
               << QString::fromStdString(e.getMessage());
    emit errorOccurred(QString("Virtual port: ") +
                       QString::fromStdString(e.getMessage()));
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
  } catch (...) {
  }

  emit portsChanged();
}

void MidiEngine::pollPorts() {
  QStringList newInputs, newOutputs;

  if (m_midiIn) {
    try {
      int count = static_cast<int>(m_midiIn->getPortCount());
      for (int i = 0; i < count; ++i)
        newInputs << QString::fromStdString(m_midiIn->getPortName(i));
    } catch (...) {
    }
  }
  try {
    RtMidiOut tmpOut;
    int count = static_cast<int>(tmpOut.getPortCount());
    for (int i = 0; i < count; ++i)
      newOutputs << QString::fromStdString(tmpOut.getPortName(i));
  } catch (...) {
  }

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
      try {
        m_midiIn->closePort();
      } catch (...) {
      }
    activeInputName.clear();
  }
  // If the active output disappeared, close it
  if (!activeOutputName.isEmpty() && !newOutputs.contains(activeOutputName)) {
    if (m_midiOutHW && m_midiOutHW->isPortOpen())
      try {
        m_midiOutHW->closePort();
      } catch (...) {
      }
    activeOutputName.clear();
  }

  m_inputPorts = newInputs;
  m_outputPorts = newOutputs;

  // Re-derive indices from names so the ComboBox selection survives list
  // changes
  int newInIdx =
      activeInputName.isEmpty() ? -1 : m_inputPorts.indexOf(activeInputName);
  int newOutIdx =
      activeOutputName.isEmpty() ? -1 : m_outputPorts.indexOf(activeOutputName);

  bool inChanged = (newInIdx != m_selectedInputPort);
  bool outChanged = (newOutIdx != m_selectedOutputPort);
  m_selectedInputPort = newInIdx;
  m_selectedOutputPort = newOutIdx;

  emit portsChanged();
  if (inChanged)
    emit selectedInputPortChanged();
  if (outChanged)
    emit selectedOutputPortChanged();

  tryRestoreSavedPorts();
}

void MidiEngine::openInputPort(int port) {
  if (!m_midiIn)
    return;
  try {
    if (m_midiIn->isPortOpen())
      m_midiIn->closePort();
    m_midiIn->openPort(static_cast<unsigned int>(port));
    m_midiIn->ignoreTypes(false, true, true);
    m_midiIn->setCallback(&midiCallbackStatic, this);
    qDebug() << "Opened input port" << port;
  } catch (RtMidiError &e) {
    emit errorOccurred(QString::fromStdString(e.getMessage()));
  }
}

void MidiEngine::openOutputPort(int port) {
  if (!m_midiOutHW)
    return;
  try {
    if (m_midiOutHW->isPortOpen())
      m_midiOutHW->closePort();
    m_midiOutHW->openPort(static_cast<unsigned int>(port));
    qDebug() << "Opened output port" << port;
  } catch (RtMidiError &e) {
    emit errorOccurred(QString::fromStdString(e.getMessage()));
  }
}

void MidiEngine::setSelectedInputPort(int port) {
  if (m_selectedInputPort == port)
    return;
  m_selectedInputPort = port;
  if (port >= 0)
    openInputPort(port);
  emit selectedInputPortChanged();
  saveSettings();
}

void MidiEngine::setSelectedOutputPort(int port) {
  if (m_selectedOutputPort == port)
    return;
  m_selectedOutputPort = port;
  if (port >= 0)
    openOutputPort(port);
  emit selectedOutputPortChanged();
  saveSettings();
}

// ── Transport & BPM ──────────────────────────────────────────────────────────

void MidiEngine::setBpm(double bpm) {
  bpm = qBound(40.0, bpm, 240.0);
  if (qFuzzyCompare(m_bpm, bpm))
    return;
  m_bpm = bpm;
  if (m_stepTimer->isActive()) {
    int ms = static_cast<int>(60000.0 / m_bpm / 4.0);
    m_stepTimer->setInterval(ms);
  }
  emit bpmChanged();
  saveSettings();
}

void MidiEngine::setProjectName(const QString &name) {
  const QString trimmed = name.trimmed();
  const QString nextName = trimmed.isEmpty() ? defaultProjectName() : trimmed;
  if (m_projectName == nextName)
    return;
  m_projectName = nextName;
  emit projectNameChanged();
}

QString MidiEngine::projectFileName() const {
  return projectNameToFileName(m_projectName);
}

void MidiEngine::setPassthroughEnabled(bool enabled) {
  if (m_passthroughEnabled == enabled)
    return;
  m_passthroughEnabled = enabled;
  emit passthroughEnabledChanged();
}

void MidiEngine::setActiveTrack(int track) {
  track = qBound(0, track, m_trackCount - 1);
  if (m_activeTrack == track)
    return;
  m_activeTrack = track;
  if (m_recording)
    stopRecording();
  emit activeTrackChanged();
  emit activeTrackMidiChannelChanged();
  emit sequenceChanged();
}

int MidiEngine::activeTrackMidiChannel() const {
  if (m_activeTrack < 0 || m_activeTrack >= m_trackMidiChannels.size())
    return 1;
  return m_trackMidiChannels[m_activeTrack] + 1;
}

void MidiEngine::setActiveTrackMidiChannel(int channel) {
  if (m_activeTrack < 0 || m_activeTrack >= m_trackMidiChannels.size())
    return;
  const int nextChannel = qBound(1, channel, 16) - 1;
  if (m_trackMidiChannels[m_activeTrack] == nextChannel)
    return;
  m_trackMidiChannels[m_activeTrack] = nextChannel;
  emit activeTrackMidiChannelChanged();
  saveSettings();
}

void MidiEngine::setRecordAllBeats(bool enabled) {
  if (m_recordAllBeats == enabled)
    return;
  if (m_recording)
    stopRecording();
  m_recordAllBeats = enabled;
  emit recordAllBeatsChanged();
  saveSettings();
}

void MidiEngine::startRecording() {
  if (m_recording)
    return;

  QVector<ChordStep> &sequence = m_tracks[m_activeTrack];

  int startStep = -1;
  if (!m_recordAllBeats && m_playing) {
    startStep = m_currentStep;
  } else {
    // Determine start step:
    // 1. If a cursor has been set manually, use it.
    // 2. Otherwise find the first empty step.
    // 3. If all steps are filled, start from 0 (overwrite from beginning).
    startStep = m_cursorStep;
    if (startStep < 0) {
      startStep = 0;
      for (int i = 0; i < m_maxSteps; ++i) {
        if (sequence[i].isEmpty()) {
          startStep = i;
          break;
        }
      }
    }
  }

  // Consume the cursor so the next recording auto-detects again
  if (m_cursorStep >= 0) {
    m_cursorStep = -1;
    emit cursorStepChanged();
  }

  m_recording = true;
  m_recordStep = startStep;
  if (!m_playing)
    m_currentStep = startStep;
  emit recordingChanged();
  emit recordingStepChanged();
  if (!m_playing)
    emit currentStepChanged();
}

void MidiEngine::stopRecording() {
  if (!m_recording)
    return;
  m_chordTimer->stop();
  m_recording = false;
  m_recordStep = -1;
  if (m_stepRecordTarget >= 0) {
    m_stepRecordTarget = -1;
    emit stepRecordTargetChanged();
  }
  emit recordingChanged();
  emit recordingStepChanged();
}

void MidiEngine::startPlayback() {
  if (m_playing)
    return;
  m_playing = true;
  m_currentStep = 0;
  int ms = static_cast<int>(60000.0 / m_bpm / 4.0);
  m_stepTimer->start(ms);
  emit playingChanged();
  emit currentStepChanged();
}

void MidiEngine::stopPlayback() {
  if (!m_playing)
    return;
  m_stepTimer->stop();
  m_playing = false;
  stopAllNotes();
  m_currentStep = -1;
  emit playingChanged();
  emit currentStepChanged();
}

void MidiEngine::clearSequence() {
  stopAllNotes();
  for (auto &step : m_tracks[m_activeTrack])
    step.clear();
  emit sequenceChanged();
}

QString MidiEngine::normalizedProjectPath(const QString &filePath) const {
  QUrl url(filePath);
  QString path = url.isLocalFile() ? url.toLocalFile() : filePath;
  if (path.startsWith(QStringLiteral("file://")))
    path = QUrl(path).toLocalFile();
  return path;
}

QString MidiEngine::defaultProjectName() {
  return QStringLiteral("LoopMidi %1")
      .arg(QDate::currentDate().toString(Qt::ISODate));
}

QString MidiEngine::projectNameToFileName(const QString &name) {
  QString fileName = name.trimmed();
  if (fileName.isEmpty())
    fileName = defaultProjectName();

  fileName.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]+")),
                   QStringLiteral("-"));
  fileName.replace(QRegularExpression(QStringLiteral("\\s+")),
                   QStringLiteral(" "));
  fileName = fileName.trimmed();

  if (fileName.isEmpty())
    fileName = defaultProjectName();
  if (!fileName.endsWith(QStringLiteral(".loopmidi"), Qt::CaseInsensitive))
    fileName += QStringLiteral(".loopmidi");
  return fileName;
}

bool MidiEngine::saveProject(const QString &filePath) {
  QString path = normalizedProjectPath(filePath);
  if (path.isEmpty()) {
    emit errorOccurred(QStringLiteral("Choose a project file to save."));
    return false;
  }

  if (QFileInfo(path).suffix().isEmpty())
    path += QStringLiteral(".loopmidi");

  QJsonObject root;
  root["format"] = kProjectFormat;
  root["version"] = kProjectVersion;
  root["name"] = m_projectName;
  root["bpm"] = m_bpm;
  root["activeTrack"] = m_activeTrack;
  root["recordAllBeats"] = m_recordAllBeats;
  root["trackCount"] = m_trackCount;
  root["stepsPerTrack"] = m_maxSteps;

  QJsonArray trackMidiChannels;
  for (int channel : m_trackMidiChannels)
    trackMidiChannels.append(channel + 1);
  root["trackMidiChannels"] = trackMidiChannels;

  QJsonArray tracks;
  {
    QMutexLocker locker(&m_mutex);
    for (const auto &track : m_tracks) {
      QJsonArray steps;
      for (const auto &step : track) {
        QJsonArray notes;
        for (const auto &ev : step) {
          QJsonObject note;
          note["note"] = ev.note;
          note["velocity"] = ev.velocity;
          note["channel"] = ev.channel;
          notes.append(note);
        }
        steps.append(notes);
      }
      tracks.append(steps);
    }
  }
  root["tracks"] = tracks;

  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    emit errorOccurred(QStringLiteral("Could not save project: ") +
                       file.errorString());
    return false;
  }

  file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
  file.close();

  if (m_projectFilePath != path) {
    m_projectFilePath = path;
    emit projectFilePathChanged();
  }
  return true;
}

bool MidiEngine::loadProject(const QString &filePath) {
  const QString path = normalizedProjectPath(filePath);
  if (path.isEmpty()) {
    emit errorOccurred(QStringLiteral("Choose a project file to load."));
    return false;
  }

  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    emit errorOccurred(QStringLiteral("Could not load project: ") +
                       file.errorString());
    return false;
  }

  QJsonParseError parseError;
  const QJsonDocument doc =
      QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
    emit errorOccurred(QStringLiteral("Invalid project file: ") +
                       parseError.errorString());
    return false;
  }

  const QJsonObject root = doc.object();
  if (root.value("format").toString() != QString::fromLatin1(kProjectFormat)) {
    emit errorOccurred(QStringLiteral("Invalid LoopMidi project file."));
    return false;
  }

  const QJsonArray tracksJson = root.value("tracks").toArray();
  if (tracksJson.isEmpty()) {
    emit errorOccurred(QStringLiteral("Project file contains no tracks."));
    return false;
  }

  QVector<QVector<ChordStep>> loadedTracks(m_trackCount);
  for (auto &track : loadedTracks)
    track.resize(m_maxSteps);

  QVector<int> loadedTrackMidiChannels(m_trackCount);
  for (int i = 0; i < m_trackCount; ++i)
    loadedTrackMidiChannels[i] = qMin(i, 15);

  const QJsonArray trackMidiChannelsJson =
      root.value("trackMidiChannels").toArray();
  for (int i = 0;
       i < qMin(m_trackCount, static_cast<int>(trackMidiChannelsJson.size()));
       ++i) {
    loadedTrackMidiChannels[i] =
        qBound(1, trackMidiChannelsJson[i].toInt(i + 1), 16) - 1;
  }

  for (int trackIndex = 0;
       trackIndex < qMin(m_trackCount, static_cast<int>(tracksJson.size()));
       ++trackIndex) {
    const QJsonArray stepsJson = tracksJson[trackIndex].toArray();
    for (int stepIndex = 0;
         stepIndex < qMin(m_maxSteps, static_cast<int>(stepsJson.size()));
         ++stepIndex) {
      const QJsonArray notesJson = stepsJson[stepIndex].toArray();
      for (const QJsonValue &noteValue : notesJson) {
        const QJsonObject note = noteValue.toObject();
        NoteEvent ev;
        ev.note = qBound(0, note.value("note").toInt(), 127);
        ev.velocity = qBound(0, note.value("velocity").toInt(), 127);
        ev.channel = qBound(0, note.value("channel").toInt(), 15);
        loadedTracks[trackIndex][stepIndex].append(ev);
      }
    }
  }

  stopRecording();
  stopPlayback();
  m_chordTimer->stop();
  stopAllNotes();

  const bool activeTrackMidiChannelChangedNow =
      m_trackMidiChannels != loadedTrackMidiChannels;

  {
    QMutexLocker locker(&m_mutex);
    m_tracks = loadedTracks;
    m_trackMidiChannels = loadedTrackMidiChannels;
  }

  const QString loadedName = root.value("name").toString(defaultProjectName());
  const QString nextName = loadedName.trimmed().isEmpty()
                               ? defaultProjectName()
                               : loadedName.trimmed();
  const bool nameChanged = m_projectName != nextName;
  const int nextActiveTrack =
      qBound(0, root.value("activeTrack").toInt(0), m_trackCount - 1);
  const bool activeTrackChangedNow = m_activeTrack != nextActiveTrack;
  const bool recordAllBeatsChangedNow =
      m_recordAllBeats != root.value("recordAllBeats").toBool(true);
  const bool cursorChanged = m_cursorStep != -1;

  m_projectName = nextName;
  m_bpm = qBound(40.0, root.value("bpm").toDouble(120.0), 240.0);
  m_activeTrack = nextActiveTrack;
  m_recordAllBeats = root.value("recordAllBeats").toBool(true);
  m_cursorStep = -1;
  m_recordStep = -1;
  m_stepRecordTarget = -1;

  if (m_projectFilePath != path) {
    m_projectFilePath = path;
    emit projectFilePathChanged();
  }
  if (nameChanged)
    emit projectNameChanged();
  emit bpmChanged();
  if (activeTrackChangedNow)
    emit activeTrackChanged();
  if (activeTrackChangedNow || activeTrackMidiChannelChangedNow)
    emit activeTrackMidiChannelChanged();
  if (recordAllBeatsChangedNow)
    emit recordAllBeatsChanged();
  if (cursorChanged)
    emit cursorStepChanged();
  emit sequenceChanged();
  saveSettings();
  return true;
}

void MidiEngine::clearStep(int index) {
  if (index < 0 || index >= m_maxSteps)
    return;
  m_tracks[m_activeTrack][index].clear();
  emit sequenceChanged();
}

// Arm a single step for re-recording: the next chord played will overwrite
// that step, then recording stops automatically (playback is not interrupted).
void MidiEngine::recordStep(int index) {
  if (index < 0 || index >= m_maxSteps)
    return;
  // Cancel any ongoing full recording first
  if (m_recording)
    stopRecording();
  // Clear the target step so the new notes replace it fully
  m_tracks[m_activeTrack][index].clear();
  m_stepRecordTarget = index;
  m_recordStep = index;
  if (!m_playing)
    m_currentStep = index;
  m_recording = true;
  emit stepRecordTargetChanged();
  emit recordingChanged();
  emit recordingStepChanged();
  if (!m_playing)
    emit currentStepChanged();
  emit sequenceChanged();
}

void MidiEngine::setCursorStep(int index) {
  // index == -1 clears the manual cursor (revert to auto)
  int clamped = (index < 0) ? -1 : qBound(0, index, m_maxSteps - 1);
  if (m_cursorStep == clamped)
    return;
  m_cursorStep = clamped;
  emit cursorStepChanged();
}

void MidiEngine::tapTempo() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  if (!m_tapTempoTimes.isEmpty() &&
      now - m_tapTempoTimes.last() > kTapTempoResetMs) {
    m_tapTempoTimes.clear();
  }

  m_tapTempoTimes.append(now);
  while (m_tapTempoTimes.size() > kTapTempoMaxTaps)
    m_tapTempoTimes.removeFirst();

  if (m_tapTempoTimes.size() < 2)
    return;

  qint64 totalInterval = 0;
  for (int i = 1; i < m_tapTempoTimes.size(); ++i)
    totalInterval += m_tapTempoTimes[i] - m_tapTempoTimes[i - 1];

  const double averageInterval =
      static_cast<double>(totalInterval) / (m_tapTempoTimes.size() - 1);
  if (averageInterval <= 0.0)
    return;

  setBpm(60000.0 / averageInterval);
}

// ── MIDI Learn ───────────────────────────────────────────────────────────────

void MidiEngine::startMidiLearn(const QString &target) {
  m_midiLearnTargetStr = target;
  if (target == "record")
    m_midiLearnTargetType = MidiLearnTarget::Record;
  else if (target == "play")
    m_midiLearnTargetType = MidiLearnTarget::Play;
  else if (target == "stop")
    m_midiLearnTargetType = MidiLearnTarget::Stop;
  else if (target == "clear")
    m_midiLearnTargetType = MidiLearnTarget::Clear;
  else if (target == "tapTempo")
    m_midiLearnTargetType = MidiLearnTarget::TapTempo;
  else
    m_midiLearnTargetType = MidiLearnTarget::None;

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

void MidiEngine::handleMidiLearn(int ccOrNote, bool isCC) {
  switch (m_midiLearnTargetType) {
  case MidiLearnTarget::Record:
    m_recordButton = ccOrNote;
    m_recordButtonIsNote = !isCC;
    break;
  case MidiLearnTarget::Play:
    m_playButton = ccOrNote;
    m_playButtonIsNote = !isCC;
    break;
  case MidiLearnTarget::Stop:
    m_stopButton = ccOrNote;
    m_stopButtonIsNote = !isCC;
    break;
  case MidiLearnTarget::Clear:
    m_clearButton = ccOrNote;
    m_clearButtonIsNote = !isCC;
    break;
  case MidiLearnTarget::TapTempo:
    m_tapTempoButton = ccOrNote;
    m_tapTempoButtonIsNote = !isCC;
    break;
  default:
    break;
  }
  m_midiLearnActive = false;
  m_midiLearnTargetStr.clear();
  m_midiLearnTargetType = MidiLearnTarget::None;
  emit midiBindingsChanged();
  emit midiLearnActiveChanged();
  emit midiLearnTargetChanged();
  saveSettings();
}

bool MidiEngine::triggerBoundAction(int value, bool isNote) {
  if (value == m_recordButton && m_recordButton >= 0 &&
      isNote == m_recordButtonIsNote) {
    QMetaObject::invokeMethod(
        this,
        [this]() {
          if (!m_recording)
            startRecording();
          else
            stopRecording();
        },
        Qt::QueuedConnection);
    return true;
  }
  if (value == m_playButton && m_playButton >= 0 &&
      isNote == m_playButtonIsNote) {
    QMetaObject::invokeMethod(
        this,
        [this]() {
          if (!m_playing)
            startPlayback();
          else
            stopPlayback();
        },
        Qt::QueuedConnection);
    return true;
  }
  if (value == m_stopButton && m_stopButton >= 0 &&
      isNote == m_stopButtonIsNote) {
    QMetaObject::invokeMethod(
        this,
        [this]() {
          stopPlayback();
          stopRecording();
        },
        Qt::QueuedConnection);
    return true;
  }
  if (value == m_clearButton && m_clearButton >= 0 &&
      isNote == m_clearButtonIsNote) {
    QMetaObject::invokeMethod(this, &MidiEngine::clearSequence,
                              Qt::QueuedConnection);
    return true;
  }
  if (value == m_tapTempoButton && m_tapTempoButton >= 0 &&
      isNote == m_tapTempoButtonIsNote) {
    QMetaObject::invokeMethod(this, &MidiEngine::tapTempo,
                              Qt::QueuedConnection);
    return true;
  }
  return false;
}

// ── Sequence QVariantList (read by QML) ──────────────────────────────────────

QVariantList MidiEngine::sequence() const {
  QVariantList result;
  for (const auto &step : m_tracks[m_activeTrack]) {
    QVariantMap map;
    bool active = !step.isEmpty();
    map["active"] = active;
    map["noteCount"] = step.size();
    if (active) {
      // Expose the lowest note as the "primary" note for the cell label
      const NoteEvent *primary = &step[0];
      for (const auto &ev : step)
        if (ev.note < primary->note)
          primary = &ev;
      map["note"] = primary->note;
      map["velocity"] = primary->velocity;
      map["channel"] = primary->channel;
    } else {
      map["note"] = 0;
      map["velocity"] = 0;
      map["channel"] = 0;
    }
    result << map;
  }
  return result;
}

// ── MIDI input processing ────────────────────────────────────────────────────

void MidiEngine::processIncomingMidi(const std::vector<unsigned char> &msg) {
  if (msg.empty())
    return;

  unsigned char status = msg[0];
  unsigned char data1 = msg.size() > 1 ? msg[1] : 0;
  unsigned char data2 = msg.size() > 2 ? msg[2] : 0;
  int channel = (status & 0x0F);
  unsigned char msgType = (status & 0xF0);

  bool isNoteOn = (msgType == 0x90) && (data2 > 0);
  bool isNoteOff = (msgType == 0x80) || ((msgType == 0x90) && (data2 == 0));
  bool isCC = (msgType == 0xB0);
  Q_UNUSED(isNoteOff)

  // MIDI Learn
  if (m_midiLearnActive) {
    if (isCC)
      handleMidiLearn(static_cast<int>(data1), true);
    else if (isNoteOn)
      handleMidiLearn(static_cast<int>(data1), false);
    return;
  }

  // Control bindings (CC buttons and note/pad buttons)
  if (isCC) {
    int cc = static_cast<int>(data1);
    if (data2 > 0 && triggerBoundAction(cc, false))
      return;
  } else if (isNoteOn) {
    int note = static_cast<int>(data1);
    if (triggerBoundAction(note, true))
      return;
  }

  // Passthrough to virtual output
  if (m_passthroughEnabled && m_midiOut && m_virtualPortOpen) {
    try {
      std::vector<unsigned char> fwd(msg.begin(), msg.end());
      m_midiOut->sendMessage(&fwd);
    } catch (...) {
    }
  }

  // Recording: group Note-On events within kChordWindowMs into the same step
  if (m_recording && isNoteOn) {
    QMutexLocker locker(&m_mutex);
    if (m_recordStep >= 0 && m_recordStep < m_maxSteps) {
      m_tracks[m_activeTrack][m_recordStep].append(
          NoteEvent{static_cast<int>(data1), static_cast<int>(data2), channel});
      // All signal emissions and timer ops must happen on the main thread
      QMetaObject::invokeMethod(
          this,
          [this]() {
            emit sequenceChanged();
            m_chordTimer->start(kChordWindowMs);
          },
          Qt::QueuedConnection);
    }
  }

  if (isNoteOn)
    QMetaObject::invokeMethod(
        this,
        [this, data1, data2, channel]() {
          emit noteReceived(data1, data2, channel);
        },
        Qt::QueuedConnection);
}

// Called when chord window closes: advance to the next step (always on main
// thread)
void MidiEngine::commitChordStep() {
  if (!m_recording)
    return;

  // Single-step re-record mode: just stop recording after capturing one chord
  if (m_stepRecordTarget >= 0) {
    m_stepRecordTarget = -1;
    m_recording = false;
    m_recordStep = -1;
    emit stepRecordTargetChanged();
    emit recordingChanged();
    emit recordingStepChanged();
    // Don't change m_currentStep — playback owns it now
    return;
  }

  if (!m_recordAllBeats) {
    // In current-beat mode, recording follows playback and stays sparse.
    return;
  }

  m_recordStep++;
  emit recordingStepChanged();
  if (!m_playing) {
    m_currentStep = m_recordStep;
    emit currentStepChanged();
  }
  if (m_recordStep >= m_maxSteps) {
    m_recording = false;
    m_recordStep = -1;
    emit recordingChanged();
    emit recordingStepChanged();
    if (!m_playing)
      QMetaObject::invokeMethod(this, "startPlayback", Qt::QueuedConnection);
  }
}

// ── Playback step advance ────────────────────────────────────────────────────

void MidiEngine::advanceStep() {
  if (!m_playing)
    return;

  const int playbackStep = m_currentStep;

  // Stop all notes from the previous step
  for (const auto &ev : m_activePlaybackNotes)
    sendNoteOff(ev.note, ev.channel);
  m_activePlaybackNotes.clear();

  for (int trackIndex = 0; trackIndex < m_tracks.size(); ++trackIndex) {
    const auto &track = m_tracks[trackIndex];
    const ChordStep &step = track[playbackStep];
    const int outputChannel = trackIndex < m_trackMidiChannels.size()
                                  ? m_trackMidiChannels[trackIndex]
                                  : qMin(trackIndex, 15);
    for (const auto &ev : step) {
      sendNoteOn(ev.note, ev.velocity, outputChannel);
      m_activePlaybackNotes.append(
          NoteEvent{ev.note, ev.velocity, outputChannel});
    }
  }

  m_currentStep = (playbackStep + 1) % m_maxSteps;
  emit currentStepChanged();
  if (m_recording && !m_recordAllBeats && m_recordStep != m_currentStep) {
    m_recordStep = m_currentStep;
    emit recordingStepChanged();
  }
}

// ── Note send helpers ────────────────────────────────────────────────────────

void MidiEngine::sendNoteOn(int note, int velocity, int channel) {
  if (!m_midiOut || !m_virtualPortOpen)
    return;
  try {
    std::vector<unsigned char> msg = {
        static_cast<unsigned char>(0x90 | (channel & 0x0F)),
        static_cast<unsigned char>(note & 0x7F),
        static_cast<unsigned char>(velocity & 0x7F)};
    m_midiOut->sendMessage(&msg);
  } catch (...) {
  }
}

void MidiEngine::sendNoteOff(int note, int channel) {
  if (!m_midiOut || !m_virtualPortOpen)
    return;
  try {
    std::vector<unsigned char> msg = {
        static_cast<unsigned char>(0x80 | (channel & 0x0F)),
        static_cast<unsigned char>(note & 0x7F), 0x00};
    m_midiOut->sendMessage(&msg);
  } catch (...) {
  }
}

void MidiEngine::stopAllNotes() {
  for (const auto &ev : m_activePlaybackNotes)
    sendNoteOff(ev.note, ev.channel);
  m_activePlaybackNotes.clear();

  if (m_midiOut && m_virtualPortOpen) {
    try {
      for (int ch = 0; ch < 16; ++ch) {
        std::vector<unsigned char> msg = {static_cast<unsigned char>(0xB0 | ch),
                                          123, 0};
        m_midiOut->sendMessage(&msg);
      }
    } catch (...) {
    }
  }
}

// ── Persistence ──────────────────────────────────────────────────────────────

void MidiEngine::loadSettings() {
  QSettings s;
  m_bpm = s.value("bpm", 120.0).toDouble();
  m_savedInputName = s.value("inputPortName").toString();
  m_savedOutputName = s.value("outputPortName").toString();
  m_recordButton = s.value("recordButton", -1).toInt();
  m_playButton = s.value("playButton", -1).toInt();
  m_stopButton = s.value("stopButton", -1).toInt();
  m_clearButton = s.value("clearButton", -1).toInt();
  m_tapTempoButton = s.value("tapTempoButton", -1).toInt();
  m_recordAllBeats = s.value("recordAllBeats", true).toBool();
  const QVariantList trackMidiChannels = s.value("trackMidiChannels").toList();
  for (int i = 0;
       i < qMin(m_trackCount, static_cast<int>(trackMidiChannels.size()));
       ++i) {
    const int channel = trackMidiChannels[i].toInt();
    m_trackMidiChannels[i] = qBound(1, channel > 0 ? channel : i + 1, 16) - 1;
  }
  m_recordButtonIsNote = s.value("recordButtonIsNote", false).toBool();
  m_playButtonIsNote = s.value("playButtonIsNote", false).toBool();
  m_stopButtonIsNote = s.value("stopButtonIsNote", false).toBool();
  m_clearButtonIsNote = s.value("clearButtonIsNote", false).toBool();
  m_tapTempoButtonIsNote = s.value("tapTempoButtonIsNote", false).toBool();
  if (m_bpm < 40.0)
    m_bpm = 40.0;
  if (m_bpm > 240.0)
    m_bpm = 240.0;
}

void MidiEngine::saveSettings() const {
  QSettings s;
  s.setValue("bpm", m_bpm);
  if (m_selectedInputPort >= 0 && m_selectedInputPort < m_inputPorts.size())
    s.setValue("inputPortName", m_inputPorts[m_selectedInputPort]);
  if (m_selectedOutputPort >= 0 && m_selectedOutputPort < m_outputPorts.size())
    s.setValue("outputPortName", m_outputPorts[m_selectedOutputPort]);
  s.setValue("recordButton", m_recordButton);
  s.setValue("playButton", m_playButton);
  s.setValue("stopButton", m_stopButton);
  s.setValue("clearButton", m_clearButton);
  s.setValue("tapTempoButton", m_tapTempoButton);
  s.setValue("recordAllBeats", m_recordAllBeats);
  QVariantList trackMidiChannels;
  for (int channel : m_trackMidiChannels)
    trackMidiChannels << channel + 1;
  s.setValue("trackMidiChannels", trackMidiChannels);
  s.setValue("recordButtonIsNote", m_recordButtonIsNote);
  s.setValue("playButtonIsNote", m_playButtonIsNote);
  s.setValue("stopButtonIsNote", m_stopButtonIsNote);
  s.setValue("clearButtonIsNote", m_clearButtonIsNote);
  s.setValue("tapTempoButtonIsNote", m_tapTempoButtonIsNote);
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
