set(PNAM_UI_DIR ${CMAKE_SOURCE_DIR}/UserInterface)
set(PNAM_AUDIO_DIR ${CMAKE_SOURCE_DIR}/AudioEngine)

juce_add_gui_app(ParametricNAMStudio
    PRODUCT_NAME "Parametric NAM Studio"
    COMPANY_NAME "Parametric NAM Studio"
    BUNDLE_ID "io.github.vfxchris581.parametricnamstudio"
    VERSION ${PROJECT_VERSION}
    ICON_BIG ${CMAKE_SOURCE_DIR}/Resources/icon.png
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Parametric NAM Studio needs audio input to play your guitar through the amp models."
    DOCUMENT_EXTENSIONS nam pnampreset pnamproj)

target_sources(ParametricNAMStudio PRIVATE
    Main.cpp
    ${PNAM_AUDIO_DIR}/AppAudioEngine.cpp
    ${PNAM_CAB_SOURCES}
    ${PNAM_UI_DIR}/StudioLook.cpp
    ${PNAM_UI_DIR}/PlayPage.cpp
    ${PNAM_UI_DIR}/LibraryPage.cpp
    ${PNAM_UI_DIR}/StudioPage.cpp
    ${PNAM_UI_DIR}/MainComponent.cpp)

target_include_directories(ParametricNAMStudio PRIVATE ${PNAM_UI_DIR} ${PNAM_AUDIO_DIR} ${PNAM_CAB_INCLUDE})

target_compile_definitions(ParametricNAMStudio PRIVATE
    PNAM_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:ParametricNAMStudio,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:ParametricNAMStudio,JUCE_VERSION>"
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_ASIO=0
    JUCE_JACK=$<IF:$<BOOL:${PNAM_LINUX_AUDIO}>,1,0>
    JUCE_ALSA=$<IF:$<BOOL:${PNAM_LINUX_AUDIO}>,1,0>)

target_link_libraries(ParametricNAMStudio
    PRIVATE
        pnam_dsp
        juce::juce_gui_extra
        juce::juce_audio_utils
        juce::juce_audio_devices
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
