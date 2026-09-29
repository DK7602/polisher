set(POLISHER_FORMATS VST3 Standalone)
if(APPLE)
    list(APPEND POLISHER_FORMATS AU)
endif()

juce_add_plugin(Polisher
    PRODUCT_NAME                "Polisher"
    COMPANY_NAME                "Polisher Audio"
    COMPANY_WEBSITE             "https://example.com"
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
    VST3_CATEGORIES             Fx Mastering Dynamics
    AU_MAIN_TYPE                kAudioUnitType_Effect)

target_sources(Polisher PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp)

target_compile_definitions(Polisher PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Polisher
    PRIVATE
        polisher_dsp
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
