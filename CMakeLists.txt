cmake_minimum_required(VERSION 3.20)
include(GNUInstallDirs)

# Version is injected by the Nix flake (or defaults to 0.0.0 for local dev)
if(NOT DEFINED APP_VERSION)
  set(APP_VERSION "0.0.0")
endif()

project(
  LoopMidi
  VERSION ${APP_VERSION}
  LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickDialogs2)
find_package(PkgConfig REQUIRED)
pkg_check_modules(RTMIDI REQUIRED rtmidi)

qt_policy(SET QTP0004 NEW)

qt_add_executable(loopmidi src/main.cpp src/MidiEngine.cpp src/MidiEngine.h)

# Register QML module — embeds all QML files and icons as Qt resources with
# proper absolute qrc:/ URLs, eliminating "qmlRegisterType requires absolute
# URLs" warnings from the old .qrc approach.
qt_add_qml_module(
  loopmidi
  URI
  LoopMidi
  VERSION
  1.0
  RESOURCE_PREFIX
  "/"
  NO_PLUGIN
  QML_FILES
  qml/main.qml
  qml/TransportButton.qml
  qml/LoopButton.qml
  qml/StatusPill.qml
  qml/SectionLabel.qml
  qml/NoteVisualizer.qml
  qml/StepCell.qml
  RESOURCES
  qml/qmldir
  icons/app.png)

target_compile_definitions(loopmidi PRIVATE APP_VERSION="${APP_VERSION}")

target_include_directories(loopmidi PRIVATE ${RTMIDI_INCLUDE_DIRS} src)

target_link_libraries(loopmidi PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
                                       Qt6::QuickDialogs2 ${RTMIDI_LIBRARIES})

target_compile_options(loopmidi PRIVATE ${RTMIDI_CFLAGS_OTHER})

install(TARGETS loopmidi RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Install desktop file and icon so xdg-desktop-portal can resolve the app ID
install(FILES loopmidi.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(
  FILES icons/app.png
  DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps
  RENAME loopmidi.png)
