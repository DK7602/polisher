# Polisher Blind Test: a loudness-matched, shuffled listening test from exported songs (see BlindTest.cpp).
juce_add_console_app(PolisherBlindTest PRODUCT_NAME "Polisher Blind Test")
target_sources(PolisherBlindTest PRIVATE BlindTest.cpp)
target_compile_definitions(PolisherBlindTest PRIVATE
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(PolisherBlindTest PRIVATE
    polisher_dsp
    juce::juce_audio_formats
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)
set_target_properties(PolisherBlindTest PROPERTIES FOLDER "Polisher")
