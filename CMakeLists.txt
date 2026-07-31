cmake_minimum_required(VERSION 3.22)

project(BlockRig VERSION 0.3.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    option(BLOCKRIG_UNIVERSAL "Build a universal (arm64 + x86_64) binary" OFF)
    if(BLOCKRIG_UNIVERSAL)
        set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
    endif()
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ---------------------------------------------------------------------------
# NAM Core (built as our own static library; upstream's CMakeLists is a
# top-level project that only builds tools, so we compile the sources directly)
# ---------------------------------------------------------------------------
set(NAM_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/NeuralAmpModelerCore")
set(NAM_DEPS_DIR "${NAM_CORE_DIR}/Dependencies")
set(ADT_DIR "${NAM_DEPS_DIR}/AudioDSPTools")

if(NOT EXISTS "${NAM_CORE_DIR}/NAM/dsp.h")
    message(FATAL_ERROR "NeuralAmpModelerCore submodule missing. Run: git submodule update --init --recursive")
endif()

file(GLOB NAM_CORE_SOURCES
    "${NAM_CORE_DIR}/NAM/*.cpp"
    "${NAM_CORE_DIR}/NAM/wavenet/*.cpp")

# AudioDSPTools pieces we use: noise gate, biquads (tone stack), and the
# ResamplingContainer (header-only).
set(ADT_SOURCES
    "${ADT_DIR}/dsp/dsp.cpp"
    "${ADT_DIR}/dsp/NoiseGate.cpp"
    "${ADT_DIR}/dsp/RecursiveLinearFilter.cpp")

add_library(nam_core STATIC ${NAM_CORE_SOURCES} ${ADT_SOURCES})

target_include_directories(nam_core PUBLIC
    "${NAM_CORE_DIR}"
    "${ADT_DIR}")
# Third-party headers as SYSTEM so their warnings don't drown ours.
target_include_directories(nam_core SYSTEM PUBLIC
    "${NAM_DEPS_DIR}/eigen"
    "${NAM_DEPS_DIR}/nlohmann")

target_compile_features(nam_core PUBLIC cxx_std_20)

# Route A2-shaped models through the hand-optimized fast-path WaveNet.
target_compile_definitions(nam_core PUBLIC NAM_ENABLE_A2_FAST)

if(MSVC)
    target_compile_definitions(nam_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_compile_options(nam_core PRIVATE $<$<CONFIG:Release>:/O2>)
else()
    target_compile_options(nam_core PRIVATE
        -Wno-unused-parameter
        $<$<CONFIG:Release>:-O3>)
endif()

set_target_properties(nam_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden)

# ---------------------------------------------------------------------------
# JUCE
# ---------------------------------------------------------------------------
add_subdirectory(third_party/JUCE)

# ---------------------------------------------------------------------------
# BlockRig: the block host. Shared sources, then one app target and one plugin
# target, so the standalone can own its audio device while the plugin build stays
# a plain juce_add_plugin.
# ---------------------------------------------------------------------------
set(BLOCKRIG_SOURCES
    src/BlockRigProcessor.cpp
    src/midi/MidiEngine.cpp
    src/state/RigState.cpp
    src/state/BlockFavorites.cpp
    src/state/Setlist.cpp
    src/state/Snapshots.cpp
    src/state/UndoHistory.cpp
    src/state/RigFiles.cpp
    src/state/PluginState.cpp
    src/host/BlockChain.cpp
    src/host/BlockInstance.cpp
    src/host/Transport.cpp
    src/host/PluginCatalog.cpp
    src/host/PluginScannerWorker.cpp
    src/host/InternalBlockFormat.cpp
    src/blocks/eq/EqBlockProcessor.cpp
    src/blocks/ir/IrBlockProcessor.cpp
    src/blocks/ir/IrLibrary.cpp
    src/blocks/utility/UtilityBlockProcessor.cpp
    src/blocks/nam/CaptureLibrary.cpp
    src/blocks/nam/DownloadsWatcher.cpp
    src/midi/MidiEngine.cpp
    src/net/Tone3000Client.cpp
    src/blocks/nam/NamBlockProcessor.cpp
    src/dsp/AmpSlot.cpp
    src/dsp/ToneStack.cpp
    src/dsp/ResamplingNam.cpp
    src/dsp/ModelLoader.cpp
    src/ui/Theme.cpp
    src/ui/BlockCategories.cpp
    src/ui/BlockWindow.cpp
    src/ui/CpuMeter.cpp
    src/ui/HeaderMeters.cpp
    src/ui/TransportBar.cpp
    src/ui/AppShell.cpp
    src/ui/SnapshotStrip.cpp
    src/ui/BuiltInBlockPanel.cpp
    src/ui/GigView.cpp
    src/ui/MidiPanel.cpp
    src/ui/Tone3000Panel.cpp
    src/ui/TunerPanel.cpp
    src/ui/LaneView.cpp
    src/ui/BlockPicker.cpp
    src/ui/NamBlockPanel.cpp
    src/ui/MainView.cpp
    src/ui/HostedEditor.cpp)

set(BLOCKRIG_DEFINITIONS
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_AU=1
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    # We never shipped a VST2, so there is no automation compatibility to keep.
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

juce_add_gui_app(BlockRigApp
    PRODUCT_NAME "BlockRig"
    COMPANY_NAME "BlockRig"
    BUNDLE_ID "com.blockrig.app"
    # Without this, macOS hands the app silence instead of input and never even
    # prompts: the device opens, callbacks run, and every sample is zero. It is
    # invisible from inside the process, and it does not affect a binary run from
    # a terminal, which inherits that terminal's permission.
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "BlockRig needs audio input to process your guitar."
    ICON_BIG "")

target_sources(BlockRigApp PRIVATE
    src/app/StandaloneApp.cpp
    ${BLOCKRIG_SOURCES})

target_include_directories(BlockRigApp PRIVATE src)

target_compile_definitions(BlockRigApp PRIVATE
    ${BLOCKRIG_DEFINITIONS}
    JUCE_STANDALONE_APPLICATION=1
    JUCE_MODAL_LOOPS_PERMITTED=1)

target_link_libraries(BlockRigApp PRIVATE
    juce::juce_cryptography
    $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_extra
    juce::juce_recommended_config_flags)

juce_add_plugin(BlockRigPlugin
    PRODUCT_NAME "BlockRig"
    COMPANY_NAME "BlockRig"
    BUNDLE_ID "com.blockrig.plugin"
    PLUGIN_MANUFACTURER_CODE Brig
    PLUGIN_CODE Brg1
    FORMATS VST3 AU
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(BlockRigPlugin PRIVATE
    src/PluginEntry.cpp
    ${BLOCKRIG_SOURCES})

target_include_directories(BlockRigPlugin PRIVATE src)

target_compile_definitions(BlockRigPlugin PUBLIC ${BLOCKRIG_DEFINITIONS})

target_link_libraries(BlockRigPlugin PUBLIC
    $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>)

target_link_libraries(BlockRigPlugin PRIVATE
    juce::juce_cryptography
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_devices
    juce::juce_dsp
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags)

# ---------------------------------------------------------------------------
# Headless DSP checks (no JUCE dependency)
# ---------------------------------------------------------------------------
option(BLOCKRIG_BUILD_TESTS "Build the headless DSP tests" ON)

if(BLOCKRIG_BUILD_TESTS)
    add_executable(dsp_tests
        tests/dsp_tests.cpp
        src/dsp/ToneStack.cpp
        src/dsp/ResamplingNam.cpp)

    target_include_directories(dsp_tests PRIVATE src)
    target_link_libraries(dsp_tests PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>)
    target_compile_features(dsp_tests PRIVATE cxx_std_20)

    add_executable(bench
        tests/bench.cpp
        src/dsp/ToneStack.cpp
        src/dsp/ResamplingNam.cpp)

    target_include_directories(bench PRIVATE src)
    target_link_libraries(bench PRIVATE $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>)
    target_compile_features(bench PRIVATE cxx_std_20)

    # Chain engine tests: real hosted plug-ins, live edits, CPU attribution.
    juce_add_console_app(chain_tests PRODUCT_NAME "chain_tests")

    target_sources(chain_tests PRIVATE
        tests/chain_tests.cpp
        src/host/BlockChain.cpp
        src/host/BlockInstance.cpp
    src/host/Transport.cpp
        src/host/InternalBlockFormat.cpp
        src/blocks/eq/EqBlockProcessor.cpp
    src/blocks/ir/IrBlockProcessor.cpp
    src/blocks/ir/IrLibrary.cpp
    src/blocks/utility/UtilityBlockProcessor.cpp
    src/blocks/nam/CaptureLibrary.cpp
    src/blocks/nam/NamBlockProcessor.cpp
        src/dsp/AmpSlot.cpp
        src/dsp/ToneStack.cpp
        src/dsp/ResamplingNam.cpp
        src/dsp/ModelLoader.cpp
        src/state/PluginState.cpp)

    target_include_directories(chain_tests PRIVATE src)

    target_compile_definitions(chain_tests PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(chain_tests PRIVATE
        juce::juce_cryptography
        $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    # Plug-in scanning tests. This binary is both coordinator and scanner child:
    # the coordinator relaunches it with the scanner UID, which is exactly the
    # shipping arrangement, so crash containment is really exercised.
    juce_add_console_app(scan_tests PRODUCT_NAME "scan_tests")

    target_sources(scan_tests PRIVATE
        tests/scan_tests.cpp
        src/host/PluginCatalog.cpp
        src/host/PluginScannerWorker.cpp
        src/host/InternalBlockFormat.cpp
        src/blocks/eq/EqBlockProcessor.cpp
    src/blocks/ir/IrBlockProcessor.cpp
    src/blocks/ir/IrLibrary.cpp
    src/blocks/utility/UtilityBlockProcessor.cpp
    src/blocks/nam/CaptureLibrary.cpp
    src/blocks/nam/NamBlockProcessor.cpp
        src/dsp/AmpSlot.cpp
        src/dsp/ToneStack.cpp
        src/dsp/ResamplingNam.cpp
        src/dsp/ModelLoader.cpp
        src/state/PluginState.cpp)

    target_include_directories(scan_tests PRIVATE src)

    target_compile_definitions(scan_tests PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(scan_tests PRIVATE
        juce::juce_cryptography
        $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    # Rig save/restore against the real host processor.
    juce_add_console_app(rig_tests PRODUCT_NAME "rig_tests")

    target_sources(rig_tests PRIVATE
        tests/rig_tests.cpp
        tests/headless_editor_stub.cpp
        src/BlockRigProcessor.cpp
        src/midi/MidiEngine.cpp
        src/state/RigState.cpp
    src/state/BlockFavorites.cpp
    src/state/Setlist.cpp
    src/state/Snapshots.cpp
    src/state/UndoHistory.cpp
    src/state/RigFiles.cpp
        src/host/BlockChain.cpp
        src/host/BlockInstance.cpp
    src/host/Transport.cpp
        src/host/PluginCatalog.cpp
        src/host/PluginScannerWorker.cpp
        src/host/InternalBlockFormat.cpp
        src/blocks/eq/EqBlockProcessor.cpp
    src/blocks/ir/IrBlockProcessor.cpp
    src/blocks/ir/IrLibrary.cpp
    src/blocks/utility/UtilityBlockProcessor.cpp
    src/blocks/nam/CaptureLibrary.cpp
    src/blocks/nam/NamBlockProcessor.cpp
        src/dsp/AmpSlot.cpp
        src/dsp/ToneStack.cpp
        src/dsp/ResamplingNam.cpp
        src/dsp/ModelLoader.cpp
        src/state/PluginState.cpp)

    target_include_directories(rig_tests PRIVATE src)

    target_compile_definitions(rig_tests PRIVATE
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(rig_tests PRIVATE
        juce::juce_cryptography
        $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    enable_testing()
    add_test(NAME dsp_tests
             COMMAND dsp_tests "${NAM_CORE_DIR}/example_models")
    add_test(NAME chain_tests
             COMMAND chain_tests "${NAM_CORE_DIR}/example_models")
    add_test(NAME rig_tests
             COMMAND rig_tests "${NAM_CORE_DIR}/example_models")

    # scan_tests is deliberately NOT in the default set: it scans every plug-in
    # on the machine out-of-process and takes minutes. Run it directly as a soak
    # test after touching anything in host/PluginCatalog or PluginScannerWorker.
    add_test(NAME scan_tests COMMAND scan_tests)
    set_tests_properties(scan_tests PROPERTIES
        LABELS "soak"
        TIMEOUT 1800
        DISABLED TRUE)
endif()
