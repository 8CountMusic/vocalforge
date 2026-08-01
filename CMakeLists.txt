cmake_minimum_required(VERSION 3.24)
project(VocalForge VERSION 1.2.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(FetchContent)

# ---- JUCE framework ----
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.15
    GIT_SHALLOW ON
)
FetchContent_MakeAvailable(JUCE)

# ---- Signalsmith Stretch (pitch + formant shifting, MIT licensed) ----
FetchContent_Declare(
    signalsmith-stretch
    GIT_REPOSITORY https://github.com/Signalsmith-Audio/signalsmith-stretch.git
    GIT_TAG 57b93f4e9206a089a45387eaa39bdc9f310d3308
)
FetchContent_MakeAvailable(signalsmith-stretch)

juce_add_plugin(VocalForge
    COMPANY_NAME "8 Count Music"
    BUNDLE_ID com.8countmusic.vocalforge12
    PLUGIN_MANUFACTURER_CODE Ecmu
    PLUGIN_CODE Vf12
    FORMATS VST3 Standalone
    PRODUCT_NAME "VocalForge 1.2"
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    COPY_PLUGIN_AFTER_BUILD FALSE
)

target_sources(VocalForge PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
)

target_include_directories(VocalForge PRIVATE src)

target_compile_definitions(VocalForge PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(VocalForge
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        signalsmith-stretch
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
)
