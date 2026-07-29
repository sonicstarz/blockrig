cmake_minimum_required(VERSION 3.22)

project(NAMModeler VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.15" CACHE STRING "Minimum macOS version")
    option(NAMMODELER_UNIVERSAL "Build a universal (arm64 + x86_64) binary" OFF)
    if(NAMMODELER_UNIVERSAL)
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

juce_add_plugin(NAMModeler
    PRODUCT_NAME "NAM Modeler"
    COMPANY_NAME "NAM Modeler"
    BUNDLE_ID "com.nammodeler.nammodeler"
    PLUGIN_MANUFACTURER_CODE Nmdl
    PLUGIN_CODE Nmd1
    FORMATS VST3 AU Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD TRUE
    VST3_CATEGORIES Fx Distortion
    AU_MAIN_TYPE kAudioUnitType_Effect)

target_sources(NAMModeler PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp
    src/dsp/AmpSlot.cpp
    src/dsp/ToneStack.cpp
    src/dsp/ResamplingNam.cpp
    src/dsp/ModelLoader.cpp
    src/state/Parameters.cpp
    src/state/PluginState.cpp)

target_include_directories(NAMModeler PRIVATE src)

target_compile_definitions(NAMModeler PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

# NAM registers its architectures ("WaveNet", "LSTM", ...) with file-scope static
# objects. A normal static-library link drops those translation units, because
# nothing references their symbols directly, and every model then fails to load
# with "No config parser registered for architecture". Forcing the whole archive
# keeps the registrars alive. PUBLIC so it propagates to the VST3/AU/Standalone
# targets, which are where the real link happens.
target_link_libraries(NAMModeler PUBLIC
    $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>)

target_link_libraries(NAMModeler PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Headless DSP checks (no JUCE dependency)
# ---------------------------------------------------------------------------
option(NAMMODELER_BUILD_TESTS "Build the headless DSP tests" ON)

if(NAMMODELER_BUILD_TESTS)
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

    # Integration tests drive the real AudioProcessor, so they need JUCE and the
    # plugin sources (minus the format wrapper's createPluginFilter entry point).
    juce_add_console_app(plugin_tests PRODUCT_NAME "plugin_tests")

    target_sources(plugin_tests PRIVATE
        tests/plugin_tests.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/dsp/AmpSlot.cpp
        src/dsp/ToneStack.cpp
        src/dsp/ResamplingNam.cpp
        src/dsp/ModelLoader.cpp
        src/state/Parameters.cpp
        src/state/PluginState.cpp)

    target_include_directories(plugin_tests PRIVATE src)

    target_compile_definitions(plugin_tests PRIVATE
        NAMMODELER_HEADLESS_TEST=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(plugin_tests PRIVATE
        $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>
        juce::juce_audio_utils
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    # Chain engine tests: real hosted plug-ins, live edits, CPU attribution.
    juce_add_console_app(chain_tests PRODUCT_NAME "chain_tests")

    target_sources(chain_tests PRIVATE
        tests/chain_tests.cpp
        src/host/BlockChain.cpp
        src/host/BlockInstance.cpp
        src/host/InternalBlockFormat.cpp
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
        $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    # P0 de-risk spike: proves plugin hosting works and measures its cost.
    # Throwaway once the real host engine lands (docs/16-BUILD-PLAN.md P0).
    juce_add_console_app(host_spike PRODUCT_NAME "host_spike")

    # Links our own plugin sources too, so the same DSP can be measured both
    # in-process and hosted-as-VST3 in a single run.
    target_sources(host_spike PRIVATE
        tests/host_spike.cpp
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/dsp/AmpSlot.cpp
        src/dsp/ToneStack.cpp
        src/dsp/ResamplingNam.cpp
        src/dsp/ModelLoader.cpp
        src/state/Parameters.cpp
        src/state/PluginState.cpp)

    target_include_directories(host_spike PRIVATE src)

    target_compile_definitions(host_spike PRIVATE
        NAMMODELER_HEADLESS_TEST=1
        JUCE_PLUGINHOST_VST3=1
        JUCE_PLUGINHOST_AU=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_STANDALONE_APPLICATION=1
        JUCE_MODAL_LOOPS_PERMITTED=1)

    target_link_libraries(host_spike PRIVATE
        $<LINK_LIBRARY:WHOLE_ARCHIVE,nam_core>
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
        juce::juce_recommended_config_flags)

    enable_testing()
    add_test(NAME dsp_tests
             COMMAND dsp_tests "${NAM_CORE_DIR}/example_models")
    add_test(NAME plugin_tests
             COMMAND plugin_tests "${NAM_CORE_DIR}/example_models")
    add_test(NAME chain_tests
             COMMAND chain_tests "${NAM_CORE_DIR}/example_models")
endif()
