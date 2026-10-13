set(POLISHER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND POLISHER_FORMATS AU)
endif()

# No COMPANY_WEBSITE yet: hosts show the plug-in's web address, so add one only when there is a real
# product page (an empty one is better than a placeholder).
juce_add_plugin(Polisher
    PRODUCT_NAME                "Masterology"
    COMPANY_NAME                "Masterology Audio"
    BUNDLE_ID                   com.polisheraudio.polisher
    PLUGIN_MANUFACTURER_CODE    Plsh
    PLUGIN_CODE                 Pls1
    FORMATS                     ${POLISHER_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    NEEDS_WEBVIEW2              TRUE
    VST3_CATEGORIES             Fx Mastering Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect)

# The liquid-glass web UI, embedded in the binary. File names must be unique (they are served flat).
file(GLOB POLISHER_UI_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.html"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/assets/*"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/fonts/*.ttf")

juce_add_binary_data(PolisherUIData
    HEADER_NAME PolisherUIData.h
    NAMESPACE   PolisherUI
    SOURCES
        ${POLISHER_UI_FILES}
        "${CMAKE_SOURCE_DIR}/docs/manual/polisher-manual.md"
        "${juce_SOURCE_DIR}/modules/juce_gui_extra/native/javascript/index.js"
        "${juce_SOURCE_DIR}/modules/juce_gui_extra/native/javascript/check_native_interop.js")
# The user manual ships inside the plug-in (MANUAL in Quick Help). Keep it in step with the plug-in:
# the build stops if its version line doesn't match this release.
file(READ "${CMAKE_SOURCE_DIR}/docs/manual/polisher-manual.md" POLISHER_MANUAL_TEXT)
if(NOT POLISHER_MANUAL_TEXT MATCHES "Version ${PROJECT_VERSION} ")
    message(FATAL_ERROR "docs/manual/polisher-manual.md is not updated for v${PROJECT_VERSION}: update the manual (and its 'Version' line) for this release.")
endif()
set_target_properties(PolisherUIData PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_sources(Polisher PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp
    PolisherWebEditor.cpp
    AutoMasterController.cpp
    ReferenceTrack.cpp
    ToneTargetStore.cpp)

target_compile_definitions(Polisher PUBLIC
    JUCE_WEB_BROWSER=1
    JUCE_USE_WIN_WEBVIEW2_WITH_STATIC_LINKING=1
    JUCE_USE_CURL=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Polisher
    PRIVATE
        polisher_dsp
        PolisherUIData
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
