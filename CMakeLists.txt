cmake_minimum_required(VERSION 3.22)

project(WhyshoSY1 VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
#  Whysho SY1 — a modern, straightforward synthesizer plugin (VST3 / AU / SA)
#  Part of the Whysho plugin suite. Independent, self-contained build.
# ---------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Universal binaries on macOS (x86_64 + arm64) unless overridden by CI.
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

include(FetchContent)

# ---- JUCE -----------------------------------------------------------------
FetchContent_Declare(
    juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(juce)

# ---- Monocypher (Ed25519 verify, SHA-512 variant = standard Ed25519) ------
FetchContent_Declare(
    monocypher
    GIT_REPOSITORY https://github.com/LoupVaillant/Monocypher.git
    GIT_TAG        4.0.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(monocypher)

add_library(monocypher STATIC
    ${monocypher_SOURCE_DIR}/src/monocypher.c
    ${monocypher_SOURCE_DIR}/src/optional/monocypher-ed25519.c)
# Keep crypto_* symbols out of the plugin's exports (another plugin in the same
# host may embed a different Monocypher).
set_target_properties(monocypher PROPERTIES C_VISIBILITY_PRESET hidden)
target_include_directories(monocypher PUBLIC
    ${monocypher_SOURCE_DIR}/src
    ${monocypher_SOURCE_DIR}/src/optional)

# ---------------------------------------------------------------------------
juce_add_plugin(WhyshoSY1
    COMPANY_NAME                "whysho"
    COMPANY_WEBSITE             "https://whysho.com"
    BUNDLE_ID                   "com.whysho.sy1"
    PLUGIN_MANUFACTURER_CODE    Wsho
    PLUGIN_CODE                 Wsy1
    FORMATS                     VST3 AU Standalone
    PRODUCT_NAME                "whysho SY1"
    DESCRIPTION                 "whysho SY1 — modern virtual-analog synthesizer"
    VST3_CATEGORIES             Instrument Synth
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    VST3_AUTO_MANIFEST          FALSE)

juce_generate_juce_header(WhyshoSY1)

# Every .cpp under src/ belongs to the plugin (re-run cmake after adding files).
file(GLOB_RECURSE WHYSHO_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp)

target_sources(WhyshoSY1 PRIVATE ${WHYSHO_SOURCES})

target_compile_definitions(WhyshoSY1 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

target_include_directories(WhyshoSY1 PRIVATE src)

target_link_libraries(WhyshoSY1 PRIVATE
    monocypher
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_cryptography
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
#  Optional: offscreen GUI snapshot tool (local preview only, never shipped).
#  Build with -DWHYSHO_BUILD_SNAPSHOT=ON, run under xvfb-run.
# ---------------------------------------------------------------------------
option(WHYSHO_BUILD_SNAPSHOT "Build offscreen GUI snapshot tool" OFF)
if(WHYSHO_BUILD_SNAPSHOT)
    juce_add_console_app(WhyshoSY1_Snapshot PRODUCT_NAME "WhyshoSY1Snapshot")
    juce_generate_juce_header(WhyshoSY1_Snapshot)
    target_sources(WhyshoSY1_Snapshot PRIVATE tools/Snapshot.cpp ${WHYSHO_SOURCES})
    target_include_directories(WhyshoSY1_Snapshot PRIVATE src)
    target_compile_definitions(WhyshoSY1_Snapshot PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_CHECK_MEMORY_LEAKS=0 NDEBUG=1)
    target_link_libraries(WhyshoSY1_Snapshot PRIVATE
        monocypher
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
        juce::juce_gui_basics juce::juce_cryptography
        PUBLIC juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
#  Optional: headless audio render / preset QA tool (dev only, never shipped).
#  Build with -DWHYSHO_BUILD_RENDER=ON.
# ---------------------------------------------------------------------------
option(WHYSHO_BUILD_RENDER "Build headless render tool" OFF)
if(WHYSHO_BUILD_RENDER)
    juce_add_console_app(WhyshoSY1_Render PRODUCT_NAME "WhyshoSY1Render")
    juce_generate_juce_header(WhyshoSY1_Render)
    target_sources(WhyshoSY1_Render PRIVATE tools/Render.cpp ${WHYSHO_SOURCES})
    target_include_directories(WhyshoSY1_Render PRIVATE src)
    target_compile_definitions(WhyshoSY1_Render PUBLIC
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=1 JUCE_MODAL_LOOPS_PERMITTED=1
        JUCE_STANDALONE_APPLICATION=1 JUCE_DISPLAY_SPLASH_SCREEN=0
        JUCE_CHECK_MEMORY_LEAKS=0 NDEBUG=1)
    target_link_libraries(WhyshoSY1_Render PRIVATE
        monocypher
        juce::juce_audio_utils juce::juce_audio_processors juce::juce_dsp
        juce::juce_gui_basics juce::juce_cryptography
        PUBLIC juce::juce_recommended_config_flags)
endif()

# ---------------------------------------------------------------------------
#  Linux needs libcurl for JUCE's HTTPS (Windows/macOS use native networking).
# ---------------------------------------------------------------------------
if(UNIX AND NOT APPLE)
    find_package(CURL REQUIRED)
    target_link_libraries(WhyshoSY1 PRIVATE CURL::libcurl)
    foreach(t WhyshoSY1_Snapshot WhyshoSY1_Render)
        if(TARGET ${t})
            target_link_libraries(${t} PRIVATE CURL::libcurl)
        endif()
    endforeach()
endif()
