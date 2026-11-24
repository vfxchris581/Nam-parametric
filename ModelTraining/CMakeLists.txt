add_library(pnam_training STATIC
    TrainableWaveNet.cpp
    Trainer.cpp
    TrainingSignal.cpp
    Validation.cpp
    Export.cpp)
target_include_directories(pnam_training PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pnam_training PUBLIC pnam_parametric)
if(NOT MSVC)
    target_compile_options(pnam_training PRIVATE -O3)
endif()
