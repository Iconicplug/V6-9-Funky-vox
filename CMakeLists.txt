cmake_minimum_required(VERSION 3.22)
project(FunkVox VERSION 0.2.0)

set(CMAKE_CXX_STANDARD 17)
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

if(DEFINED ENV{CI})
    set(FUNKVOX_COPY FALSE)   # en GitHub Actions no se copia a Program Files
else()
    set(FUNKVOX_COPY TRUE)
endif()

juce_add_plugin(FunkVox
    FORMATS VST3 Standalone
    PRODUCT_NAME "FunkyVox V6-9"
    COMPANY_NAME "ICONICO"
    PLUGIN_MANUFACTURER_CODE Icnc
    PLUGIN_CODE Fv69
    DESCRIPTION "FunkyVox V6-9 - vocoder funk"
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${FUNKVOX_COPY})

# Imagen del pedal incrustada dentro del plugin (carpeta Assets/)
juce_add_binary_data(FunkVoxAssets SOURCES Assets/pedal_bg.png)

target_sources(FunkVox PRIVATE
    PluginProcessor.cpp PluginProcessor.h
    PluginEditor.cpp PluginEditor.h
    DSP.h)

target_compile_definitions(FunkVox PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(FunkVox PRIVATE
    FunkVoxAssets
    juce::juce_audio_utils
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
