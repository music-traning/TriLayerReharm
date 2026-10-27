cmake_minimum_required(VERSION 3.22)

project(TriLayerReharm VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# MSVC環境でのUTF-8ソースコード解釈を強制（日本語コメントによる改行消失バグ・C4819警告の防止）
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ローカルに JUCE ディレクトリがあればそれを使用し、無ければ FetchContent で自動取得
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/JUCE/CMakeLists.txt")
    add_subdirectory(JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.4
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(JUCE)
endif()

# WindowsのデフォルトVST3インストール先ディレクトリを明示設定
if(WIN32)
    set(DEFAULT_VST3_INSTALL_DIR "C:/Program Files/Common Files/VST3")
endif()

juce_add_plugin(TriLayerReharm
    COMPANY_NAME "Universal Make Associates"
    COMPANY_COPYRIGHT "Copyright (c) 2026 Universal Make Associates"
    BUNDLE_ID "com.UniversalMakeAssociates.TriLayerReharm"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT TRUE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_COPY_DIR "${DEFAULT_VST3_INSTALL_DIR}"
    PLUGIN_MANUFACTURER_CODE UMAs
    PLUGIN_CODE TLRh
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "TriLayerReharm"
)

target_sources(TriLayerReharm
    PRIVATE
        Source/PluginProcessor.h
        Source/PluginProcessor.cpp
        Source/PluginEditor.h
        Source/PluginEditor.cpp
        Source/Engine/VoicingDataTypes.h
        Source/Engine/ChordAnalyzer.h
        Source/Engine/ChordAnalyzer.cpp
        Source/Engine/ChordTemplateBank.h
        Source/Engine/ChordTemplateBank.cpp
        Source/Engine/SimpleAuditionSynth.h
        Source/Engine/MidiReharmonizerEngine.h
        Source/Engine/MidiReharmonizerEngine.cpp
        Source/UI/TriLayerOrbComponent.h
        Source/UI/TriLayerOrbComponent.cpp
        Source/UI/PianoRollComponent.h
        Source/UI/PianoRollComponent.cpp
)

target_include_directories(TriLayerReharm
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/Source
)

target_compile_definitions(TriLayerReharm
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(TriLayerReharm
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)
