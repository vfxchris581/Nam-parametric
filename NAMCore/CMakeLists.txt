# Builds NeuralAmpModelerCore as a static library, plus our NAM file inspection and WAV I/O.
set(NAM_DIR ${nam_core_SOURCE_DIR})

file(GLOB NAM_SOURCES_TOP "${NAM_DIR}/NAM/*.cpp")
file(GLOB NAM_SOURCES_SUB "${NAM_DIR}/NAM/*/*.cpp")

add_library(nam_core STATIC
    ${NAM_SOURCES_TOP}
    ${NAM_SOURCES_SUB}
    NamFileInfo.cpp
    WavIO.cpp)

target_include_directories(nam_core SYSTEM PUBLIC
    ${NAM_DIR}
    ${NAM_DIR}/Dependencies/nlohmann
    ${eigen_SOURCE_DIR})
target_include_directories(nam_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST NAM_SAMPLE_FLOAT EIGEN_MPL2_ONLY)
if(WIN32)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
endif()
if(MSVC)
    target_compile_options(nam_core PRIVATE /W2 /bigobj)
else()
    target_compile_options(nam_core PRIVATE -w)
endif()
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
    target_link_libraries(nam_core PUBLIC Threads::Threads)
endif()
