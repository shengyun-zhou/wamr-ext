if (NOT DEFINED WAMR_EXT_ROOT_DIR)
    set(WAMR_EXT_ROOT_DIR ${CMAKE_CURRENT_LIST_DIR})
endif()
set(WAMR_ROOT_DIR ${WAMR_EXT_ROOT_DIR}/wasm-micro-runtime)
if (NOT WAMR_BUILD_PLATFORM)
    if (ANDROID)
        set(WAMR_BUILD_PLATFORM "android")
    elseif (CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(WAMR_BUILD_PLATFORM "linux")
    endif()
endif()
if (NOT WAMR_BUILD_PLATFORM)
    message(FATAL_ERROR "WAMR_BUILD_PLATFORM not defined and cannot guess which platform to build WAMR")
endif()
if (NOT WAMR_BUILD_TARGET)
    message(FATAL_ERROR "WAMR_BUILD_TARGET not defined")
endif()
if (WAMR_BUILD_TARGET MATCHES "ARM")
    # Forced to be armv5te for AOT
    set(WAMR_BUILD_TARGET ARMV5TE)
endif()
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 1)
set(WAMR_BUILD_AOT 1)
set(WAMR_DISABLE_HW_BOUND_CHECK 1)      # Don't mmap large memory
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_UVWASI 1)
set(WAMR_BUILD_LIB_PTHREAD 1)
set(WAMR_BUILD_DUMP_CALL_STACK 1)
set(WAMR_BUILD_CUSTOM_NAME_SECTION 1)
set(WAMR_BUILD_MULTI_MODULE 1)
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
add_library(wamr STATIC ${WAMR_RUNTIME_LIB_SOURCE})
target_link_libraries(wamr uv_a)
if (ANDROID)
    target_link_libraries(wamr log)
    add_library(android-spawn STATIC
            ${WAMR_EXT_ROOT_DIR}/third_party/libandroid-spawn/posix_spawn.cpp)
endif()

set(WAMR_EXT_INCLUDE_DIRS
        ${WAMR_EXT_ROOT_DIR}/include
        ${WAMR_ROOT_DIR}/core/iwasm/libraries
        ${WAMR_ROOT_DIR}/core/iwasm
        ${uvwasi_SOURCE_DIR}/src
        ${WAMR_EXT_ROOT_DIR}/third_party
        )

include(${WAMR_EXT_ROOT_DIR}/version.cmake)
message(STATUS "wamr-ext version: ${WAMR_EXT_VERSION_STRING}(${WAMR_EXT_VERSION_CODE})")
configure_file(${WAMR_EXT_ROOT_DIR}/src/wamr_ext_api/wamr_ext_version.cpp.in
        ${CMAKE_CURRENT_BINARY_DIR}/wamr_ext_version.cpp
        )
add_library(wamr_ext_obj OBJECT
        ${CMAKE_CURRENT_BINARY_DIR}/wamr_ext_version.cpp
        ${WAMR_EXT_ROOT_DIR}/src/base/Utility.cpp
        ${WAMR_EXT_ROOT_DIR}/src/base/FSUtility.cpp
        ${WAMR_EXT_ROOT_DIR}/src/base/LoopThread.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WamrExtInternalDef.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WasiPthreadExt.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WasiWamrExt.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WasiFSExt.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WasiSocketExt.cpp
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_lib/WasiProcessExt.cpp
        )
target_include_directories(wamr_ext_obj PRIVATE ${WAMR_EXT_INCLUDE_DIRS})

set(WAMR_EXT_DEP_LIBS wamr)
if (ANDROID)
    list(APPEND WAMR_EXT_DEP_LIBS android-spawn)
endif()
add_library(wamr_ext_static STATIC
        $<TARGET_OBJECTS:wamr_ext_obj>
        ${WAMR_EXT_ROOT_DIR}/src/wamr_ext_api/wamr_ext_api.cpp
        )
target_include_directories(wamr_ext_static PRIVATE include ${WAMR_EXT_INCLUDE_DIRS})
target_compile_definitions(wamr_ext_static PRIVATE -DWAMR_EXT_STATIC_LIB)
target_link_libraries(wamr_ext_static PRIVATE ${WAMR_EXT_DEP_LIBS})
