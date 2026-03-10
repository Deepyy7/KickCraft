cmake_minimum_required(VERSION 3.22)
project(KickCraft VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(JUCE_PATH "$ENV{HOME}/JUCE" CACHE PATH "Path to JUCE")
add_subdirectory(${JUCE_PATH} JUCE EXCLUDE_FROM_ALL)

juce_add_plugin(KickCraft
    COMPANY_NAME            "Gravitas Audio"
    PLUGIN_MANUFACTURER_CODE GrvA
    PLUGIN_CODE             Kc01
    FORMATS                 VST3 AU
    PRODUCT_NAME            "KickCraft"
    BUNDLE_ID               "com.gravitasaudio.kickcraft"
    IS_SYNTH                FALSE
    NEEDS_MIDI_INPUT        FALSE
    NEEDS_MIDI_OUTPUT       FALSE
    IS_MIDI_EFFECT          FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES         "Fx" "EQ" "Dynamics"
    AU_MAIN_TYPE            kAudioUnitType_Effect
    COPY_PLUGIN_AFTER_BUILD TRUE
    NEEDS_WEBVIEW2          TRUE
)

juce_generate_juce_header(KickCraft)

juce_add_binary_data(KickCraftResources
    SOURCES Resources/ui.html
)

target_sources(KickCraft PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
)

target_compile_definitions(KickCraft PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
)

if(MSVC)
    target_compile_options(KickCraft PRIVATE /Od /bigobj)
endif()

target_link_libraries(KickCraft PRIVATE
    KickCraftResources
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags
    $<$<NOT:$<BOOL:${MSVC}>>:juce::juce_recommended_lto_flags>
    $<$<NOT:$<BOOL:${MSVC}>>:juce::juce_recommended_warning_flags>
)
