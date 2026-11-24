# JUCE-free real-time pieces (tested directly by Tests/)
add_library(pnam_dsp STATIC
    dsp/Resampler.cpp
    ModelProcessor.cpp
    Presets.cpp)
target_include_directories(pnam_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnam_dsp PUBLIC pnam_library)
if(NOT MSVC)
    target_compile_options(pnam_dsp PRIVATE -O3)
endif()
