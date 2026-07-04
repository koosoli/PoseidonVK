# FindLLVMSanitizer.cmake
# Dynamically locate LLVM installation, compilers, and sanitizer runtime libraries
# Supports Windows with LLVM installed in Program Files, and Linux

function(find_llvm_root OUTPUT_VAR ARCH)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        # On Linux, clang is on PATH; no separate LLVM root needed
        set(${OUTPUT_VAR} "" PARENT_SCOPE)
        return()
    endif()
    if(DEFINED ENV{LLVM_ROOT})
        set(LLVM_DIR "$ENV{LLVM_ROOT}")
    elseif(DEFINED LLVM_ROOT)
        set(LLVM_DIR "${LLVM_ROOT}")
    elseif(ARCH STREQUAL "x86")
        set(LLVM_DIR "C:/Program Files (x86)/LLVM")
    elseif(ARCH STREQUAL "x64")
        set(LLVM_DIR "C:/Program Files/LLVM")
    else()
        message(FATAL_ERROR "Unsupported architecture: ${ARCH}")
    endif()

    # Fallback search: check common non-standard install locations
    if(NOT EXISTS "${LLVM_DIR}")
        foreach(_candidate
            "D:/Program Files/LLVM"
            "D:/LLVM"
            "C:/LLVM"
        )
            if(EXISTS "${_candidate}")
                set(LLVM_DIR "${_candidate}")
                break()
            endif()
        endforeach()
    endif()

    if(EXISTS "${LLVM_DIR}")
        set(${OUTPUT_VAR} "${LLVM_DIR}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR "LLVM not found. Set LLVM_ROOT env variable or CMake variable to your LLVM install directory.")
    endif()
endfunction()

function(find_llvm_compilers ARCH)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        find_program(CLANG_C   NAMES clang   REQUIRED)
        find_program(CLANG_CXX NAMES clang++ REQUIRED)
        set(CMAKE_C_COMPILER   "${CLANG_C}"   CACHE FILEPATH "C compiler"   FORCE)
        set(CMAKE_CXX_COMPILER "${CLANG_CXX}" CACHE FILEPATH "C++ compiler" FORCE)
        message(STATUS "Found LLVM compilers (Linux): ${CLANG_C}")
        return()
    endif()
    find_llvm_root(LLVM_ROOT ${ARCH})
    
    set(CLANG_CL "${LLVM_ROOT}/bin/clang-cl.exe")
    set(LLVM_RC "${LLVM_ROOT}/bin/llvm-rc.exe")
    
    if(EXISTS "${CLANG_CL}")
        set(CMAKE_C_COMPILER "${CLANG_CL}" CACHE FILEPATH "C compiler" FORCE)
        set(CMAKE_CXX_COMPILER "${CLANG_CL}" CACHE FILEPATH "C++ compiler" FORCE)
        set(CMAKE_RC_COMPILER "${LLVM_RC}" CACHE FILEPATH "Resource compiler" FORCE)
        message(STATUS "Found LLVM compilers (${ARCH}): ${CLANG_CL}")
    else()
        message(FATAL_ERROR "clang-cl.exe not found at ${CLANG_CL}")
    endif()
endfunction()

function(find_asan_runtime OUTPUT_VAR ARCH)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(${OUTPUT_VAR} "" PARENT_SCOPE)
        return()
    endif()
    find_llvm_root(LLVM_DIR ${ARCH})
    
    if(ARCH STREQUAL "x86")
        set(RT_NAME "clang_rt.asan_dynamic-i386.dll")
    else()
        set(RT_NAME "clang_rt.asan_dynamic-x86_64.dll")
    endif()
    
    # Search for the runtime in LLVM lib directory (any version)
    file(GLOB_RECURSE ASAN_DLL 
        "${LLVM_DIR}/lib/clang/*/lib/windows/${RT_NAME}"
    )
    
    if(ASAN_DLL)
        list(GET ASAN_DLL 0 ASAN_DLL_PATH)  # Take first match (latest version)
        set(${OUTPUT_VAR} "${ASAN_DLL_PATH}" PARENT_SCOPE)
        message(STATUS "Found ASan runtime (${ARCH}): ${ASAN_DLL_PATH}")
    else()
        message(WARNING "ASan runtime ${RT_NAME} not found in ${LLVM_DIR}")
        set(${OUTPUT_VAR} "" PARENT_SCOPE)
    endif()
endfunction()

function(find_asan_import_lib OUTPUT_VAR ARCH)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(${OUTPUT_VAR} "" PARENT_SCOPE)
        return()
    endif()
    find_llvm_root(LLVM_DIR ${ARCH})
    
    if(ARCH STREQUAL "x86")
        set(RT_THUNK "clang_rt.asan_dynamic_runtime_thunk-i386.lib")
        set(RT_IMPLIB "clang_rt.asan_dynamic-i386.lib")
    else()
        set(RT_THUNK "clang_rt.asan_dynamic_runtime_thunk-x86_64.lib")
        set(RT_IMPLIB "clang_rt.asan_dynamic-x86_64.lib")
    endif()
    
    # Search for LLVM lib directory
    file(GLOB LLVM_LIB_DIRS "${LLVM_DIR}/lib/clang/*/lib/windows")
    
    if(LLVM_LIB_DIRS)
        list(GET LLVM_LIB_DIRS 0 LLVM_LIB_DIR)  # Take first match
        set(${OUTPUT_VAR} "${LLVM_LIB_DIR}" PARENT_SCOPE)
        message(STATUS "Found LLVM lib directory (${ARCH}): ${LLVM_LIB_DIR}")
    else()
        message(WARNING "LLVM lib directory not found in ${LLVM_DIR}")
        set(${OUTPUT_VAR} "" PARENT_SCOPE)
    endif()
endfunction()
