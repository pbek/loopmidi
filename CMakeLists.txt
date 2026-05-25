cmake_minimum_required(VERSION 3.20)

# Version is injected by the Nix flake (or defaults to 0.0.0 for local dev)
if(NOT DEFINED APP_VERSION)
    set(APP_VERSION "0.0.0")
endif()

project(LoopMidi VERSION ${APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick)
find_package(PkgConfig REQUIRED)
pkg_check_modules(RTMIDI REQUIRED rtmidi)

qt_add_executable(loopmidi
    src/main.cpp
    src/MidiEngine.cpp
    src/MidiEngine.h
    resources.qrc
)

target_compile_definitions(loopmidi PRIVATE
    APP_VERSION="${APP_VERSION}"
)

target_include_directories(loopmidi PRIVATE
    ${RTMIDI_INCLUDE_DIRS}
    src
)

target_link_libraries(loopmidi PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    ${RTMIDI_LIBRARIES}
)

target_compile_options(loopmidi PRIVATE ${RTMIDI_CFLAGS_OTHER})

install(TARGETS loopmidi RUNTIME DESTINATION bin)
