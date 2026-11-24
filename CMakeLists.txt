cmake_minimum_required(VERSION 3.24)

project(ModeMatch VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# ---------------------------------------------------------------------------------------------------------------
# The plugin. AU is built on macOS only (JUCE drops it on other platforms); VST3 and Standalone everywhere.
# ---------------------------------------------------------------------------------------------------------------
if(MM_BUILD_PLUGIN)
juce_add_plugin(ModeMatch
    COMPANY_NAME "${MM_COMPANY_NAME}"
    PRODUCT_NAME "${MM_PRODUCT_NAME}"
    BUNDLE_ID com.modematch.modematch
    PLUGIN_MANUFACTURER_CODE Mdmh
    PLUGIN_CODE Mdm1
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Analyzer Tools
    AU_MAIN_TYPE kAudioUnitType_Effect
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Mode Match analyses live input in the standalone app.")

target_sources(ModeMatch PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/ui/SpiralView.cpp
    src/ui/SummaryPanel.cpp
    src/ui/TimelineView.cpp
    src/ui/TopBar.cpp
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
        juce::juce_audio_utils
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
    ${MM_ANALYSIS_SOURCES})
target_include_directories(ModeMatchRender PRIVATE src)
target_include_directories(ModeMatchRender SYSTEM PRIVATE third_party)
target_compile_definitions(ModeMatchRender PRIVATE
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JucePlugin_Name="${MM_PRODUCT_NAME}")
target_link_libraries(ModeMatchRender PRIVATE juce::juce_audio_utils
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
