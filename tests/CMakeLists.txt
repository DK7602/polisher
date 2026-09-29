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
    test_automaster.cpp)

target_link_libraries(polisher_tests PRIVATE polisher_dsp Catch2::Catch2WithMain)
set_target_properties(polisher_tests PROPERTIES FOLDER "Polisher")

include(Catch)
catch_discover_tests(polisher_tests)
