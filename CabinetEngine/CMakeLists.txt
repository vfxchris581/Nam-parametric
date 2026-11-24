# JUCE sources are compiled into the app target (JUCE modules must be built exactly once per binary).
set(PNAM_CAB_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/DualIRCabinet.cpp PARENT_SCOPE)
set(PNAM_CAB_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR} PARENT_SCOPE)
