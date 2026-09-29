add_library(polisher_dsp STATIC
    src/KWeighting.cpp
    src/TruePeakDetector.cpp
    src/LoudnessMeter.cpp
    src/TruePeakLimiter.cpp
    src/SoftClipper.cpp
    src/LoudnessTargeting.cpp)

target_include_directories(polisher_dsp PUBLIC include)
target_compile_features(polisher_dsp PUBLIC cxx_std_20)

if(MSVC)
    target_compile_options(polisher_dsp PRIVATE /W4 /fp:precise)
else()
    target_compile_options(polisher_dsp PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
endif()

set_target_properties(polisher_dsp PROPERTIES POSITION_INDEPENDENT_CODE ON FOLDER "Polisher")
