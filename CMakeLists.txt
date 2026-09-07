cmake_minimum_required(VERSION 3.15)

project(EchoPre3 VERSION 0.3.0)   # bump VERSION to force Logic to rescan the AU after changes

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS deployment version")

enable_testing()

# -Wall/-Wextra are GCC/Clang flags; MSVC's cl.exe misparses "-Wextra" as the /W switch with a
# non-numeric argument (error D8021) — so a hardcoded "-Wextra" breaks the Windows CI build. Gate it.
if (MSVC)
    set(PEDAL_WARNING_FLAGS /W4)
else()
    set(PEDAL_WARNING_FLAGS -Wall -Wextra)
endif()

add_subdirectory(libs/JUCE)
add_subdirectory(libs/xsimd)          # optional; remove if not used
add_subdirectory(libs/chowdsp_wdf)

# Re-declare chowdsp_wdf's includes as SYSTEM so juce_recommended_warning_flags stops firing on its
# header-only constructors (-Wshadow-field-in-constructor, -Wfloat-equal -- harmless param/field name
# reuse, not bugs). Silencing those globally would blind us to real shadowing in our own code, which
# matters: a genuine -Wshadow hit in PluginProcessor.cpp was nearly lost in ~40 lines of library noise.
# CMake 3.15-compatible form (build.md).
get_target_property(_chowdsp_inc chowdsp_wdf INTERFACE_INCLUDE_DIRECTORIES)
set_target_properties(chowdsp_wdf PROPERTIES INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_chowdsp_inc}")

# Embed the UI art (processed from ui/ into assets/ui/ by tools/process_ui_assets.sh). The
# accessors in src/ui/Assets.h expect exactly these names; keep the two in sync when reskinning.
juce_add_binary_data(PedalAssets
    SOURCES
        assets/ui/knob.png
        assets/ui/trim_knob.png
        assets/ui/led_on.png
        assets/ui/led_off.png
        assets/ui/footswitch_up.png
        assets/ui/footswitch_down.png
        assets/ui/switch_up.png
        assets/ui/switch_mid.png
        assets/ui/switch_down.png
        assets/ui/texture.jpg
)

juce_add_plugin(EchoPre3
    COMPANY_NAME "Leigh Pierce"
    BUNDLE_ID com.leighpierce.echopre3
    PLUGIN_MANUFACTURER_CODE Lprc   # 4 chars, one uppercase
    PLUGIN_CODE Ep3p                # 4 chars, one uppercase, unique per plugin
    FORMATS AU VST3
    PRODUCT_NAME "Echo Pre 3"
    COPY_PLUGIN_AFTER_BUILD TRUE      # installs the AU to ~/Library/Audio/Plug-Ins/Components/
)

target_sources(EchoPre3
    PRIVATE
        src/PluginProcessor.cpp
        src/PluginEditor.cpp
        src/ui/PedalLookAndFeel.cpp
        src/ui/PedalFace.cpp
)

# So `#include "PluginProcessor.h"` / `"ui/PedalFace.h"` resolve without long relative paths.
target_include_directories(EchoPre3 PRIVATE src)

target_compile_definitions(EchoPre3
    PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(EchoPre3
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
        chowdsp::chowdsp_wdf
        PedalAssets                    # embedded UI art (src/ui/Assets.h)
        xsimd                          # remove if not used
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

target_compile_options(EchoPre3 PRIVATE ${PEDAL_WARNING_FLAGS})

# --- Tests --------------------------------------------------------------------------------------
# Register each pass/fail exe with add_test() so CI can run the whole suite via `ctest`.
# Linear-stage exes that only need chowdsp_wdf (no JUCE):
#   add_executable(<Name> tests/<Name>.cpp)
#   target_link_libraries(<Name> PRIVATE chowdsp::chowdsp_wdf)
#   target_compile_options(<Name> PRIVATE ${PEDAL_WARNING_FLAGS})
#   add_test(NAME <Name> COMMAND <Name>)

# Step 3 (CLAUDE.md): chowdsp_wdf smoke test, a trivial RC lowpass verified to a -3 dB point
# within 1% -- confirms the library/toolchain wiring before any pedal-specific DSP is written.
add_executable(WdfSmokeTest tests/WdfSmokeTest.cpp)
target_link_libraries(WdfSmokeTest PRIVATE chowdsp::chowdsp_wdf)
target_compile_options(WdfSmokeTest PRIVATE ${PEDAL_WARNING_FLAGS})
add_test(NAME WdfSmokeTest COMMAND WdfSmokeTest)

# Step 4 (CLAUDE.md): per-stage validation. Each linear stage is checked against the analytic
# transfer function of its own component values AND against the corners circuit.md derives
# independently, so a mis-wired tree and a mis-transcribed value fail differently.
foreach(_stageTest InputNetworkTest OutputNetworkTest JfetStageTest ChainTest)
    add_executable(${_stageTest} tests/${_stageTest}.cpp)
    target_include_directories(${_stageTest} PRIVATE src)
    target_link_libraries(${_stageTest} PRIVATE chowdsp::chowdsp_wdf)
    target_compile_options(${_stageTest} PRIVATE ${PEDAL_WARNING_FLAGS})
    add_test(NAME ${_stageTest} COMMAND ${_stageTest})
endforeach()
#
# Tests needing juce::dsp (Oversampling / FFT) must be JUCE console apps:
#   juce_add_console_app(<Name> PRODUCT_NAME "<Name>")
#   target_sources(<Name> PRIVATE tests/<Name>.cpp)
#   target_compile_definitions(<Name> PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1)
#   target_link_libraries(<Name> PRIVATE chowdsp::chowdsp_wdf juce::juce_dsp juce::juce_recommended_config_flags)
#   add_test(NAME <Name> COMMAND <Name>)
#
# OfflineRender — renders a WAV through the plugin for the analysis/ A/B harness. It does not
# mirror processBlock, it calls it on a real PedalAudioProcessor (see the header comment in
# analysis/offline_render.cpp), which is why it links the same sources as UISnapshot: the editor
# and PedalAssets come along with PluginProcessor.cpp's createEditor(). Not registered with
# add_test() — it is a tool, not a pass/fail check, and it needs a rendered signal to be useful.
juce_add_console_app(OfflineRender PRODUCT_NAME "OfflineRender")
target_sources(OfflineRender PRIVATE
    analysis/offline_render.cpp src/PluginProcessor.cpp src/PluginEditor.cpp
    src/ui/PedalLookAndFeel.cpp src/ui/PedalFace.cpp)
target_include_directories(OfflineRender PRIVATE src)
target_compile_definitions(OfflineRender PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(OfflineRender PRIVATE juce::juce_gui_extra juce::juce_audio_utils juce::juce_dsp
    chowdsp::chowdsp_wdf PedalAssets juce::juce_recommended_config_flags)
target_compile_options(OfflineRender PRIVATE ${PEDAL_WARNING_FLAGS})

# OSFidelity — how close 1x/2x/4x are to 8x, and what ADAA buys at each (build.md probes, build
# step 6). Registered as a FINITE-ONLY test: it asserts no NaN/Inf and that the wanted distortion
# does not move with the factor, but never gates on an absolute alias figure — that number is what
# the probe is for, and freezing it as a threshold would make the report circular.
juce_add_console_app(OSFidelity PRODUCT_NAME "OSFidelity")
target_sources(OSFidelity PRIVATE
    tests/OSFidelity.cpp src/PluginProcessor.cpp src/PluginEditor.cpp
    src/ui/PedalLookAndFeel.cpp src/ui/PedalFace.cpp)
target_include_directories(OSFidelity PRIVATE src)
target_compile_definitions(OSFidelity PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(OSFidelity PRIVATE juce::juce_gui_extra juce::juce_audio_utils juce::juce_dsp
    chowdsp::chowdsp_wdf PedalAssets juce::juce_recommended_config_flags)
target_compile_options(OSFidelity PRIVATE ${PEDAL_WARNING_FLAGS})
add_test(NAME OSFidelity COMMAND OSFidelity)

# PerfBenchmark — CPU % of realtime + reported latency per OS factor x mode (build.md probes).
# Finite-only, like OSFidelity: CI machine speed varies, so a CPU threshold would fail on a slow
# runner regardless of whether the DSP actually got dearer.
juce_add_console_app(PerfBenchmark PRODUCT_NAME "PerfBenchmark")
target_sources(PerfBenchmark PRIVATE
    tests/PerfBenchmark.cpp src/PluginProcessor.cpp src/PluginEditor.cpp
    src/ui/PedalLookAndFeel.cpp src/ui/PedalFace.cpp)
target_include_directories(PerfBenchmark PRIVATE src)
target_compile_definitions(PerfBenchmark PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(PerfBenchmark PRIVATE juce::juce_gui_extra juce::juce_audio_utils juce::juce_dsp
    chowdsp::chowdsp_wdf PedalAssets juce::juce_recommended_config_flags)
target_compile_options(PerfBenchmark PRIVATE ${PEDAL_WARNING_FLAGS})
add_test(NAME PerfBenchmark COMMAND PerfBenchmark)

# FeatureProfile — pairs each feature's CPU cost with its accuracy delta and prints the verdict
# (build.md probes, dsp.md "HQ / Eco mode"). Finite-only, like the other two probes.
juce_add_console_app(FeatureProfile PRODUCT_NAME "FeatureProfile")
target_sources(FeatureProfile PRIVATE
    tests/FeatureProfile.cpp src/PluginProcessor.cpp src/PluginEditor.cpp
    src/ui/PedalLookAndFeel.cpp src/ui/PedalFace.cpp)
target_include_directories(FeatureProfile PRIVATE src)
target_compile_definitions(FeatureProfile PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(FeatureProfile PRIVATE juce::juce_gui_extra juce::juce_audio_utils juce::juce_dsp
    chowdsp::chowdsp_wdf PedalAssets juce::juce_recommended_config_flags)
target_compile_options(FeatureProfile PRIVATE ${PEDAL_WARNING_FLAGS})
add_test(NAME FeatureProfile COMMAND FeatureProfile)

# BypassClickTest — does skipping the DSP while bypassed click, and is the skip transparent
# (architecture.md "Bypass")? Unlike the three probes above this one DOES gate: the crossfade bound,
# the exactness of true bypass and the settled equivalence are all machine-independent properties,
# not speed measurements, so a threshold on them is a real regression check rather than a CI-speed
# lottery. It carries its own mutation guard so a passing run cannot mean a blind instrument.
juce_add_console_app(BypassClickTest PRODUCT_NAME "BypassClickTest")
target_sources(BypassClickTest PRIVATE
    tests/BypassClickTest.cpp src/PluginProcessor.cpp src/PluginEditor.cpp
    src/ui/PedalLookAndFeel.cpp src/ui/PedalFace.cpp)
target_include_directories(BypassClickTest PRIVATE src)
target_compile_definitions(BypassClickTest PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(BypassClickTest PRIVATE juce::juce_gui_extra juce::juce_audio_utils juce::juce_dsp
    chowdsp::chowdsp_wdf PedalAssets juce::juce_recommended_config_flags)
target_compile_options(BypassClickTest PRIVATE ${PEDAL_WARNING_FLAGS})
add_test(NAME BypassClickTest COMMAND BypassClickTest)

# UISnapshot — headless editor render to PNG (build.md "UI changes"). Needs juce_gui_extra +
# PedalAssets and the shared UI/processor sources; it's a smoke test (asserts a non-empty image),
# so add_test() with no CPU/pixel gate.
juce_add_console_app(UISnapshot PRODUCT_NAME "UISnapshot")
target_sources(UISnapshot PRIVATE
    tests/UISnapshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp
    src/ui/PedalLookAndFeel.cpp src/ui/PedalFace.cpp)
target_include_directories(UISnapshot PRIVATE src)
target_compile_definitions(UISnapshot PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_STANDALONE_APPLICATION=1 JucePlugin_VersionString="${PROJECT_VERSION}")
target_link_libraries(UISnapshot PRIVATE juce::juce_gui_extra juce::juce_audio_utils juce::juce_dsp
    chowdsp::chowdsp_wdf PedalAssets juce::juce_recommended_config_flags)
add_test(NAME UISnapshot COMMAND UISnapshot)
