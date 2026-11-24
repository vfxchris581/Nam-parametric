add_library(pnam_library STATIC
    CapturePlanner.cpp
    Libraries.cpp)
target_include_directories(pnam_library PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnam_library PUBLIC pnam_training)
