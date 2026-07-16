cmake_minimum_required(VERSION 3.22)
project(CollabSync VERSION 0.8.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")

# Build libopus from bundled source
set(OPUS_BUILD_SHARED_LIBRARY OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_TESTING        OFF CACHE BOOL "" FORCE)
set(OPUS_DISABLE_INTRINSICS   ON  CACHE BOOL "" FORCE)
add_subdirectory(opus)

# JUCE is bundled in the project folder
add_subdirectory(JUCE)

# Plugin formats and post-build install are platform-specific.
# macOS: build VST3 + AU and copy into the system plugin folders.
# Windows/Linux: build VST3 + Standalone; leave copying to CI/packaging.
if(APPLE)
    set(COLLABSYNC_FORMATS VST3 AU Standalone)
    set(COLLABSYNC_COPY_AFTER_BUILD TRUE)
else()
    set(COLLABSYNC_FORMATS VST3 Standalone)
    set(COLLABSYNC_COPY_AFTER_BUILD FALSE)
endif()

juce_add_plugin(CollabSync
    COMPANY_NAME "CollabSync"
    PLUGIN_MANUFACTURER_CODE Clbs
    PLUGIN_CODE Csyn
    FORMATS ${COLLABSYNC_FORMATS}
    PRODUCT_NAME "CollabSync"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    # Only auto-copy into system folders on macOS; the hardcoded /Library
    # paths below are meaningless on other platforms.
    COPY_PLUGIN_AFTER_BUILD ${COLLABSYNC_COPY_AFTER_BUILD}
    VST3_COPY_DIR "/Library/Audio/Plug-Ins/VST3"
    AU_COPY_DIR   "/Library/Audio/Plug-Ins/Components"
)

juce_generate_juce_header(CollabSync)

# Bundled DM Sans / DM Mono fonts (OFL-licensed, see Assets/Fonts/*-LICENSE.txt)
# for the redesigned UI — embedded as binary data so nothing is fetched at
# runtime. Loaded via juce::Typeface::createSystemTypefaceFor in
# Source/UI/CollabSyncLookAndFeel.cpp.
juce_add_binary_data(CollabSyncFonts SOURCES
    Assets/Fonts/DMSans-Regular.ttf
    Assets/Fonts/DMSans-Medium.ttf
    Assets/Fonts/DMSans-SemiBold.ttf
    Assets/Fonts/DMSans-Bold.ttf
    Assets/Fonts/DMMono-Regular.ttf
    Assets/Fonts/DMMono-Medium.ttf
)

target_sources(CollabSync PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/UI/CollabSyncLookAndFeel.cpp
    Source/UI/CollabSyncComponents.cpp
    Source/UI/GeneratedFileCard.cpp
    Source/Network/SignalingClient.cpp
    Source/Network/SignalingServer.cpp
    Source/Network/PeerConnection.cpp
    Source/Audio/OpusCodec.cpp
    Source/Audio/JitterBuffer.cpp
    Source/Audio/SharedAudioBuffer.cpp
    Source/Midi/MidiCapture.cpp
    Source/Clock/SessionClock.cpp
    Source/Session/Recorder.cpp
)

target_compile_definitions(CollabSync PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(CollabSync PRIVATE
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_dsp
    juce::juce_gui_basics
    juce::juce_core
    opus
    CollabSyncFonts
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)

# Windows networking: getpeername/inet_ntop live in ws2_32, GetAdaptersAddresses
# in iphlpapi. Linked explicitly so non-MSVC toolchains (that ignore the
# #pragma comment directives in SignalingServer.cpp) still link cleanly.
if(WIN32)
    target_link_libraries(CollabSync PRIVATE ws2_32 iphlpapi)
endif()

# Ad-hoc sign the plugin after build so DAWs can load it without Gatekeeper blocking
# --deep is deprecated by Apple and signs nested code inconsistently. These
# bundles embed no dylibs (opus is static), so signing the bundle alone is
# sufficient and behaves predictably across machines.
if(APPLE)
    add_custom_command(TARGET CollabSync_VST3 POST_BUILD
        COMMAND codesign --force -s -
            "/Library/Audio/Plug-Ins/VST3/CollabSync.vst3"
        COMMENT "Ad-hoc signing CollabSync.vst3"
    )
    add_custom_command(TARGET CollabSync_AU POST_BUILD
        COMMAND codesign --force -s -
            "/Library/Audio/Plug-Ins/Components/CollabSync.component"
        COMMENT "Ad-hoc signing CollabSync.component"
    )
endif()
