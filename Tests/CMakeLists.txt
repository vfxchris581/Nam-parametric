set(PNAM_FIXTURES "${CMAKE_SOURCE_DIR}/Resources/fixtures")

add_executable(pnam_core_tests
    TestMain.cpp
    test_nam_compat.cpp
    test_training.cpp)
target_link_libraries(pnam_core_tests PRIVATE pnam_training)
target_compile_definitions(pnam_core_tests PRIVATE PNAM_FIXTURES="${PNAM_FIXTURES}")
add_test(NAME core COMMAND pnam_core_tests)
target_sources(pnam_core_tests PRIVATE test_library.cpp)
target_link_libraries(pnam_core_tests PRIVATE pnam_library)
target_sources(pnam_core_tests PRIVATE test_engine.cpp)
target_link_libraries(pnam_core_tests PRIVATE pnam_dsp)
