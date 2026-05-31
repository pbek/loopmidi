#include "MidiEngine.h"
#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QUrl>
#include <utility>

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
  m_instrumentRack.resize(m_trackCount);
  for (int i = 0; i < m_trackCount; ++i)
    m_instrumentRack[i] = defaultInstrumentSlot(i);

  setupVirtualOutput();
  setupTrackVirtualOutputs();
  loadSettings();
  scanPlugins();
  QTimer::singleShot(250, this, &MidiEngine::scanSurgePatches);
  refreshPorts();
  tryRestoreSavedPorts();
}

MidiEngine::~MidiEngine() {
  stopPluginHost();
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
  for (auto &midiOut : m_trackMidiOuts) {
    if (midiOut)
      try {
        midiOut->closePort();
      } catch (...) {
      }
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

void MidiEngine::setupTrackVirtualOutputs() {
  m_trackMidiOuts.clear();
  m_trackVirtualPortsOpen.clear();
  m_trackMidiOuts.reserve(static_cast<size_t>(m_trackCount));
  m_trackVirtualPortsOpen.resize(m_trackCount);

  for (int i = 0; i < m_trackCount; ++i) {
    try {
      auto midiOut = std::make_unique<RtMidiOut>();
      const QString portName = QStringLiteral("LoopMidi Track %1").arg(i + 1);
      midiOut->openVirtualPort(portName.toStdString());
      m_trackMidiOuts.push_back(std::move(midiOut));
      m_trackVirtualPortsOpen[i] = true;
      qDebug() << "Virtual MIDI port" << portName << "opened";
    } catch (RtMidiError &e) {
      m_trackMidiOuts.push_back(nullptr);
      m_trackVirtualPortsOpen[i] = false;
      emit errorOccurred(QStringLiteral("Track virtual port: ") +
                         QString::fromStdString(e.getMessage()));
    }
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
  emit activeInstrumentChanged();
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

InstrumentSlot MidiEngine::defaultInstrumentSlot(int trackIndex) const {
  InstrumentSlot slot;
  slot.enabled = true;
  slot.name = QStringLiteral("Surge-XT Track %1").arg(trackIndex + 1);
  slot.pluginFormat = QStringLiteral("LV2");
  slot.pluginId = QStringLiteral("Surge XT");
  slot.pluginPath.clear();
  slot.patchPath.clear();
  slot.presetName = QStringLiteral("Init");
  slot.program = 0;
  return slot;
}

InstrumentSlot MidiEngine::activeInstrumentSlot() const {
  if (m_activeTrack < 0 || m_activeTrack >= m_instrumentRack.size())
    return defaultInstrumentSlot(qMax(0, m_activeTrack));
  return m_instrumentRack[m_activeTrack];
}

QVariantList MidiEngine::instrumentRack() const {
  QVariantList result;
  for (int i = 0; i < m_instrumentRack.size(); ++i) {
    const InstrumentSlot &slot = m_instrumentRack[i];
    QVariantMap map;
    map["track"] = i;
    map["enabled"] = slot.enabled;
    map["name"] = slot.name;
    map["pluginFormat"] = slot.pluginFormat;
    map["pluginId"] = slot.pluginId;
    map["pluginPath"] = slot.pluginPath;
    map["patchPath"] = slot.patchPath;
    map["presetName"] = slot.presetName;
    map["program"] = slot.program;
    result << map;
  }
  return result;
}

QVariantList MidiEngine::availablePlugins() const {
  QVariantList result;
  for (const AvailablePlugin &plugin : m_availablePlugins) {
    QVariantMap map;
    map["name"] = plugin.name;
    map["pluginFormat"] = plugin.pluginFormat;
    map["pluginId"] = plugin.pluginId;
    map["path"] = plugin.path;
    map["label"] =
        QStringLiteral("%1 (%2)").arg(plugin.name, plugin.pluginFormat);
    result << map;
  }
  return result;
}

QVariantList MidiEngine::availableSurgePatches() const {
  QVariantList result;
  for (const SurgePatch &patch : m_availableSurgePatches) {
    QVariantMap map;
    map["name"] = patch.name;
    map["category"] = patch.category;
    map["path"] = patch.path;
    map["label"] =
        patch.category.isEmpty()
            ? patch.name
            : QStringLiteral("%1 / %2").arg(patch.category, patch.name);
    result << map;
  }
  return result;
}

int MidiEngine::activeSurgePatchIndex() const {
  const QString activePath = activeInstrumentSlot().patchPath;
  if (activePath.isEmpty())
    return -1;

  for (int i = 0; i < m_availableSurgePatches.size(); ++i) {
    if (m_availableSurgePatches[i].path == activePath)
      return i;
  }
  return -1;
}

QStringList MidiEngine::pluginSearchPaths(const QString &format) const {
  QString envName;
  QStringList paths;
  const QString upperFormat = format.toUpper();

  if (upperFormat == QStringLiteral("LV2")) {
    envName = QStringLiteral("LV2_PATH");
    paths << QStringLiteral("%1/.lv2").arg(QDir::homePath())
          << QStringLiteral("/usr/local/lib/lv2")
          << QStringLiteral("/usr/lib/lv2")
          << QStringLiteral("/run/current-system/sw/lib/lv2");
  } else if (upperFormat == QStringLiteral("CLAP")) {
    envName = QStringLiteral("CLAP_PATH");
    paths << QStringLiteral("%1/.clap").arg(QDir::homePath())
          << QStringLiteral("/usr/local/lib/clap")
          << QStringLiteral("/usr/lib/clap")
          << QStringLiteral("/run/current-system/sw/lib/clap");
  } else if (upperFormat == QStringLiteral("VST3")) {
    envName = QStringLiteral("VST3_PATH");
    paths << QStringLiteral("%1/.vst3").arg(QDir::homePath())
          << QStringLiteral("/usr/local/lib/vst3")
          << QStringLiteral("/usr/lib/vst3")
          << QStringLiteral("/run/current-system/sw/lib/vst3");
  }

  if (!envName.isEmpty()) {
    const QStringList envPaths =
        QString::fromLocal8Bit(qgetenv(envName.toUtf8()))
            .split(QLatin1Char(':'), Qt::SkipEmptyParts);
    paths = envPaths + paths;
  }

  paths.removeDuplicates();
  return paths;
}

QStringList MidiEngine::surgeDataPaths() const {
  QStringList paths = QString::fromLocal8Bit(qgetenv("SURGE_XT_DATA_PATH"))
                          .split(QLatin1Char(':'), Qt::SkipEmptyParts);

  for (const AvailablePlugin &plugin : m_availablePlugins) {
    if (!plugin.name.contains(QStringLiteral("Surge XT"), Qt::CaseInsensitive))
      continue;
    const int libIndex = plugin.path.indexOf(QStringLiteral("/lib/lv2/"));
    if (libIndex > 0)
      paths << plugin.path.left(libIndex) + QStringLiteral("/share/surge-xt");
  }

  paths << QStringLiteral("/run/current-system/sw/share/surge-xt")
        << QStringLiteral("/usr/share/surge-xt")
        << QStringLiteral("/usr/local/share/surge-xt");
  paths.removeDuplicates();
  return paths;
}

void MidiEngine::addAvailablePlugin(const QString &name, const QString &format,
                                    const QString &pluginId,
                                    const QString &path) {
  const QString key =
      format + QLatin1Char('|') + pluginId + QLatin1Char('|') + path;
  for (const AvailablePlugin &plugin : m_availablePlugins) {
    const QString existingKey = plugin.pluginFormat + QLatin1Char('|') +
                                plugin.pluginId + QLatin1Char('|') +
                                plugin.path;
    if (existingKey == key)
      return;
  }

  AvailablePlugin plugin;
  plugin.name = name;
  plugin.pluginFormat = format;
  plugin.pluginId = pluginId;
  plugin.path = path;
  m_availablePlugins.append(plugin);
}

void MidiEngine::addAvailableSurgePatch(const QString &name,
                                        const QString &category,
                                        const QString &path) {
  for (const SurgePatch &patch : m_availableSurgePatches) {
    if (patch.path == path)
      return;
  }

  SurgePatch patch;
  patch.name = name;
  patch.category = category;
  patch.path = path;
  m_availableSurgePatches.append(patch);
}

void MidiEngine::scanPlugins() {
  m_availablePlugins.clear();

  for (const QString &path : pluginSearchPaths(QStringLiteral("LV2"))) {
    QDir dir(path);
    if (!dir.exists())
      continue;
    const QFileInfoList bundles =
        dir.entryInfoList(QStringList() << QStringLiteral("*.lv2"),
                          QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo &bundle : bundles) {
      QString name = bundle.completeBaseName();
      QString pluginId = name;
      bool isInstrumentPlugin = false;
      QFile manifest(bundle.absoluteFilePath() +
                     QStringLiteral("/manifest.ttl"));
      if (manifest.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString ttl = QString::fromUtf8(manifest.readAll());
        const QRegularExpression seeAlsoRe(
            QStringLiteral("rdfs:seeAlso\\s+<([^>]+)>"));
        const QRegularExpressionMatch seeAlsoMatch = seeAlsoRe.match(ttl);
        if (seeAlsoMatch.hasMatch()) {
          QFile seeAlso(bundle.absoluteFilePath() + QLatin1Char('/') +
                        seeAlsoMatch.captured(1));
          if (seeAlso.open(QIODevice::ReadOnly | QIODevice::Text))
            ttl += QLatin1Char('\n') + QString::fromUtf8(seeAlso.readAll());
        }
        const QRegularExpression nameRe(
            QStringLiteral("doap:name\\s+\\\"([^\\\"]+)\\\""));
        const QRegularExpression uriRe(
            QStringLiteral("<([^>]+)>\\s+a\\s+[^.]*lv2:Plugin"),
            QRegularExpression::DotMatchesEverythingOption);
        const QRegularExpressionMatch nameMatch = nameRe.match(ttl);
        const QRegularExpressionMatch uriMatch = uriRe.match(ttl);
        if (nameMatch.hasMatch())
          name = nameMatch.captured(1);
        if (uriMatch.hasMatch())
          pluginId = uriMatch.captured(1);
        isInstrumentPlugin =
            ttl.contains(QStringLiteral("lv2:InstrumentPlugin"));
      }
      if (!isInstrumentPlugin)
        continue;
      if (name.compare(QStringLiteral("Surge XT"), Qt::CaseInsensitive) == 0 ||
          name.compare(QStringLiteral("SurgeXT"), Qt::CaseInsensitive) == 0)
        name = QStringLiteral("Surge XT");
      addAvailablePlugin(name, QStringLiteral("LV2"), pluginId,
                         bundle.absoluteFilePath());
    }
  }

  for (const QString &path : pluginSearchPaths(QStringLiteral("CLAP"))) {
    QDirIterator it(path, QStringList() << QStringLiteral("*.clap"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QFileInfo file(it.next());
      addAvailablePlugin(file.completeBaseName(), QStringLiteral("CLAP"),
                         file.completeBaseName(), file.absoluteFilePath());
    }
  }

  for (const QString &path : pluginSearchPaths(QStringLiteral("VST3"))) {
    QDirIterator it(path, QStringList() << QStringLiteral("*.vst3"),
                    QDir::Dirs | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
      const QFileInfo bundle(it.next());
      addAvailablePlugin(bundle.completeBaseName(), QStringLiteral("VST3"),
                         bundle.completeBaseName(), bundle.absoluteFilePath());
    }
  }

  emit availablePluginsChanged();
}

void MidiEngine::scanSurgePatches() {
  m_availableSurgePatches.clear();

  const QStringList categories = {
      QStringLiteral("Basses"),    QStringLiteral("Brass"),
      QStringLiteral("Keys"),      QStringLiteral("Leads"),
      QStringLiteral("Pads"),      QStringLiteral("Percussion"),
      QStringLiteral("Plucks"),    QStringLiteral("Polysynths"),
      QStringLiteral("Sequences"), QStringLiteral("Strings"),
      QStringLiteral("Winds"),     QStringLiteral("MPE"),
  };
  constexpr int kMaxPatchesPerCategory = 32;

  for (const QString &dataPath : surgeDataPaths()) {
    QDir root(dataPath);
    if (!root.exists())
      continue;

    const QString factoryRoot =
        root.absoluteFilePath(QStringLiteral("patches_factory"));
    for (const QString &category : categories) {
      QDir dir(factoryRoot + QLatin1Char('/') + category);
      if (!dir.exists())
        continue;

      const QFileInfoList files = dir.entryInfoList(
          QStringList() << QStringLiteral("*.fxp"), QDir::Files, QDir::Name);
      const int count = qMin(kMaxPatchesPerCategory, files.size());
      for (int i = 0; i < count; ++i) {
        const QFileInfo &file = files[i];
        addAvailableSurgePatch(file.completeBaseName(), category,
                               file.absoluteFilePath());
      }
    }

    if (!m_availableSurgePatches.isEmpty())
      break;
  }

  qDebug() << "Scanned" << m_availableSurgePatches.size() << "Surge XT patches";
  emit availableSurgePatchesChanged();
}

void MidiEngine::setActiveInstrumentFromAvailablePlugin(int index) {
  if (index < 0 || index >= m_availablePlugins.size() || m_activeTrack < 0 ||
      m_activeTrack >= m_instrumentRack.size())
    return;

  const AvailablePlugin &plugin = m_availablePlugins[index];
  InstrumentSlot &slot = m_instrumentRack[m_activeTrack];
  slot.enabled = true;
  slot.name =
      QStringLiteral("%1 Track %2").arg(plugin.name).arg(m_activeTrack + 1);
  slot.pluginFormat = plugin.pluginFormat;
  slot.pluginId = plugin.pluginId;
  slot.pluginPath = plugin.path;
  if (slot.presetName.trimmed().isEmpty())
    slot.presetName = QStringLiteral("Init");
  emitInstrumentChanges();
  saveSettings();
}

void MidiEngine::setActiveInstrumentFromSurgePatch(int index) {
  if (index < 0 || index >= m_availableSurgePatches.size() ||
      m_activeTrack < 0 || m_activeTrack >= m_instrumentRack.size())
    return;

  const SurgePatch &patch = m_availableSurgePatches[index];
  InstrumentSlot &slot = m_instrumentRack[m_activeTrack];
  slot.patchPath = patch.path;
  slot.presetName =
      patch.category.isEmpty()
          ? patch.name
          : QStringLiteral("%1 / %2").arg(patch.category, patch.name);
  emitInstrumentChanges();
  saveSettings();

  // Restart the plugin host so jalv picks up the new LV2 state for the changed
  // patch
  if (m_pluginHostRunning)
    startPluginHost();
}

bool MidiEngine::resolveInstrumentSlotPlugin(int trackIndex) {
  if (trackIndex < 0 || trackIndex >= m_instrumentRack.size())
    return false;

  InstrumentSlot &slot = m_instrumentRack[trackIndex];
  if (slot.pluginFormat.compare(QStringLiteral("LV2"), Qt::CaseInsensitive) !=
      0)
    return false;
  const bool isKnownEffect =
      slot.pluginId.contains(QStringLiteral("Surge_XT_Effects"),
                             Qt::CaseInsensitive) ||
      slot.pluginId.contains(QStringLiteral("surge-xt-effects"),
                             Qt::CaseInsensitive) ||
      slot.name.contains(QStringLiteral("Effects"), Qt::CaseInsensitive);
  if (slot.pluginId.startsWith(QStringLiteral("http")) && !isKnownEffect)
    return true;

  const QString wanted =
      isKnownEffect ? QStringLiteral("Surge XT") : slot.pluginId.trimmed();
  const QString slotName = slot.name.trimmed();

  for (const AvailablePlugin &plugin : std::as_const(m_availablePlugins)) {
    if (plugin.pluginFormat.compare(QStringLiteral("LV2"),
                                    Qt::CaseInsensitive) != 0)
      continue;
    const bool nameMatches =
        plugin.name.compare(wanted, Qt::CaseInsensitive) == 0 ||
        slotName.contains(plugin.name, Qt::CaseInsensitive);
    if (!nameMatches)
      continue;

    slot.name =
        QStringLiteral("%1 Track %2").arg(plugin.name).arg(trackIndex + 1);
    slot.pluginFormat = plugin.pluginFormat;
    slot.pluginId = plugin.pluginId;
    slot.pluginPath = plugin.path;
    emitInstrumentChanges();
    saveSettings();
    return true;
  }

  for (const AvailablePlugin &plugin : std::as_const(m_availablePlugins)) {
    if (plugin.pluginFormat.compare(QStringLiteral("LV2"),
                                    Qt::CaseInsensitive) != 0)
      continue;
    if (wanted.compare(QStringLiteral("Surge XT"), Qt::CaseInsensitive) == 0 &&
        plugin.name.contains(QStringLiteral("Effects"), Qt::CaseInsensitive))
      continue;
    if (!plugin.path.contains(wanted, Qt::CaseInsensitive))
      continue;

    slot.name =
        QStringLiteral("%1 Track %2").arg(plugin.name).arg(trackIndex + 1);
    slot.pluginFormat = plugin.pluginFormat;
    slot.pluginId = plugin.pluginId;
    slot.pluginPath = plugin.path;
    emitInstrumentChanges();
    saveSettings();
    return true;
  }

  return false;
}

QString MidiEngine::activeInstrumentName() const {
  return activeInstrumentSlot().name;
}

QString MidiEngine::activeInstrumentFormat() const {
  return activeInstrumentSlot().pluginFormat;
}

QString MidiEngine::activeInstrumentPluginId() const {
  return activeInstrumentSlot().pluginId;
}

QString MidiEngine::activeInstrumentPluginPath() const {
  return activeInstrumentSlot().pluginPath;
}

QString MidiEngine::activeInstrumentPatchPath() const {
  return activeInstrumentSlot().patchPath;
}

QString MidiEngine::activeInstrumentPresetName() const {
  return activeInstrumentSlot().presetName;
}

int MidiEngine::activeInstrumentProgram() const {
  return activeInstrumentSlot().program;
}

bool MidiEngine::activeInstrumentEnabled() const {
  return activeInstrumentSlot().enabled;
}

void MidiEngine::emitInstrumentChanges() {
  emit activeInstrumentChanged();
  emit instrumentRackChanged();
}

void MidiEngine::setActiveInstrumentName(const QString &name) {
  if (m_activeTrack < 0 || m_activeTrack >= m_instrumentRack.size())
    return;
  const QString nextName = name.trimmed().isEmpty()
                               ? defaultInstrumentSlot(m_activeTrack).name
                               : name.trimmed();
  if (m_instrumentRack[m_activeTrack].name == nextName)
    return;
  m_instrumentRack[m_activeTrack].name = nextName;
  emitInstrumentChanges();
  saveSettings();
}

void MidiEngine::setActiveInstrumentFormat(const QString &format) {
  if (m_activeTrack < 0 || m_activeTrack >= m_instrumentRack.size())
    return;
  const QString nextFormat = format.trimmed().isEmpty()
                                 ? QStringLiteral("LV2")
                                 : format.trimmed().toUpper();
  if (m_instrumentRack[m_activeTrack].pluginFormat == nextFormat)
    return;
  m_instrumentRack[m_activeTrack].pluginFormat = nextFormat;
  emitInstrumentChanges();
  saveSettings();
}

void MidiEngine::setActiveInstrumentPluginId(const QString &pluginId) {
  if (m_activeTrack < 0 || m_activeTrack >= m_instrumentRack.size())
    return;
  const QString nextPluginId =
      pluginId.trimmed().isEmpty()
          ? defaultInstrumentSlot(m_activeTrack).pluginId
          : pluginId.trimmed();
  if (m_instrumentRack[m_activeTrack].pluginId == nextPluginId)
    return;
  m_instrumentRack[m_activeTrack].pluginId = nextPluginId;
  m_instrumentRack[m_activeTrack].pluginPath.clear();
  emitInstrumentChanges();
  saveSettings();
}

QString MidiEngine::pluginHostExecutable(const QString &format) const {
  if (format.compare(QStringLiteral("LV2"), Qt::CaseInsensitive) != 0)
    return QString();

  QString executable = QStandardPaths::findExecutable(QStringLiteral("jalv"));
  if (executable.isEmpty())
    executable = QStandardPaths::findExecutable(QStringLiteral("jalv.qt5"));
  if (executable.isEmpty())
    executable = QStandardPaths::findExecutable(QStringLiteral("jalv.gtk"));
  return executable;
}

QString MidiEngine::pluginHostClientName(int trackIndex) const {
  return QStringLiteral("loopmidi-track-%1").arg(trackIndex + 1);
}

QString MidiEngine::pwJackExecutable() const {
  return QStandardPaths::findExecutable(QStringLiteral("pw-jack"));
}

QByteArray MidiEngine::readSurgePatchChunk(const QString &path) const {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly))
    return QByteArray();

  const QByteArray header = file.read(60);
  if (header.size() != 60)
    return QByteArray();

  auto readBe32 = [](const QByteArray &data, int offset) {
    const auto *bytes =
        reinterpret_cast<const unsigned char *>(data.constData() + offset);
    return (static_cast<quint32>(bytes[0]) << 24) |
           (static_cast<quint32>(bytes[1]) << 16) |
           (static_cast<quint32>(bytes[2]) << 8) |
           static_cast<quint32>(bytes[3]);
  };

  if (readBe32(header, 0) != 0x43636e4b || readBe32(header, 8) != 0x46504368 ||
      readBe32(header, 16) != 0x636a7333)
    return QByteArray();

  const quint32 chunkSize = readBe32(header, 56);
  if (chunkSize == 0 || chunkSize > 16 * 1024 * 1024)
    return QByteArray();

  const QByteArray chunk = file.read(static_cast<qint64>(chunkSize));
  return chunk.size() == static_cast<int>(chunkSize) ? chunk : QByteArray();
}

QString MidiEngine::writeSurgeProgramState(int trackIndex) const {
  if (trackIndex < 0 || trackIndex >= m_instrumentRack.size())
    return QString();

  const InstrumentSlot &slot = m_instrumentRack[trackIndex];
  if (!slot.pluginId.contains(QStringLiteral("surge"), Qt::CaseInsensitive))
    return QString();

  const QByteArray patchChunk = readSurgePatchChunk(slot.patchPath);
  if (!patchChunk.isEmpty()) {
    qWarning().noquote() << "[plugin host] Track" << (trackIndex + 1)
                         << "loading Surge patch" << slot.presetName << "from"
                         << slot.patchPath;
  } else {
    qWarning().noquote() << "[plugin host] Track" << (trackIndex + 1)
                         << "has no readable Surge patch; using Program"
                         << slot.program << "patchPath=" << slot.patchPath;
  }

  QString root =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
  if (root.isEmpty())
    root = QDir::tempPath() + QStringLiteral("/loopmidi");

  const QString stateDir =
      root +
      QStringLiteral("/surge-program-state/track-%1").arg(trackIndex + 1);
  QDir dir;
  if (!dir.mkpath(stateDir))
    return QString();

  const QString manifestPath = stateDir + QStringLiteral("/manifest.ttl");
  const QString statePath = stateDir + QStringLiteral("/state.ttl");

  // Write a minimal manifest; jalv -l loads state.ttl directly so this is
  // only needed if the bundle directory is ever scanned by lilv_world.
  QFile manifest(manifestPath);
  if (!manifest.open(QIODevice::WriteOnly | QIODevice::Truncate |
                     QIODevice::Text))
    return QString();
  manifest.write(
      QStringLiteral(
          "@prefix lv2:  <http://lv2plug.in/ns/lv2core#> .\n"
          "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n"
          "@prefix rdfs: <http://www.w3.org/2000/01/rdf-schema#> .\n\n"
          "<urn:loopmidi:surge-state-%2>\n"
          "    a pset:Preset ;\n"
          "    lv2:appliesTo <%1> ;\n"
          "    rdfs:seeAlso <state.ttl> .\n")
          .arg(slot.pluginId)
          .arg(trackIndex + 1)
          .toUtf8());

  QFile state(statePath);
  if (!state.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
    return QString();
  // Declare the subject as both pset:Preset and state:State so that:
  // - lilv_state_new_from_file (called by jalv with NULL subject) can find the
  //   subject via the state:State type marker it searches for, AND
  // - the node is also a valid pset:Preset so lilv doesn't crash on this
  // version
  //   of jalv/lilv where a bare state:State subject causes a segfault.
  if (!patchChunk.isEmpty()) {
    state.write(
        QStringLiteral("@prefix atom: <http://lv2plug.in/ns/ext/atom#> .\n"
                       "@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
                       "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n"
                       "@prefix state: <http://lv2plug.in/ns/ext/state#> .\n\n"
                       "<urn:loopmidi:surge-state-%3>\n"
                       "    a pset:Preset, state:State ;\n"
                       "    lv2:appliesTo <%1> ;\n"
                       "    state:state [\n"
                       "        <%1:StateString> \"%2\"^^atom:String\n"
                       "    ] .\n")
            .arg(slot.pluginId, QString::fromLatin1(patchChunk.toBase64()))
            .arg(trackIndex + 1)
            .toUtf8());
  } else {
    state.write(
        QStringLiteral("@prefix lv2: <http://lv2plug.in/ns/lv2core#> .\n"
                       "@prefix pset: <http://lv2plug.in/ns/ext/presets#> .\n"
                       "@prefix state: <http://lv2plug.in/ns/ext/state#> .\n"
                       "@prefix xsd: <http://www.w3.org/2001/XMLSchema#> .\n\n"
                       "<urn:loopmidi:surge-state-%3>\n"
                       "    a pset:Preset, state:State ;\n"
                       "    lv2:appliesTo <%1> ;\n"
                       "    state:state [\n"
                       "        <%1:Program> \"%2\"^^xsd:int\n"
                       "    ] .\n")
            .arg(slot.pluginId)
            .arg(slot.program)
            .arg(trackIndex + 1)
            .toUtf8());
  }

  return stateDir;
}

bool MidiEngine::jackServerAvailable(QString *errorMessage) const {
  const QString jackLsp =
      QStandardPaths::findExecutable(QStringLiteral("jack_lsp"));
  if (jackLsp.isEmpty()) {
    if (errorMessage)
      *errorMessage = QStringLiteral("jack_lsp is not available.");
    return false;
  }

  QProcess lsp;
  const QString pwJack = pwJackExecutable();
  if (pwJack.isEmpty()) {
    lsp.setProgram(jackLsp);
  } else {
    lsp.setProgram(pwJack);
    lsp.setArguments(QStringList() << jackLsp);
  }
  lsp.setProcessChannelMode(QProcess::MergedChannels);
  lsp.start();
  if (!lsp.waitForFinished(1500)) {
    lsp.kill();
    if (errorMessage)
      *errorMessage = QStringLiteral("Timed out while checking JACK ports.");
    return false;
  }

  if (lsp.exitStatus() != QProcess::NormalExit || lsp.exitCode() != 0) {
    const QString output = QString::fromLocal8Bit(lsp.readAll()).trimmed();
    if (errorMessage)
      *errorMessage = output.isEmpty()
                          ? QStringLiteral("JACK server is not running.")
                          : output;
    return false;
  }

  return true;
}

void MidiEngine::appendPluginHostOutput(QProcess *process, int trackIndex,
                                        const QString &output) {
  const QString trimmed = output.trimmed();
  if (trimmed.isEmpty())
    return;

  const QString prefix =
      QStringLiteral("[plugin host track %1]").arg(trackIndex + 1);
  const QStringList lines =
      trimmed.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  for (const QString &line : lines)
    qWarning().noquote() << prefix << line.trimmed();

  const QString previous = process->property("loopmidiOutput").toString();
  QString combined = previous +
                     (previous.isEmpty() ? QString() : QStringLiteral("\n")) +
                     trimmed;
  const int maxLength = 1600;
  if (combined.size() > maxLength)
    combined = combined.right(maxLength);
  process->setProperty("loopmidiOutput", combined);
}

void MidiEngine::connectPluginHostAudio(const QString &clientName) {
  if (!m_pluginHostAutoConnectAudio)
    return;

  const QString jackLsp =
      QStandardPaths::findExecutable(QStringLiteral("jack_lsp"));
  const QString jackConnect =
      QStandardPaths::findExecutable(QStringLiteral("jack_connect"));
  if (jackLsp.isEmpty() || jackConnect.isEmpty())
    return;

  QProcess lsp;
  const QString pwJack = pwJackExecutable();
  if (pwJack.isEmpty())
    lsp.start(jackLsp);
  else
    lsp.start(pwJack, QStringList() << jackLsp);
  if (!lsp.waitForFinished(1000))
    return;

  const QStringList ports = QString::fromLocal8Bit(lsp.readAllStandardOutput())
                                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QStringList pluginOutputs;
  for (const QString &port : ports) {
    if (port == clientName + QStringLiteral(":audio_out_1") ||
        port == clientName + QStringLiteral(":audio_out_2")) {
      pluginOutputs << port;
    }
  }

  if (pluginOutputs.isEmpty())
    return;

  const QStringList playbackPorts = {QStringLiteral("system:playback_1"),
                                     QStringLiteral("system:playback_2")};
  for (int i = 0; i < pluginOutputs.size(); ++i) {
    const QString destination =
        playbackPorts[qMin(i, playbackPorts.size() - 1)];
    if (pwJack.isEmpty()) {
      QProcess::execute(jackConnect, QStringList()
                                         << pluginOutputs[i] << destination);
    } else {
      QProcess::execute(pwJack, QStringList() << jackConnect << pluginOutputs[i]
                                              << destination);
    }
  }
}

void MidiEngine::connectPluginHostMidi(const QString &clientName,
                                       int trackIndex) {
  const QString jackLsp =
      QStandardPaths::findExecutable(QStringLiteral("jack_lsp"));
  const QString jackConnect =
      QStandardPaths::findExecutable(QStringLiteral("jack_connect"));
  if (jackLsp.isEmpty() || jackConnect.isEmpty())
    return;

  QProcess lsp;
  const QString pwJack = pwJackExecutable();
  if (pwJack.isEmpty())
    lsp.start(jackLsp);
  else
    lsp.start(pwJack, QStringList() << jackLsp);
  if (!lsp.waitForFinished(1000))
    return;

  const QStringList ports = QString::fromLocal8Bit(lsp.readAllStandardOutput())
                                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QString loopMidiOutput;
  const QString pluginInput = clientName + QStringLiteral(":in");
  const QString trackPortName =
      QStringLiteral("LoopMidi Track %1").arg(trackIndex + 1);

  disconnectPluginHostMidi(clientName);

  for (const QString &port : ports) {
    if (port.contains(trackPortName, Qt::CaseInsensitive) &&
        port.contains(QStringLiteral("capture"), Qt::CaseInsensitive)) {
      loopMidiOutput = port;
      break;
    }
  }

  if (loopMidiOutput.isEmpty()) {
    const QString message =
        QStringLiteral("Could not find MIDI output %1 for %2.")
            .arg(trackPortName, clientName);
    qWarning().noquote() << "[plugin host]" << message;
    emit errorOccurred(message);
    return;
  }
  if (!ports.contains(pluginInput)) {
    const QString message =
        QStringLiteral("Could not find hosted MIDI input %1.").arg(pluginInput);
    qWarning().noquote() << "[plugin host]" << message;
    emit errorOccurred(message);
    return;
  }

  int result = 0;
  if (pwJack.isEmpty()) {
    result = QProcess::execute(jackConnect,
                               QStringList() << loopMidiOutput << pluginInput);
  } else {
    result = QProcess::execute(
        pwJack, QStringList() << jackConnect << loopMidiOutput << pluginInput);
  }

  if (result == 0) {
    qWarning().noquote() << "[plugin host] Connected MIDI" << loopMidiOutput
                         << "->" << pluginInput;
  } else {
    const QString message = QStringLiteral("Could not connect MIDI %1 -> %2.")
                                .arg(loopMidiOutput, pluginInput);
    qWarning().noquote() << "[plugin host]" << message;
    emit errorOccurred(message);
  }
}

void MidiEngine::disconnectPluginHostMidi(const QString &clientName) {
  const QString jackLsp =
      QStandardPaths::findExecutable(QStringLiteral("jack_lsp"));
  const QString jackDisconnect =
      QStandardPaths::findExecutable(QStringLiteral("jack_disconnect"));
  if (jackLsp.isEmpty() || jackDisconnect.isEmpty())
    return;

  const QString pwJack = pwJackExecutable();
  QProcess lsp;
  if (pwJack.isEmpty())
    lsp.start(jackLsp, QStringList() << QStringLiteral("-c"));
  else
    lsp.start(pwJack, QStringList() << jackLsp << QStringLiteral("-c"));
  if (!lsp.waitForFinished(1000))
    return;

  const QString pluginInput = clientName + QStringLiteral(":in");
  const QStringList lines = QString::fromLocal8Bit(lsp.readAllStandardOutput())
                                .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
  QString currentPort;
  for (const QString &line : lines) {
    const QString trimmed = line.trimmed();
    if (!line.startsWith(QLatin1Char(' ')) &&
        !line.startsWith(QLatin1Char('\t'))) {
      currentPort = trimmed;
      continue;
    }

    if (trimmed != pluginInput)
      continue;
    if (!currentPort.contains(QStringLiteral("LoopMidi"),
                              Qt::CaseInsensitive) &&
        !currentPort.contains(QStringLiteral("RtMidi Output Client"),
                              Qt::CaseInsensitive))
      continue;

    if (pwJack.isEmpty()) {
      QProcess::execute(jackDisconnect, QStringList()
                                            << currentPort << pluginInput);
    } else {
      QProcess::execute(pwJack, QStringList() << jackDisconnect << currentPort
                                              << pluginInput);
    }
  }
}

void MidiEngine::startPluginHost() {
  stopPluginHost();

  QString jackError;
  if (!jackServerAvailable(&jackError)) {
    m_pluginHostRunning = false;
    m_pluginHostStatus =
        QStringLiteral("Plugin host stopped: JACK is not running");
    emit pluginHostChanged();
    emit errorOccurred(
        QStringLiteral("Cannot start plugin host: %1").arg(jackError));
    qWarning().noquote() << "[plugin host] JACK preflight failed:" << jackError;
    return;
  }

  int started = 0;
  m_stoppingPluginHost = false;
  for (int i = 0; i < m_instrumentRack.size(); ++i) {
    if (!m_instrumentRack[i].enabled)
      continue;

    if (!resolveInstrumentSlotPlugin(i)) {
      emit errorOccurred(QStringLiteral("Select a scanned LV2 plugin for track "
                                        "%1 before starting the host.")
                             .arg(i + 1));
      continue;
    }

    const InstrumentSlot &slot = m_instrumentRack[i];
    const QString hostExecutable = pluginHostExecutable(slot.pluginFormat);
    if (hostExecutable.isEmpty()) {
      emit errorOccurred(
          QStringLiteral("No host executable found for %1 plugins.")
              .arg(slot.pluginFormat));
      continue;
    }
    auto *process = new QProcess(this);
    const QString pwJack = pwJackExecutable();
    process->setProgram(pwJack.isEmpty() ? hostExecutable : pwJack);
    const QString clientName = pluginHostClientName(i);
    QStringList hostArgs;
    if (!pwJack.isEmpty())
      hostArgs << hostExecutable;
    hostArgs << QStringLiteral("-n") << clientName;
    const QString stateDir = writeSurgeProgramState(i);
    if (!stateDir.isEmpty())
      hostArgs << QStringLiteral("-l") << stateDir;
    hostArgs << slot.pluginId;
    process->setArguments(hostArgs);
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyReadStandardOutput, this,
            [this, process, i]() {
              appendPluginHostOutput(
                  process, i,
                  QString::fromLocal8Bit(process->readAllStandardOutput()));
            });
    connect(
        process, &QProcess::errorOccurred, this,
        [this, i](QProcess::ProcessError) {
          emit errorOccurred(
              QStringLiteral("Plugin host failed for track %1.").arg(i + 1));
        });
    connect(process,
            QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, process, i](int exitCode, QProcess::ExitStatus exitStatus) {
              appendPluginHostOutput(
                  process, i,
                  QString::fromLocal8Bit(process->readAllStandardOutput()));
              const QString output =
                  process->property("loopmidiOutput").toString();
              m_pluginHostProcesses.removeAll(process);
              process->deleteLater();
              const bool running = !m_pluginHostProcesses.isEmpty();
              if (!m_stoppingPluginHost &&
                  (exitStatus != QProcess::NormalExit || exitCode != 0)) {
                const QString message =
                    output.isEmpty()
                        ? QStringLiteral(
                              "Plugin host track %1 exited with code %2.")
                              .arg(i + 1)
                              .arg(exitCode)
                        : QStringLiteral("Plugin host track %1 exited: %2")
                              .arg(i + 1)
                              .arg(output);
                emit errorOccurred(message);
                qWarning().noquote() << "[plugin host]" << message;
              }
              if (m_pluginHostRunning != running) {
                m_pluginHostRunning = running;
                m_pluginHostStatus =
                    running ? QStringLiteral("Plugin host running")
                            : QStringLiteral("Plugin host stopped");
                emit pluginHostChanged();
              }
            });
    process->start();
    if (process->waitForStarted(1500)) {
      m_pluginHostProcesses.append(process);
      ++started;
      QTimer::singleShot(1200, this, [this, clientName, i]() {
        connectPluginHostAudio(clientName);
        connectPluginHostMidi(clientName, i);
      });
    } else {
      process->deleteLater();
      emit errorOccurred(
          QStringLiteral("Could not start plugin host for track %1.")
              .arg(i + 1));
    }
  }

  m_pluginHostRunning = started > 0;
  m_pluginHostStatus =
      m_pluginHostRunning
          ? QStringLiteral("Plugin host running (%1 instance%2)")
                .arg(started)
                .arg(started == 1 ? QString() : QStringLiteral("s"))
          : QStringLiteral("Plugin host stopped");
  emit pluginHostChanged();
}

void MidiEngine::stopPluginHost() {
  m_stoppingPluginHost = true;
  const QVector<QProcess *> processes = m_pluginHostProcesses;
  m_pluginHostProcesses.clear();
  for (QProcess *process : processes) {
    if (!process)
      continue;
    process->terminate();
    if (!process->waitForFinished(1000))
      process->kill();
    process->deleteLater();
  }
  m_stoppingPluginHost = false;
  if (m_pluginHostRunning ||
      m_pluginHostStatus != QStringLiteral("Plugin host stopped")) {
    m_pluginHostRunning = false;
    m_pluginHostStatus = QStringLiteral("Plugin host stopped");
    emit pluginHostChanged();
  }
}

void MidiEngine::setActiveInstrumentPresetName(const QString &presetName) {
  if (m_activeTrack < 0 || m_activeTrack >= m_instrumentRack.size())
    return;
  const QString nextPresetName = presetName.trimmed().isEmpty()
                                     ? QStringLiteral("Init")
                                     : presetName.trimmed();
  if (m_instrumentRack[m_activeTrack].presetName == nextPresetName)
    return;
  m_instrumentRack[m_activeTrack].presetName = nextPresetName;
  emitInstrumentChanges();
  saveSettings();
}

void MidiEngine::setActiveInstrumentProgram(int program) {
  if (m_activeTrack < 0 || m_activeTrack >= m_instrumentRack.size())
    return;
  const int nextProgram = qBound(0, program, 2047);
  if (m_instrumentRack[m_activeTrack].program == nextProgram)
    return;
  m_instrumentRack[m_activeTrack].program = nextProgram;
  emitInstrumentChanges();
  saveSettings();
}

void MidiEngine::setActiveInstrumentEnabled(bool enabled) {
  if (m_activeTrack < 0 || m_activeTrack >= m_instrumentRack.size())
    return;
  if (m_instrumentRack[m_activeTrack].enabled == enabled)
    return;
  m_instrumentRack[m_activeTrack].enabled = enabled;
  emitInstrumentChanges();
  saveSettings();
}

void MidiEngine::setPluginHostAutoConnectAudio(bool enabled) {
  if (m_pluginHostAutoConnectAudio == enabled)
    return;
  m_pluginHostAutoConnectAudio = enabled;
  emit pluginHostChanged();
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

  QJsonArray instrumentRack;
  for (const InstrumentSlot &slot : m_instrumentRack) {
    QJsonObject slotJson;
    slotJson["enabled"] = slot.enabled;
    slotJson["name"] = slot.name;
    slotJson["pluginFormat"] = slot.pluginFormat;
    slotJson["pluginId"] = slot.pluginId;
    slotJson["pluginPath"] = slot.pluginPath;
    slotJson["patchPath"] = slot.patchPath;
    slotJson["presetName"] = slot.presetName;
    slotJson["program"] = slot.program;
    instrumentRack.append(slotJson);
  }
  root["instrumentRack"] = instrumentRack;

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

  QVector<InstrumentSlot> loadedInstrumentRack(m_trackCount);
  for (int i = 0; i < m_trackCount; ++i)
    loadedInstrumentRack[i] = defaultInstrumentSlot(i);

  const QJsonArray instrumentRackJson = root.value("instrumentRack").toArray();
  for (int i = 0;
       i < qMin(m_trackCount, static_cast<int>(instrumentRackJson.size()));
       ++i) {
    const QJsonObject slotJson = instrumentRackJson[i].toObject();
    InstrumentSlot slot = loadedInstrumentRack[i];
    slot.enabled = slotJson.value("enabled").toBool(slot.enabled);
    slot.name = slotJson.value("name").toString(slot.name).trimmed();
    slot.pluginFormat =
        slotJson.value("pluginFormat").toString(slot.pluginFormat).trimmed();
    slot.pluginId =
        slotJson.value("pluginId").toString(slot.pluginId).trimmed();
    slot.pluginPath =
        slotJson.value("pluginPath").toString(slot.pluginPath).trimmed();
    slot.patchPath =
        slotJson.value("patchPath").toString(slot.patchPath).trimmed();
    slot.presetName =
        slotJson.value("presetName").toString(slot.presetName).trimmed();
    slot.program =
        qBound(0, slotJson.value("program").toInt(slot.program), 2047);
    if (slot.name.isEmpty())
      slot.name = defaultInstrumentSlot(i).name;
    if (slot.pluginFormat.isEmpty())
      slot.pluginFormat = QStringLiteral("LV2");
    if (slot.pluginId.isEmpty())
      slot.pluginId = defaultInstrumentSlot(i).pluginId;
    if (slot.presetName.isEmpty())
      slot.presetName = QStringLiteral("Init");
    loadedInstrumentRack[i] = slot;
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
  bool instrumentRackChangedNow =
      m_instrumentRack.size() != loadedInstrumentRack.size();
  if (!instrumentRackChangedNow) {
    for (int i = 0; i < m_instrumentRack.size(); ++i) {
      const InstrumentSlot &current = m_instrumentRack[i];
      const InstrumentSlot &loaded = loadedInstrumentRack[i];
      if (current.enabled != loaded.enabled || current.name != loaded.name ||
          current.pluginFormat != loaded.pluginFormat ||
          current.pluginId != loaded.pluginId ||
          current.pluginPath != loaded.pluginPath ||
          current.patchPath != loaded.patchPath ||
          current.presetName != loaded.presetName ||
          current.program != loaded.program) {
        instrumentRackChangedNow = true;
        break;
      }
    }
  }

  {
    QMutexLocker locker(&m_mutex);
    m_tracks = loadedTracks;
    m_trackMidiChannels = loadedTrackMidiChannels;
    m_instrumentRack = loadedInstrumentRack;
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
  if (activeTrackChangedNow || instrumentRackChangedNow) {
    emit activeInstrumentChanged();
    emit instrumentRackChanged();
  }
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

  // Passthrough to the shared virtual output is for external DAWs/synths. When
  // the internal host is running, per-track outputs are used to avoid stacking
  // every hosted synth from the same shared MIDI stream.
  if (m_passthroughEnabled && !m_pluginHostRunning && m_midiOut &&
      m_virtualPortOpen) {
    try {
      std::vector<unsigned char> fwd(msg.begin(), msg.end());
      m_midiOut->sendMessage(&fwd);
    } catch (...) {
    }
  }

  // Audition live input through the active track's hosted instrument only.
  if (m_passthroughEnabled && (isNoteOn || isNoteOff)) {
    std::vector<unsigned char> trackMsg(msg.begin(), msg.end());
    if (!trackMsg.empty()) {
      const int outputChannel = activeTrackMidiChannel() - 1;
      trackMsg[0] = static_cast<unsigned char>((trackMsg[0] & 0xF0) |
                                               (outputChannel & 0x0F));
      sendTrackMessage(m_activeTrack, trackMsg);
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
    sendTrackNoteOff(ev.track, ev.note, ev.channel);
  m_activePlaybackNotes.clear();

  for (int trackIndex = 0; trackIndex < m_tracks.size(); ++trackIndex) {
    if (trackIndex < m_instrumentRack.size() &&
        !m_instrumentRack[trackIndex].enabled)
      continue;
    const auto &track = m_tracks[trackIndex];
    const ChordStep &step = track[playbackStep];
    const int outputChannel = trackIndex < m_trackMidiChannels.size()
                                  ? m_trackMidiChannels[trackIndex]
                                  : qMin(trackIndex, 15);
    for (const auto &ev : step) {
      if (!m_pluginHostRunning)
        sendNoteOn(ev.note, ev.velocity, outputChannel);
      sendTrackNoteOn(trackIndex, ev.note, ev.velocity, outputChannel);
      m_activePlaybackNotes.append(
          ActivePlaybackNote{ev.note, outputChannel, trackIndex});
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

void MidiEngine::sendTrackMessage(int track,
                                  const std::vector<unsigned char> &message) {
  if (track < 0 || track >= static_cast<int>(m_trackMidiOuts.size()) ||
      track >= m_trackVirtualPortsOpen.size() ||
      !m_trackVirtualPortsOpen[track] ||
      !m_trackMidiOuts[static_cast<size_t>(track)])
    return;

  try {
    std::vector<unsigned char> msg(message.begin(), message.end());
    m_trackMidiOuts[static_cast<size_t>(track)]->sendMessage(&msg);
  } catch (...) {
  }
}

void MidiEngine::sendTrackNoteOn(int track, int note, int velocity,
                                 int channel) {
  std::vector<unsigned char> msg = {
      static_cast<unsigned char>(0x90 | (channel & 0x0F)),
      static_cast<unsigned char>(note & 0x7F),
      static_cast<unsigned char>(velocity & 0x7F)};
  sendTrackMessage(track, msg);
}

void MidiEngine::sendTrackNoteOff(int track, int note, int channel) {
  std::vector<unsigned char> msg = {
      static_cast<unsigned char>(0x80 | (channel & 0x0F)),
      static_cast<unsigned char>(note & 0x7F), 0x00};
  sendTrackMessage(track, msg);
}

void MidiEngine::stopAllNotes() {
  for (const auto &ev : m_activePlaybackNotes)
    sendTrackNoteOff(ev.track, ev.note, ev.channel);
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

  for (int track = 0; track < m_trackCount; ++track) {
    for (int ch = 0; ch < 16; ++ch) {
      std::vector<unsigned char> msg = {static_cast<unsigned char>(0xB0 | ch),
                                        123, 0};
      sendTrackMessage(track, msg);
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
  m_pluginHostAutoConnectAudio =
      s.value("pluginHostAutoConnectAudio", true).toBool();
  const QVariantList trackMidiChannels = s.value("trackMidiChannels").toList();
  for (int i = 0;
       i < qMin(m_trackCount, static_cast<int>(trackMidiChannels.size()));
       ++i) {
    const int channel = trackMidiChannels[i].toInt();
    m_trackMidiChannels[i] = qBound(1, channel > 0 ? channel : i + 1, 16) - 1;
  }
  const QVariantList instrumentRack = s.value("instrumentRack").toList();
  for (int i = 0;
       i < qMin(m_trackCount, static_cast<int>(instrumentRack.size())); ++i) {
    const QVariantMap slotMap = instrumentRack[i].toMap();
    InstrumentSlot slot = defaultInstrumentSlot(i);
    slot.enabled = slotMap.value("enabled", slot.enabled).toBool();
    slot.name = slotMap.value("name", slot.name).toString().trimmed();
    slot.pluginFormat =
        slotMap.value("pluginFormat", slot.pluginFormat).toString().trimmed();
    slot.pluginId =
        slotMap.value("pluginId", slot.pluginId).toString().trimmed();
    slot.pluginPath =
        slotMap.value("pluginPath", slot.pluginPath).toString().trimmed();
    slot.patchPath =
        slotMap.value("patchPath", slot.patchPath).toString().trimmed();
    slot.presetName =
        slotMap.value("presetName", slot.presetName).toString().trimmed();
    slot.program =
        qBound(0, slotMap.value("program", slot.program).toInt(), 2047);
    if (slot.name.isEmpty())
      slot.name = defaultInstrumentSlot(i).name;
    if (slot.pluginFormat.isEmpty())
      slot.pluginFormat = QStringLiteral("LV2");
    if (slot.pluginId.isEmpty())
      slot.pluginId = defaultInstrumentSlot(i).pluginId;
    if (slot.presetName.isEmpty())
      slot.presetName = QStringLiteral("Init");
    m_instrumentRack[i] = slot;
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
  s.setValue("pluginHostAutoConnectAudio", m_pluginHostAutoConnectAudio);
  QVariantList trackMidiChannels;
  for (int channel : m_trackMidiChannels)
    trackMidiChannels << channel + 1;
  s.setValue("trackMidiChannels", trackMidiChannels);
  QVariantList instrumentRack;
  for (const InstrumentSlot &slot : m_instrumentRack) {
    QVariantMap slotMap;
    slotMap["enabled"] = slot.enabled;
    slotMap["name"] = slot.name;
    slotMap["pluginFormat"] = slot.pluginFormat;
    slotMap["pluginId"] = slot.pluginId;
    slotMap["pluginPath"] = slot.pluginPath;
    slotMap["patchPath"] = slot.patchPath;
    slotMap["presetName"] = slot.presetName;
    slotMap["program"] = slot.program;
    instrumentRack << slotMap;
  }
  s.setValue("instrumentRack", instrumentRack);
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
