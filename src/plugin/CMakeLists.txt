include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.9
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

# Copy each built plugin into the user's plugin folders (macOS ~/Library/Audio/Plug-Ins, Linux ~/.vst3). Off by
# default on Windows, where the system VST3 folder needs administrator rights.
if(WIN32)
  option(MNM_COPY_PLUGINS "Copy the plugins to the system plugin folders after building" OFF)
else()
  option(MNM_COPY_PLUGINS "Copy the plugins to the user's plugin folders after building" ON)
endif()

set(MNM_HARDENED_OPTS com.apple.security.cs.allow-jit com.apple.security.cs.allow-unsigned-executable-memory com.apple.security.cs.disable-library-validation)

# Instrument: every machine (synths + FX) in one plugin, FX machines fed from the side-chain input. The UI is drawn
# in the hardware's LCD style; the glyphs and fonts are read from the user's OS file at run time (one/RomArt.*).
juce_add_plugin(MnmOne
  PRODUCT_NAME "Monomodule One"
  COMPANY_NAME "Shnolk"
  BUNDLE_ID "world.shnolk.mnm.one"
  PLUGIN_MANUFACTURER_CODE Shnk
  PLUGIN_CODE Mnm6
  FORMATS AU VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD ${MNM_COPY_PLUGINS}
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS ${MNM_HARDENED_OPTS}
  MICROPHONE_PERMISSION_ENABLED FALSE)

# Monomodule Six: the same processor and editor running six tracks (multitimbral: MIDI channels 1-6 address
# tracks 1-6, one stereo output bus per track, FX machines fed from the previous track or the side-chain).
juce_add_plugin(MnmSix
  PRODUCT_NAME "Monomodule Six"
  COMPANY_NAME "Shnolk"
  BUNDLE_ID "world.shnolk.mnm.six"
  PLUGIN_MANUFACTURER_CODE Shnk
  PLUGIN_CODE Mnm7
  FORMATS AU VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD ${MNM_COPY_PLUGINS}
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS ${MNM_HARDENED_OPTS}
  MICROPHONE_PERMISSION_ENABLED FALSE)

# Monomodule FX: the same processor and editor as an audio effect. One track that offers the FX machines only
# (THRU, REVERB, CHORUS, DYNAMIX, RINGMOD, PHASER, FLANGER) and processes the plugin's main input.
juce_add_plugin(MnmFx
  PRODUCT_NAME "Monomodule FX"
  COMPANY_NAME "Shnolk"
  BUNDLE_ID "world.shnolk.mnm.modulefx"
  PLUGIN_MANUFACTURER_CODE Shnk
  PLUGIN_CODE Mnm8
  FORMATS AU VST3
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD ${MNM_COPY_PLUGINS}
  AU_MAIN_TYPE kAudioUnitType_Effect
  VST3_CATEGORIES Fx
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS ${MNM_HARDENED_OPTS}
  MICROPHONE_PERMISSION_ENABLED FALSE)

# Monomodule Euclid: Six with a generative Euclidean sequencer playing its six tracks (notes and p-locks), in sync with
# the host transport, and a SEQUENCER tab beside Six's editor (one/EuclidEditor.*, core gen/Generative.*).
juce_add_plugin(MnmEuclid
  PRODUCT_NAME "Monomodule Euclid"
  COMPANY_NAME "Shnolk"
  BUNDLE_ID "world.shnolk.mnm.euclid"
  PLUGIN_MANUFACTURER_CODE Shnk
  PLUGIN_CODE Mnm9
  FORMATS AU VST3 Standalone
  IS_SYNTH TRUE
  NEEDS_MIDI_INPUT TRUE
  NEEDS_MIDI_OUTPUT FALSE
  IS_MIDI_EFFECT FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS FALSE
  COPY_PLUGIN_AFTER_BUILD ${MNM_COPY_PLUGINS}
  AU_MAIN_TYPE kAudioUnitType_MusicDevice
  HARDENED_RUNTIME_ENABLED TRUE
  HARDENED_RUNTIME_OPTIONS ${MNM_HARDENED_OPTS}
  MICROPHONE_PERMISSION_ENABLED FALSE)

set(MNM_ONE_SOURCES one/OneProcessor.cpp one/EuclidEditor.cpp one/EuclidPanel.cpp one/MakuHost.cpp one/MakuPage.cpp one/OneEditor.cpp one/MachinePicker.cpp one/LibraryUi.cpp one/Lcd.cpp one/SkinDialog.cpp one/SpecData.cpp one/RomArt.cpp)
target_sources(MnmOne PRIVATE one/OneFactory.cpp ${MNM_ONE_SOURCES} Overlays.cpp)
target_sources(MnmSix PRIVATE one/SixFactory.cpp ${MNM_ONE_SOURCES} Overlays.cpp)
target_sources(MnmFx PRIVATE one/FxFactory.cpp ${MNM_ONE_SOURCES} Overlays.cpp)
target_sources(MnmEuclid PRIVATE one/EuclidFactory.cpp ${MNM_ONE_SOURCES} Overlays.cpp)
foreach(t MnmOne MnmSix MnmFx MnmEuclid)
  target_include_directories(${t} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(${t} PUBLIC
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)
  target_link_libraries(${t} PRIVATE mnmcore mnmlibrary juce::juce_audio_utils juce::juce_audio_devices
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endforeach()

# MAKU (ext/maku, GPL-3.0-only): the ambient generator inside Monomodule Euclid. Its firmware C is built as a shared
# library of its own with a GNU C compiler (gcc or clang; on Windows MinGW-w64 gcc, as MSVC does not take GNU C99),
# then embedded in the plugin (MakuData), which loads a private copy of it per instance (one/MakuHost.*).
option(MNM_MAKU "Build MAKU into Monomodule Euclid" ON)
set(MNM_MAKU_CC "" CACHE FILEPATH "GNU C compiler for the MAKU library (default: the C compiler; MSVC: MinGW-w64 gcc from PATH)")
set(mnm_maku_ok FALSE)
if(MNM_MAKU)
  set(mnm_maku_cc "${MNM_MAKU_CC}")
  if(NOT mnm_maku_cc)
    if(MSVC)
      find_program(mnm_maku_gcc NAMES x86_64-w64-mingw32-gcc gcc PATHS C:/mingw64/bin C:/msys64/mingw64/bin C:/ProgramData/mingw64/mingw64/bin)
      set(mnm_maku_cc "${mnm_maku_gcc}")
    else()
      set(mnm_maku_cc "${CMAKE_C_COMPILER}")
    endif()
  endif()
  if(mnm_maku_cc)
    set(mnm_maku_dir ${CMAKE_SOURCE_DIR}/ext/maku)
    set(mnm_maku_lib ${CMAKE_CURRENT_BINARY_DIR}/maku/maku_library.bin)
    set(mnm_maku_flags -O2 -std=gnu99 -w -ffp-contract=off -shared -I${mnm_maku_dir}/gen -I${mnm_maku_dir}/firmware/src)
    if(WIN32)
      list(APPEND mnm_maku_flags -I${mnm_maku_dir}/plugin/compat -static -static-libgcc)
    elseif(APPLE)
      list(APPEND mnm_maku_flags -fPIC -mmacosx-version-min=${CMAKE_OSX_DEPLOYMENT_TARGET})
      foreach(a ${CMAKE_OSX_ARCHITECTURES})
        list(APPEND mnm_maku_flags -arch ${a})
      endforeach()
    else()
      list(APPEND mnm_maku_flags -fPIC)
    endif()
    file(GLOB_RECURSE mnm_maku_sources CONFIGURE_DEPENDS ${mnm_maku_dir}/*.c ${mnm_maku_dir}/*.h)
    add_custom_command(OUTPUT ${mnm_maku_lib}
      COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/maku
      COMMAND ${mnm_maku_cc} ${mnm_maku_flags} ${mnm_maku_dir}/plugin/maku_plugin.c -o ${mnm_maku_lib} -lm
      DEPENDS ${mnm_maku_sources}
      COMMENT "MAKU library (${mnm_maku_cc})"
      VERBATIM)
    juce_add_binary_data(MnmMakuData NAMESPACE MakuData HEADER_NAME MakuData.h SOURCES ${mnm_maku_lib})
    set(mnm_maku_ok TRUE)
    message(STATUS "MAKU: built into Monomodule Euclid with ${mnm_maku_cc}")
  else()
    message(WARNING "MAKU: no GNU C compiler found (set MNM_MAKU_CC to a MinGW-w64 gcc); Monomodule Euclid is built without it")
  endif()
endif()
# Offscreen editor renderer for UI review (a development tool, not installed)
juce_add_console_app(MnmUiSnapshot PRODUCT_NAME "mnm-uisnapshot")
target_sources(MnmUiSnapshot PRIVATE UiSnapshot.cpp Overlays.cpp ${MNM_ONE_SOURCES})
target_include_directories(MnmUiSnapshot PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(MnmUiSnapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=0 JucePlugin_VersionString="${PROJECT_VERSION}"
  JucePlugin_Name="Monomodule" JUCE_STANDALONE_APPLICATION=1)
target_link_libraries(MnmUiSnapshot PRIVATE mnmcore mnmlibrary juce::juce_audio_utils juce::juce_recommended_config_flags)

# Audio capture harness: fixed scenarios rendered through the processor to WAV files, for before/after comparisons of
# the audio path (a development tool, not installed; see Capture.cpp)
juce_add_console_app(MnmCapture PRODUCT_NAME "mnm-capture")
target_sources(MnmCapture PRIVATE Capture.cpp Overlays.cpp ${MNM_ONE_SOURCES})
target_include_directories(MnmCapture PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(MnmCapture PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
  JUCE_MODAL_LOOPS_PERMITTED=0 JucePlugin_VersionString="${PROJECT_VERSION}"
  JucePlugin_Name="Monomodule" JUCE_STANDALONE_APPLICATION=1 MNM_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(MnmCapture PRIVATE mnmcore mnmlibrary juce::juce_audio_utils juce::juce_cryptography juce::juce_recommended_config_flags)
# MAKU: in Euclid (and the tools that build its processor); the other plugins compile MakuHost without it
foreach(t MnmEuclid MnmUiSnapshot MnmCapture)
  if(mnm_maku_ok)
    target_link_libraries(${t} PRIVATE MnmMakuData)
    target_compile_definitions(${t} PRIVATE MNM_HAS_MAKU=1)
  else()
    target_compile_definitions(${t} PRIVATE MNM_HAS_MAKU=0)
  endif()
endforeach()
if(MNM_BUILD_TESTS)
  # Monomodule FX must pass THRU flat: its filters must not track the effect's fixed trig note (issue #5). Needs the
  # OS file (MNM_OS or -DMNM_OS_SYX=...), else reported as skipped, like the unit tests.
  add_test(NAME fx-thru-flat COMMAND MnmCapture --check-fx-flat)
  set_tests_properties(fx-thru-flat PROPERTIES SKIP_RETURN_CODE 77)
  # Monomodule FX is trigged once: the knobs that act only on a trig must not close it (issue #2); One keeps the
  # hardware's behaviour
  add_test(NAME fx-continuous COMMAND MnmCapture --check-fx-continuous)
  set_tests_properties(fx-continuous PROPERTIES SKIP_RETURN_CODE 77)
  # Library previews through the sample-rate converter (no OS file needed), and the reported latency against what
  # bypass and (with the OS file) THRU really do
  add_test(NAME preview-resampling COMMAND MnmCapture --check-preview)
  add_test(NAME reported-latency COMMAND MnmCapture --check-latency)
  # MAKU inside Euclid: plays on its bus under a host transport (48 kHz), takes the host's tempo and the sequencer's
  # key, and keeps its world and likes in the session
  if(mnm_maku_ok)
    add_test(NAME maku-integration COMMAND MnmUiSnapshot --maku-check)
  endif()
endif()
