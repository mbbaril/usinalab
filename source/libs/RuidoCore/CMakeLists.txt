cmake_minimum_required(VERSION 3.22)
project(RuidoCore VERSION 0.1.0 LANGUAGES CXX)

# --- Agree with whoever links this (2026-09-15) ------------------------
#
# A static library links no C runtime of its own; the final binary does.
# But /MT vs /MD is compiled INTO these objects, so the choice still has
# to match the consumer or the link fails with LNK2038.
#
# GUARDED on purpose. Percutere builds this tree with add_subdirectory
# and sets CMAKE_MSVC_RUNTIME_LIBRARY before doing so; that value is
# inherited here and must win, so this project never overrides a parent
# that has already decided. The default only applies when RuidoCore is
# configured on its own, for its demo and test executables.
#
# The value matches the rest of the family (see Volca Drum 250fdc8):
# static, with Debug on the debug runtime.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Core library - pure C++17, no framework dependency. This is the part
# meant to be genuinely portable into any host (a JUCE plugin, a
# standalone app, a different audio framework entirely).
# Split into two targets on 2026-09-15, when the Volca-family products
# started adopting this library for their SEQUENCER half.
#
# Those products send MIDI to external hardware and synthesise nothing at
# all, so the three Plaits/FM table translation units below are pure dead
# weight to them - large data blobs linked into a plugin that can never
# call them. `ruidocore_seq` is the pattern/rhythm/modulation half on its
# own; `ruidocore` is that plus synthesis, and keeps its name and its
# full contents so every existing consumer (Percutere) is unaffected.
add_library(ruidocore_seq STATIC
    src/Sequencer.cpp
    src/Mutation.cpp
    src/Evolution.cpp
    src/Euclidean.cpp
    src/TrigConditions.cpp
    src/LogicCombine.cpp
    src/TuringRegister.cpp
    src/Scale.cpp
    src/Generator.cpp
    src/Glitch.cpp
    src/ModEngine.cpp)
target_include_directories(ruidocore_seq PUBLIC include)

add_library(ruidocore STATIC
    src/PlaitsWaveData.cpp
    src/FmQuantizerTable.cpp
    src/PlaitsResonatorTables.cpp)
target_include_directories(ruidocore PUBLIC include)
target_link_libraries(ruidocore PUBLIC ruidocore_seq)

# ModShapeLibrary is the one module that depends on JUCE (juce::File for
# shared shapes-folder filesystem access) - kept as an opt-in add-on, not
# part of the core target, so consuming the pure-C++ core never drags in
# JUCE. Off by default; a host project that already has JUCE available
# can turn this on and add its own JUCE module link.
option(RUIDOCORE_WITH_JUCE_MODSHAPES "Build the JUCE-dependent ModShapeLibrary module" OFF)
if(RUIDOCORE_WITH_JUCE_MODSHAPES)
    add_library(ruidocore_modshapes STATIC src/ModShapeLibrary.cpp)
    target_include_directories(ruidocore_modshapes PUBLIC include)
    target_link_libraries(ruidocore_modshapes PUBLIC juce::juce_core)
endif()

option(RUIDOCORE_BUILD_TOOLS "Build the offline render/test tools" ON)
if(RUIDOCORE_BUILD_TOOLS)
    add_executable(ruidocore_fm_demo tools/render_fm_demo.cpp)
    target_link_libraries(ruidocore_fm_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_fm_demo PRIVATE tools)

    add_executable(ruidocore_wavetable_demo tools/render_wavetable_demo.cpp)
    target_link_libraries(ruidocore_wavetable_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_wavetable_demo PRIVATE tools)

    add_executable(ruidocore_bassdrum_demo tools/render_bassdrum_demo.cpp)
    target_link_libraries(ruidocore_bassdrum_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_bassdrum_demo PRIVATE tools)

    add_executable(ruidocore_snaredrum_demo tools/render_snaredrum_demo.cpp)
    target_link_libraries(ruidocore_snaredrum_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_snaredrum_demo PRIVATE tools)

    add_executable(ruidocore_hihat_demo tools/render_hihat_demo.cpp)
    target_link_libraries(ruidocore_hihat_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_hihat_demo PRIVATE tools)

    add_executable(ruidocore_modal_demo tools/render_modal_demo.cpp)
    target_link_libraries(ruidocore_modal_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_modal_demo PRIVATE tools)

    add_executable(ruidocore_string_demo tools/render_string_demo.cpp)
    target_link_libraries(ruidocore_string_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_string_demo PRIVATE tools)

    add_executable(ruidocore_filterednoise_demo tools/render_filterednoise_demo.cpp)
    target_link_libraries(ruidocore_filterednoise_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_filterednoise_demo PRIVATE tools)

    add_executable(ruidocore_particlenoise_demo tools/render_particlenoise_demo.cpp)
    target_link_libraries(ruidocore_particlenoise_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_particlenoise_demo PRIVATE tools)

    add_executable(ruidocore_swarm_demo tools/render_swarm_demo.cpp)
    target_link_libraries(ruidocore_swarm_demo PRIVATE ruidocore)
    target_include_directories(ruidocore_swarm_demo PRIVATE tools)
endif()

if(RUIDOCORE_BUILD_TOOLS)
    add_executable(ruidocore_test_stability tools/test_modal_repeat.cpp)
    target_link_libraries(ruidocore_test_stability PRIVATE ruidocore)
    target_include_directories(ruidocore_test_stability PRIVATE tools)

    add_executable(ruidocore_test_note_resolution tools/test_note_resolution.cpp)
    target_link_libraries(ruidocore_test_note_resolution PRIVATE ruidocore_seq)
    add_executable(ruidocore_test_sequencer_stamp tools/test_sequencer_stamp.cpp)
    target_link_libraries(ruidocore_test_sequencer_stamp PRIVATE ruidocore_seq)
    add_executable(ruidocore_test_glitch_profile tools/test_glitch_profile.cpp)
    target_link_libraries(ruidocore_test_glitch_profile PRIVATE ruidocore_seq)
    add_executable(ruidocore_test_generator_profile tools/test_generator_profile.cpp)
    target_link_libraries(ruidocore_test_generator_profile PRIVATE ruidocore_seq)
    add_executable(ruidocore_test_mutation_profile tools/test_mutation_profile.cpp)
    target_link_libraries(ruidocore_test_mutation_profile PRIVATE ruidocore_seq)
endif()
