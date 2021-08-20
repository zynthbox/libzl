cmake_minimum_required(VERSION 3.15)

project(libzl VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native -mcpu=native")

add_subdirectory(tracktion_engine/modules/juce)
add_subdirectory(tracktion_engine/modules)

find_package (PkgConfig REQUIRED)
pkg_check_modules (W2GTK4 REQUIRED webkit2gtk-4.0)

include_directories (${W2GTK4_INCLUDE_DIRS})
link_directories (${W2GTK4_LIBRARY_DIRS})

################################
#  JUCE MODULE STATIC LIBRARY  #
################################
#add_library(juce_plugin_modules STATIC)

#target_link_libraries(juce_plugin_modules
#    PRIVATE
#        juce::juce_core
#        juce::juce_events
#        juce::juce_audio_basics
#        juce::juce_audio_devices
#        juce::juce_audio_formats
#        juce::juce_audio_processors
#        juce::juce_audio_utils
#        tracktion::tracktion_engine
#        tracktion::tracktion_graph
#    PUBLIC
#        juce::juce_recommended_config_flags
#        juce::juce_recommended_lto_flags
#        juce::juce_recommended_warning_flags)

#target_compile_definitions(juce_plugin_modules
#    PUBLIC
#        JUCE_WEB_BROWSER=0
#        JUCE_USE_CURL=0
#        JUCE_JACK=1
#        JUCE_ALSA=0
#    INTERFACE
#        $<TARGET_PROPERTY:juce_plugin_modules,COMPILE_DEFINITIONS>)

#target_include_directories(juce_plugin_modules
#    INTERFACE
#        $<TARGET_PROPERTY:juce_plugin_modules,INCLUDE_DIRECTORIES>)

#set_target_properties(juce_plugin_modules PROPERTIES
#    POSITION_INDEPENDENT_CODE TRUE
#    VISIBILITY_INLINES_HIDDEN TRUE
#    C_VISIBILITY_PRESET hidden
#    CXX_VISIBILITY_PRESET hidden)

####################################
#  END JUCE MODULE STATIC LIBRARY  #
####################################



##########################
#  libzl SHARED LIBRARY  #
##########################

add_library(libzl SHARED)

set_target_properties(libzl PROPERTIES PREFIX "")

target_sources(libzl
    PRIVATE
        lib/libzl.cpp
        lib/ClipAudioSource.cpp
        lib/SyncTimer.cpp)

#target_link_libraries(libzl
#    PRIVATE
#    juce_plugin_modules)

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
        JUCE_JACK=0
        JUCE_ALSA=1
        TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    INTERFACE
        $<TARGET_PROPERTY:libzl,COMPILE_DEFINITIONS>)

target_link_libraries(libzl
    PRIVATE
        ${W2GTK4_LIBRARIES}
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
        juce::juce_recommended_warning_flags)
  
target_include_directories(libzl
    INTERFACE
        $<TARGET_PROPERTY:libzl,INCLUDE_DIRECTORIES>)

##############################
#  END libzl SHARED LIBRARY  #
##############################
