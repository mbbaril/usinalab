cmake_minimum_required(VERSION 3.22)
project(RuidoCore VERSION 0.1.0 LANGUAGES CXX)

# Runtime-library compatibility.
#
# Static-library objects must use the consumer runtime mode to avoid linker mismatch errors.
#
# Preserve CMAKE_MSVC_RUNTIME_LIBRARY supplied by a parent project.
# Apply a default only when configured independently.
#
# Use static runtime linkage and the matching debug runtime in Debug.
if(NOT DEFINED CMAKE_MSVC_RUNTIME_LIBRARY)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Portable C++17 libraries with separate sequencing and synthesis targets.
#
# The sequencer-only target omits synthesis tables and engine data.
# The full target adds synthesis while retaining the same consumer API.
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
    src/WaveBankData.cpp
    src/FmQuantizerTable.cpp
    src/ResonatorTables.cpp)
target_include_directories(ruidocore PUBLIC include)
target_link_libraries(ruidocore PUBLIC ruidocore_seq)

# Optional filesystem-backed shape library. Keep framework dependencies
# out of the portable core; consumers enable and link this target explicitly.
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
