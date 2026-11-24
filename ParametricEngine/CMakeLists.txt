add_library(pnam_parametric STATIC
    ParametricWaveNet.cpp
    ParametricModel.cpp
    Snapshot.cpp)
target_include_directories(pnam_parametric PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnam_parametric PUBLIC nam_core)
