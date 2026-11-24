cmake_minimum_required(VERSION 3.24)

project(ModeMatch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the MSVC runtime statically so the released plugin and app load on PCs without the VC++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Product naming in one place (formerly "Radial Key"; renamed for trademark reasons, PLAN.md section 9),
# so a rename is an edit here plus new plugin codes.
set(MM_PRODUCT_NAME "Mode Match" CACHE STRING "Plugin name shown in hosts")
set(MM_COMPANY_NAME "Mode Match" CACHE STRING "Manufacturer name shown in hosts")

# JUCE 9.0.1, the version NuvoDeck builds with. NuvoDeck's checkout is reused when present (no download);
# elsewhere (e.g. the Mac AU build) it is fetched. Override with -DFETCHCONTENT_SOURCE_DIR_JUCE=<path>.
include(FetchContent)
set(_rk_nuvo_juce "C:/Dev/NuvoDeck/build/_deps/juce-src")
if(NOT DEFINED FETCHCONTENT_SOURCE_DIR_JUCE AND EXISTS "${_rk_nuvo_juce}/CMakeLists.txt")
    set(FETCHCONTENT_SOURCE_DIR_JUCE "${_rk_nuvo_juce}" CACHE PATH "Local JUCE checkout")
endif()
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# Analysis code: juce_core + std only, compiled into both the plugin and the tests.
set(MM_ANALYSIS_SOURCES
    src/analysis/KeyAnalyzer.cpp
    src/analysis/ModeDetector.cpp
    src/analysis/SpiralAnalyzer.cpp)

option(MM_BUILD_PLUGIN "Build the plugin (off: tests only)" ON)
option(MM_ENABLE_ARA "ARA 2: analyse whole clips offline in ARA hosts (fetches the ARA SDK)" ON)
option(MM_ENABLE_CLAP "Also build a CLAP plugin (fetches clap-juce-extensions)" ON)

# ARA SDK 2.3.0 (Apache 2.0), the version JUCE 9 expects. Only its headers and library sources are used.
if(MM_BUILD_PLUGIN AND MM_ENABLE_ARA)
    FetchContent_Declare(
        ara_sdk
        GIT_REPOSITORY https://github.com/Celemony/ARA_SDK.git
        GIT_TAG        releases/2.3.0
        GIT_SHALLOW    TRUE
        SOURCE_SUBDIR  do-not-build) # populate only
    FetchContent_MakeAvailable(ara_sdk)
    juce_set_ara_sdk_path("${ara_sdk_SOURCE_DIR}")
endif()

# ---------------------------------------------------------------------------------------------------------------
# The plugin. AU is built on macOS only (JUCE drops it on other platforms); VST3, CLAP and Standalone everywhere.
# ---------------------------------------------------------------------------------------------------------------
if(MM_BUILD_PLUGIN)
# The third-party notices, shown in the About box.
juce_add_binary_data(ModeMatchData SOURCES THIRD_PARTY_NOTICES.md)

if(MM_ENABLE_ARA)
    set(_mm_ara IS_ARA_EFFECT TRUE ARA_TRANSFORMATION_FLAGS kARAPlaybackTransformationNoChanges)
else()
    set(_mm_ara)
endif()

juce_add_plugin(ModeMatch
    COMPANY_NAME "${MM_COMPANY_NAME}"
    PRODUCT_NAME "${MM_PRODUCT_NAME}"
    BUNDLE_ID com.modematch.modematch
    PLUGIN_MANUFACTURER_CODE Mdmh
    PLUGIN_CODE Mdm1
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Analyzer Tools
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Mode Match analyses live input in the standalone app."
    ${_mm_ara})

if(MM_ENABLE_CLAP)
    FetchContent_Declare(
        clap_juce_extensions
        GIT_REPOSITORY https://github.com/free-audio/clap-juce-extensions.git
        GIT_TAG        3837f4a6ed2ab0ba46aba69ae5b7041c7c64ce74) # main as of 2026-11-24, builds with JUCE 9.0.1
    FetchContent_MakeAvailable(clap_juce_extensions)
    clap_juce_extensions_plugin(TARGET ModeMatch
        CLAP_ID "com.modematch.modematch"
        CLAP_FEATURES audio-effect analyzer utility stereo mono)
    # CLAP has no ARA, but the shared code is built with ARA on: link the ARA library as JUCE does for VST3 / AU.
    if(MM_ENABLE_ARA)
        target_sources(ModeMatch_CLAP PRIVATE src/ara/ARALibraryForCLAP.cpp)
    endif()
endif()

target_sources(ModeMatch PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/SpiralView.cpp
    src/ui/SummaryPanel.cpp
    src/ui/TimelineView.cpp
    src/ui/TopBar.cpp
    src/ui/AboutBox.cpp
    src/ara/ModeMatchARA.cpp
    ${MM_ANALYSIS_SOURCES})

target_include_directories(ModeMatch PRIVATE src)
target_include_directories(ModeMatch SYSTEM PRIVATE third_party)

target_compile_definitions(ModeMatch PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    MM_VERSION_STRING="${PROJECT_VERSION}")

target_link_libraries(ModeMatch
    PRIVATE
        ModeMatchData
        juce::juce_audio_utils
        juce::juce_opengl
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# Renders the editor to PNG from synthesised audio (a visual check, not shipped).
juce_add_console_app(ModeMatchRender PRODUCT_NAME "ModeMatchRender")
target_sources(ModeMatchRender PRIVATE
    tools/RenderUI.cpp
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/SpiralView.cpp
    src/ui/SummaryPanel.cpp
    src/ui/TimelineView.cpp
    src/ui/TopBar.cpp
    src/ui/AboutBox.cpp
    ${MM_ANALYSIS_SOURCES})
target_include_directories(ModeMatchRender PRIVATE src)
target_include_directories(ModeMatchRender SYSTEM PRIVATE third_party)
target_compile_definitions(ModeMatchRender PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="${MM_PRODUCT_NAME}" MM_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(ModeMatchRender PRIVATE ModeMatchData juce::juce_audio_utils juce::juce_opengl
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
set_target_properties(ModeMatchRender PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()

# ---------------------------------------------------------------------------------------------------------------
# Tests: NuvoDeck's 240-loop key test, streaming equivalence, loop overwrite, modes, Camelot.
# ---------------------------------------------------------------------------------------------------------------
juce_add_console_app(ModeMatchTests PRODUCT_NAME "ModeMatchTests")
target_sources(ModeMatchTests PRIVATE tests/ModeMatchTests.cpp ${MM_ANALYSIS_SOURCES})
target_include_directories(ModeMatchTests PRIVATE src)
target_include_directories(ModeMatchTests SYSTEM PRIVATE third_party)
target_compile_definitions(ModeMatchTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    MM_SOURCE_DIR="${CMAKE_SOURCE_DIR}") # optional real recordings are looked up here
target_link_libraries(ModeMatchTests
    PRIVATE
        juce::juce_core
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME ModeMatchTests COMMAND ModeMatchTests)
