cmake_minimum_required(VERSION 3.22)

project(ParametricNAMStudio VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS" FORCE)
endif()

option(PNAM_BUILD_APP   "Build the JUCE desktop application" ON)
option(PNAM_BUILD_TESTS "Build the automated tests" ON)
option(PNAM_BUILD_CLI   "Build the pnam command-line tool" ON)
option(PNAM_LINUX_AUDIO "Linux only: build with ALSA/JACK audio (needs their headers)" OFF)

# ---------------------------------------------------------------------------
# Dependencies (pinned). For offline builds, point FETCHCONTENT_SOURCE_DIR_<NAME>
# at local checkouts, e.g. -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ---------------------------------------------------------------------------
include(FetchContent)

# NeuralAmpModelerCore (MIT). Submodules are NOT fetched: its Eigen submodule points at
# gitlab; we fetch the same Eigen tag (5.0.1) from a GitHub mirror below.
FetchContent_Declare(nam_core
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG        d3189ed482ded880c128bf312941266c4fa5ceec
    GIT_SUBMODULES ""
    SOURCE_SUBDIR  _pnam_no_cmake)

FetchContent_Declare(eigen
    GIT_REPOSITORY https://github.com/live-clones/eigen.git
    GIT_TAG        5.0.1
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  _pnam_no_cmake)

FetchContent_MakeAvailable(nam_core eigen)

if(PNAM_BUILD_APP)
    FetchContent_Declare(juce
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.17-security
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(juce)
endif()

# ---------------------------------------------------------------------------
# Libraries
# ---------------------------------------------------------------------------
add_subdirectory(NAMCore)          # nam_core: NeuralAmpModelerCore + file info + WAV I/O
add_subdirectory(ParametricEngine) # pnam_parametric: ParametricWaveNet format, loader, snapshot
add_subdirectory(ModelTraining)    # pnam_training: built-in control-conditioned trainer + validation
add_subdirectory(ModelLibrary)     # pnam_library: capture library, model library, capture planner, projects

add_subdirectory(AudioEngine)      # pnam_dsp: real-time amp stage, resampling, gate, presets (+ JUCE engine when building the app)

if(PNAM_BUILD_CLI)
    add_subdirectory(Source/cli)
endif()

if(PNAM_BUILD_APP)
    add_subdirectory(CabinetEngine)  # pnam_cab (JUCE): dual IR convolution
    add_subdirectory(Source)         # the desktop app (UserInterface sources are compiled here)
endif()

if(PNAM_BUILD_TESTS)
    enable_testing()
    add_subdirectory(Tests)
endif()
