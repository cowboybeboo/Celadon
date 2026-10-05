cmake_minimum_required(VERSION 3.22)

project(Celadon VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Apple Silicon native build
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Celadon
    COMPANY_NAME "AdrianEliasson"
    BUNDLE_ID com.adrianeliasson.celadon
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    PLUGIN_MANUFACTURER_CODE Adel
    PLUGIN_CODE Cldn
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Celadon"
)

target_sources(Celadon PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

# Pixel fonts (SIL OFL 1.1) — build.sh downloads them into Assets/.
# The build still succeeds without them (system-font fallback).
set(CELADON_FONTS
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/PressStart2P-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/Silkscreen-Regular.ttf"
    "${CMAKE_CURRENT_SOURCE_DIR}/Assets/VT323-Regular.ttf"
)
set(CELADON_HAVE_FONTS TRUE)
foreach(f ${CELADON_FONTS})
    if(NOT EXISTS ${f})
        set(CELADON_HAVE_FONTS FALSE)
    endif()
endforeach()

if(CELADON_HAVE_FONTS)
    juce_add_binary_data(CeladonAssets SOURCES ${CELADON_FONTS})
    target_link_libraries(Celadon PRIVATE CeladonAssets)
    target_compile_definitions(Celadon PRIVATE CELADON_HAVE_FONTS=1)
    message(STATUS "Celadon: pixel fonts embedded")
else()
    target_compile_definitions(Celadon PRIVATE CELADON_HAVE_FONTS=0)
    message(WARNING "Celadon: Assets/ fonts missing - building with system-font fallback (run ./build.sh to fetch them)")
endif()

target_compile_definitions(Celadon PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(Celadon
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
