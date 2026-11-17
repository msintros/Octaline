cmake_minimum_required(VERSION 3.22)

project(Octaline VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(OCTALINE_BUILD_PLUGIN "Build the VST3/AU plugin (downloads JUCE)" ON)
option(OCTALINE_BUILD_TESTS "Build the engine tests" ON)
option(OCTALINE_INSTALL "Copy the built plugin into your plug-in folders after building" OFF)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

# ---- DSP engine (no framework dependency) ------------------------------------
add_library(octaline_dsp STATIC src/dsp/Engine.cpp src/plugin/FactoryPresets.cpp)
target_include_directories(octaline_dsp PUBLIC src)
set_target_properties(octaline_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ---- Tests -------------------------------------------------------------------
if(OCTALINE_BUILD_TESTS)
    enable_testing()
    add_executable(octaline_tests tests/EngineTests.cpp)
    target_link_libraries(octaline_tests PRIVATE octaline_dsp)
    add_test(NAME engine COMMAND octaline_tests)
endif()

# ---- Plugin ------------------------------------------------------------------
if(OCTALINE_BUILD_PLUGIN)
    include(FetchContent)
    # To build offline, pass -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.12
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)

    set(OCTALINE_FORMATS VST3)
    if(APPLE)
        list(APPEND OCTALINE_FORMATS AU)
    endif()

    juce_add_plugin(Octaline
        COMPANY_NAME "Octaline"
        BUNDLE_ID "com.octaline.octaline"
        PLUGIN_MANUFACTURER_CODE Octl
        PLUGIN_CODE Oc8d
        FORMATS ${OCTALINE_FORMATS}
        PRODUCT_NAME "Octaline"
        DESCRIPTION "8-band modulation delay"
        IS_SYNTH FALSE
        NEEDS_MIDI_INPUT FALSE
        NEEDS_MIDI_OUTPUT FALSE
        IS_MIDI_EFFECT FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        VST3_CATEGORIES Fx Delay
        AU_MAIN_TYPE kAudioUnitType_Effect
        COPY_PLUGIN_AFTER_BUILD ${OCTALINE_INSTALL})

    target_sources(Octaline PRIVATE
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/PresetManager.cpp)

    target_compile_definitions(Octaline PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
        JUCE_DISPLAY_SPLASH_SCREEN=0)

    target_link_libraries(Octaline
        PRIVATE
            octaline_dsp
            juce::juce_audio_utils
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endif()

# ---- Headless plugin harness (processor, presets, state, editor snapshots) ----
if(OCTALINE_BUILD_PLUGIN AND OCTALINE_BUILD_TESTS)
    juce_add_console_app(octaline_harness PRODUCT_NAME "Octaline Harness")
    target_sources(octaline_harness PRIVATE
        tests/PluginHarness.cpp
        src/plugin/PluginProcessor.cpp
        src/plugin/PluginEditor.cpp
        src/plugin/PresetManager.cpp)
    target_compile_definitions(octaline_harness PRIVATE
        JucePlugin_Name="Octaline"
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1)
    target_link_libraries(octaline_harness PRIVATE
        octaline_dsp
        juce::juce_audio_utils
        juce::juce_recommended_config_flags)
endif()
