cmake_minimum_required(VERSION 3.22)
project(Saturator VERSION 0.5.0)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(Saturator
    COMPANY_NAME "Onyx Audio"
    PLUGIN_MANUFACTURER_CODE Onxa
    PLUGIN_CODE Onyx
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "Onyx"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE)

target_sources(Saturator PRIVATE
    PluginProcessor.cpp
    PluginProcessor.h
    PluginEditor.cpp
    PluginEditor.h
    AbyssEngine.h)

# ---- Optional embedded assets: background image + Cinzel font.
# Put background.jpg and Cinzel.ttf next to this file. If one is missing the plugin
# still builds (plain dark background / system serif font).
set(ONYX_ASSETS "")
set(ONYX_DEFS "")

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/background.jpg")
    list(APPEND ONYX_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/background.jpg")
    list(APPEND ONYX_DEFS ONYX_HAS_BG=1)
endif()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/Cinzel.ttf")
    file(SIZE "${CMAKE_CURRENT_SOURCE_DIR}/Cinzel.ttf" ONYX_FONT_SIZE)
    if(ONYX_FONT_SIZE GREATER 10000)
        list(APPEND ONYX_ASSETS "${CMAKE_CURRENT_SOURCE_DIR}/Cinzel.ttf")
        list(APPEND ONYX_DEFS ONYX_HAS_CINZEL=1)
    endif()
endif()

if(ONYX_ASSETS)
    juce_add_binary_data(OnyxAssets SOURCES ${ONYX_ASSETS})
    set_target_properties(OnyxAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)
    target_link_libraries(Saturator PRIVATE OnyxAssets)
    target_compile_definitions(Saturator PRIVATE ${ONYX_DEFS})
endif()

target_compile_definitions(Saturator PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(Saturator
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
