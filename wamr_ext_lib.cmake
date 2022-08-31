if (NOT DEFINED WAMR_EXT_ROOT_DIR)
    set(WAMR_EXT_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()
set(WAMR_ROOT_DIR ${WAMR_EXT_ROOT_DIR}/wasm-micro-runtime)
if (NOT WAMR_BUILD_PLATFORM)
    if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(WAMR_BUILD_PLATFORM "linux")
    endif()
endif()
if (NOT WAMR_BUILD_PLATFORM)
    message(FATAL_ERROR "WAMR_BUILD_PLATFORM not defined and cannot guess which platform to build WAMR")
endif()
if (NOT WAMR_BUILD_TARGET)
    message(FATAL_ERROR "WAMR_BUILD_TARGET not defined")
endif()
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_UVWASI 1)
set(WAMR_BUILD_LIB_PTHREAD 1)
set(WAMR_BUILD_DUMP_CALL_STACK 1)
set(WAMR_BUILD_CUSTOM_NAME_SECTION 1)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
add_library(wamr STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_link_libraries(wamr uv_a)

set(WAMR_EXT_INCLUDE_DIRS
        ${WAMR_EXT_ROOT_DIR}/include
        ${WAMR_ROOT_DIR}/core/iwasm/libraries
        ${WAMR_ROOT_DIR}/core/iwasm
        ${uvwasi_SOURCE_DIR}/src
        ${WAMR_EXT_ROOT_DIR}/third_party
        )

add_library(wamr_ext_obj OBJECT
        ${WAMR_EXT_ROOT_DIR}/src/base/Utility.cpp
        ${WAMR_EXT_ROOT_DIR}/src/base/FSUtility.cpp
        ${WAMR_EXT_ROOT_DIR}/src/base/WamrExtInternalDef.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WasiPthreadExt.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WasiWamrExt.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WasiFSExt.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WasiSocketExt.cpp
        )
target_include_directories(wamr_ext_obj PRIVATE ${WAMR_EXT_INCLUDE_DIRS})

add_library(wamr_ext_static STATIC
        $<TARGET_OBJECTS:wamr_ext_obj>
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_api/wamr_ext_api.cpp
        )
target_include_directories(wamr_ext_static PRIVATE include ${WAMR_EXT_INCLUDE_DIRS})
target_compile_definitions(wamr_ext_static PRIVATE -DWAMR_EXT_STATIC_LIB)
target_link_libraries(wamr_ext_static PRIVATE wamr)
