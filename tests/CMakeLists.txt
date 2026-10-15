include(FetchContent)
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(polisher_tests
    test_loudness.cpp
    test_truepeak.cpp
    test_limiter.cpp
    test_targeting.cpp
    test_clipper.cpp
    test_eq.cpp
    test_bass.cpp
    test_automaster.cpp
    test_imager.cpp
    test_dynamics.cpp
    test_glue.cpp
    test_levelmatch.cpp
    test_saturation.cpp
    test_stemeq.cpp
    test_spectral.cpp
    test_unlimiter.cpp
    test_dither.cpp
    test_validation.cpp
    test_reference.cpp
    test_monitoring.cpp
    test_songlistener.cpp
    test_genrecurve.cpp
    test_suite.cpp
    test_handoff.cpp
    test_autoanalyze.cpp)

target_link_libraries(polisher_tests PRIVATE polisher_dsp Catch2::Catch2WithMain)
set_target_properties(polisher_tests PROPERTIES FOLDER "Masterology")

include(Catch)
catch_discover_tests(polisher_tests)

# Plug-in-level tests: the real processor (all modules, parameters, MATCH, latency, state) as a DAW
# drives it. Built from the plug-in's processor sources without its editors (POLISHER_HEADLESS).
if(POLISHER_BUILD_PLUGIN)
    # The plug-in's version as JUCE packs it (major << 16 | minor << 8 | patch): the suite link publishes it.
    math(EXPR POLISHER_VERSION_CODE "(${PROJECT_VERSION_MAJOR} << 16) | (${PROJECT_VERSION_MINOR} << 8) | ${PROJECT_VERSION_PATCH}" OUTPUT_FORMAT HEXADECIMAL)
    juce_add_console_app(polisher_plugin_tests PRODUCT_NAME "Masterology Plugin Tests")
    target_sources(polisher_plugin_tests PRIVATE
        plugin/test_plugin.cpp
        ${CMAKE_SOURCE_DIR}/plugin/PluginProcessor.cpp
        ${CMAKE_SOURCE_DIR}/plugin/AutoMasterController.cpp
        ${CMAKE_SOURCE_DIR}/plugin/ReferenceTrack.cpp
        ${CMAKE_SOURCE_DIR}/plugin/ToneTargetStore.cpp)
    target_include_directories(polisher_plugin_tests PRIVATE ${CMAKE_SOURCE_DIR}/plugin)
    target_compile_definitions(polisher_plugin_tests PRIVATE
        POLISHER_HEADLESS=1
        JucePlugin_Name="Masterology"
        JucePlugin_VersionCode=${POLISHER_VERSION_CODE}
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_USE_MP3AUDIOFORMAT=1
        JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(polisher_plugin_tests PRIVATE
        polisher_dsp
        juce::juce_audio_processors
        juce::juce_audio_formats
        Catch2::Catch2
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    set_target_properties(polisher_plugin_tests PROPERTIES FOLDER "Masterology")
    catch_discover_tests(polisher_plugin_tests DISCOVERY_TIMEOUT 60)
endif()
