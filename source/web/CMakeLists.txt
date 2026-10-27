cmake_minimum_required(VERSION 3.22)
project(UsinaWeb LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(ROOT "${CMAKE_CURRENT_LIST_DIR}/..")
set(RUIDOCORE_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/libs/RuidoCore" ruidocore)
# Compile the actual desktop DSP, never a Javascript reconstruction or the older firmware.
file(GLOB FM CONFIGURE_DEPENDS "${ROOT}/src/engines/fm/*.cpp")
file(GLOB FX CONFIGURE_DEPENDS "${ROOT}/src/fx/*.cpp")
file(GLOB_RECURSE PORT CONFIGURE_DEPENDS
    "${ROOT}/src/engines/macro/plaits/*.cc" "${ROOT}/src/fx/clouds/*.cc"
    "${ROOT}/src/fx/texture/*.cc" "${ROOT}/src/port/stmlib/*.cc")
list(FILTER PORT EXCLUDE REGEX "resources_fm_banks\\.cc$")
# Generated upstream ports retain their original aggregate initialization (0.0f for false).
# MSVC accepts it; Clang diagnoses it as narrowing. Do not hand-edit the proven generated DSP.
set_source_files_properties(${PORT} PROPERTIES COMPILE_OPTIONS "-Wno-narrowing")
add_library(usina_web_dsp STATIC ${FM} ${FX} ${PORT}
    "${ROOT}/src/dsp/OutputGuard.cpp" "${ROOT}/src/seq/SeqPlayer.cpp" engine.cpp)
target_include_directories(usina_web_dsp PUBLIC "${ROOT}/src" "${CMAKE_CURRENT_LIST_DIR}")
target_link_libraries(usina_web_dsp PUBLIC ruidocore_seq)
target_compile_options(usina_web_dsp PRIVATE -ffp-contract=off)
if(EMSCRIPTEN)
    add_executable(filter_analyzer filter-response.cpp)
    target_include_directories(filter_analyzer PRIVATE "${ROOT}/src")
    target_compile_options(filter_analyzer PRIVATE -O3 -ffp-contract=off)
    target_link_options(filter_analyzer PRIVATE --no-entry -O3 -sMODULARIZE=1 -sEXPORT_ES6=1
        -sENVIRONMENT=web,worker,node -sFILESYSTEM=0 -sINITIAL_MEMORY=16777216
        -sALLOW_MEMORY_GROWTH=0 "-sEXPORTED_RUNTIME_METHODS=['HEAPF32']")
    set_target_properties(filter_analyzer PROPERTIES OUTPUT_NAME "filter-analyzer" SUFFIX ".mjs")
    add_executable(usina bindings.cpp)
    target_link_libraries(usina PRIVATE usina_web_dsp)
    target_compile_options(usina_web_dsp PRIVATE -O3)
    target_link_options(usina PRIVATE --no-entry -O3 -sMODULARIZE=1 -sEXPORT_ES6=1
        -sENVIRONMENT=web,worker,node -sFILESYSTEM=0 -sINITIAL_MEMORY=134217728
        -sALLOW_MEMORY_GROWTH=0 -sSTACK_SIZE=2097152
        "-sEXPORTED_RUNTIME_METHODS=['UTF8ToString','HEAPF32']")
    set_target_properties(usina PROPERTIES SUFFIX ".mjs")
    foreach(check Modal Seq)
        add_executable(usina_web_${check}_test "${ROOT}/tests/${check}Test.cpp")
        target_link_libraries(usina_web_${check}_test PRIVATE usina_web_dsp)
        target_link_options(usina_web_${check}_test PRIVATE -sENVIRONMENT=node -sSTACK_SIZE=8388608 -sINITIAL_MEMORY=134217728 -sALLOW_MEMORY_GROWTH=1)
    endforeach()
else()
    add_executable(usina_web_algorithms export-algorithms.cpp)
    target_link_libraries(usina_web_algorithms PRIVATE usina_web_dsp)
    add_executable(usina_web_reference reference.cpp bindings.cpp)
    target_link_libraries(usina_web_reference PRIVATE usina_web_dsp)
    add_executable(usina_web_runtime_test runtime-test.cpp)
    target_link_libraries(usina_web_runtime_test PRIVATE usina_web_dsp)
endif()
