cmake_minimum_required(VERSION 3.15)

project(libzl VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mcpu=native")

find_package(Qt5 5.11 REQUIRED NO_MODULE COMPONENTS Core Gui Quick Qml)

set(CMAKE_AUTOMOC ON)

add_subdirectory(tracktion_engine/modules/juce)
add_subdirectory(tracktion_engine/modules)
include_directories(tracktion_engine/modules/)

set(ENV{PKG_CONFIG_PATH} "/usr/lib/pkgconfig:/usr/local/lib/pkgconfig")
find_package (PkgConfig REQUIRED)
pkg_check_modules (W2GTK4 REQUIRED webkit2gtk-4.0)
pkg_check_modules(RTMIDI REQUIRED rtmidi)
pkg_check_modules(Jack REQUIRED jack)

include_directories (${W2GTK4_INCLUDE_DIRS})
link_directories (${W2GTK4_LIBRARY_DIRS})


##########################
#  libzl SHARED LIBRARY  #
##########################

add_library(libzl SHARED)

set_target_properties(libzl PROPERTIES PREFIX "")

target_sources(libzl
    PRIVATE
        lib/libzl.cpp
        lib/ClipAudioSource.cpp
        lib/ClipAudioSourcePositionsModel.cpp
        lib/SamplerSynth.cpp
        lib/SamplerSynthSound.cpp
        lib/SamplerSynthVoice.cpp
        lib/SyncTimer.cpp
        lib/Helper.cpp
        lib/QPainterContext.cpp
        lib/WaveFormItem.cpp
        lib/AudioLevels.cpp)

set(libzl_headers
    lib/libzl.h)
set(libzl_subheaders
    lib/JUCEHeaders.h
    lib/ClipAudioSource.h
    lib/ClipCommand.h
    lib/SamplerSynth.h
    lib/SyncTimer.h)

target_compile_definitions(libzl
    PUBLIC
        JUCE_PLUGINHOST_AU=0
        JUCE_PLUGINHOST_LADSPA=0
        JUCE_PLUGINHOST_VST3=0
        JUCE_USE_CURL=0
        JUCE_WEB_BROWSER=0
        JUCER_ENABLE_GPL_MODE=1
        JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_REPORT_APP_USAGE=0
        JUCE_STRICT_REFCOUNTEDPOINTER=1
        JUCE_JACK=1
        JUCE_ALSA=0
        TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    INTERFACE
        $<TARGET_PROPERTY:libzl,COMPILE_DEFINITIONS>)

target_link_libraries(libzl
    PRIVATE
        ${RTMIDI_LIBRARIES}
        ${W2GTK4_LIBRARIES}
        ${Jack_LIBRARIES}
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_core
        juce::juce_events
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_basics
        juce::juce_gui_extra
        "-latomic"
        "-lcurl"
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
        Qt5::Core
        Qt5::Gui
        Qt5::Qml
        Qt5::Quick)

target_include_directories(libzl
    PRIVATE
        ${RTMIDI_INCLUDE_DIRS}
        ${Jack_INCLUDE_DIRS}
    INTERFACE
        $<TARGET_PROPERTY:libzl,INCLUDE_DIRECTORIES>)

install(TARGETS libzl
    LIBRARY DESTINATION lib)
install(FILES ${libzl_headers} DESTINATION include)
install(FILES ${libzl_subheaders} DESTINATION include/libzl)

##############################
#  END libzl SHARED LIBRARY  #
##############################
