cmake_minimum_required(VERSION 3.15)

set(CMAKE_BUILD_TYPE Debug)

project(libzl VERSION 0.1.0)

# TODO : Check if works without the following flags as it is already defined in JUCE MODULE STATIC LIBRARY
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DJUCE_ALSA=0 -DJUCE_JACK=1")

add_subdirectory(JUCE)

################################
#  JUCE MODULE STATIC LIBRARY  #
################################
add_library(juce_plugin_modules STATIC)

target_link_libraries(juce_plugin_modules
    PRIVATE
        juce::juce_audio_basics
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_audio_processors
        juce::juce_audio_utils  
        juce::juce_core
        juce::juce_data_structures
        juce::juce_events
        juce::juce_graphics
        juce::juce_gui_basics
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

target_compile_definitions(juce_plugin_modules
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_JACK=1
        JUCE_ALSA=0
    INTERFACE
        $<TARGET_PROPERTY:juce_plugin_modules,COMPILE_DEFINITIONS>)

target_include_directories(juce_plugin_modules
    INTERFACE
        $<TARGET_PROPERTY:juce_plugin_modules,INCLUDE_DIRECTORIES>)

set_target_properties(juce_plugin_modules PROPERTIES
    POSITION_INDEPENDENT_CODE TRUE
    VISIBILITY_INLINES_HIDDEN TRUE
    C_VISIBILITY_PRESET hidden
    CXX_VISIBILITY_PRESET hidden)

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
        lib/ZynthiLoopsComponent.cpp
        lib/SyncTimer.cpp)

target_link_libraries(libzl
    PRIVATE      
    juce_plugin_modules)
  
target_include_directories(libzl
    INTERFACE
        $<TARGET_PROPERTY:libzl,INCLUDE_DIRECTORIES>)

##############################
#  END libzl SHARED LIBRARY  #
##############################
