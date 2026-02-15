# LLVM.cmake - Find and configure LLVM

# Try common LLVM installation paths
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH
        "C:/LLVM"
        "C:/Program Files/LLVM"
        "$ENV{LLVM_DIR}"
    )
elseif(APPLE)
    list(APPEND CMAKE_PREFIX_PATH
        "/opt/homebrew/opt/llvm"
        "/usr/local/opt/llvm"
        "$ENV{LLVM_DIR}"
    )
else()
    list(APPEND CMAKE_PREFIX_PATH
        "/usr/lib/llvm-21"
        "/usr/lib/llvm-18"
        "/usr/lib/llvm-17"
        "/usr/local"
        "$ENV{LLVM_DIR}"
    )
endif()

# Suppress "Could NOT find LibXml2" warning from LLVM's config
# (LLVM was built with LLVM_ENABLE_LIBXML2=1 but we don't need it)
set(CMAKE_DISABLE_FIND_PACKAGE_LibXml2 TRUE)
find_package(LLVM CONFIG)
unset(CMAKE_DISABLE_FIND_PACKAGE_LibXml2)

if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

    include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
    separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
    add_definitions(${LLVM_DEFINITIONS_LIST})

    # Fix DIA SDK path if LLVM was built with a different VS version (Windows only)
    if(WIN32 AND TARGET LLVMDebugInfoPDB)
        get_target_property(_pdb_libs LLVMDebugInfoPDB INTERFACE_LINK_LIBRARIES)
        if(_pdb_libs)
            # Find the actual DIA SDK on this system
            set(_dia_found FALSE)
            foreach(_vs_path
                "C:/Program Files/Microsoft Visual Studio/2022/Enterprise"
                "C:/Program Files/Microsoft Visual Studio/2022/Professional"
                "C:/Program Files/Microsoft Visual Studio/2022/Community"
                "C:/Program Files (x86)/Microsoft Visual Studio/2019/Enterprise"
                "C:/Program Files (x86)/Microsoft Visual Studio/2019/Professional"
                "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community")
                if(EXISTS "${_vs_path}/DIA SDK/lib/amd64/diaguids.lib")
                    set(_dia_lib "${_vs_path}/DIA SDK/lib/amd64/diaguids.lib")
                    set(_dia_found TRUE)
                    break()
                endif()
            endforeach()
            if(_dia_found)
                string(REGEX REPLACE "[^;]*/diaguids\\.lib" "${_dia_lib}" _pdb_libs_fixed "${_pdb_libs}")
                if(NOT "${_pdb_libs}" STREQUAL "${_pdb_libs_fixed}")
                    set_target_properties(LLVMDebugInfoPDB PROPERTIES
                        INTERFACE_LINK_LIBRARIES "${_pdb_libs_fixed}")
                    message(STATUS "Fixed DIA SDK path: ${_dia_lib}")
                endif()
            endif()
        endif()
    endif()

    # Map LLVM components to libraries
    # Use AllTargets* meta-components for cross-compilation support
    set(_LIVA_LLVM_COMPONENTS
        Core
        Support
        IRReader
        AllTargetsCodeGens
        AllTargetsAsmParsers
        AllTargetsDescs
        AllTargetsInfos
        Passes
        Coroutines
        MC
        Analysis
        TransformUtils
        ScalarOpts
        InstCombine
        CodeGen
        AsmPrinter
        ObjCARCOpts
        OrcJIT
        OrcShared
        OrcTargetProcess
        JITLink
        ExecutionEngine
        RuntimeDyld
    )
    llvm_map_components_to_libnames(LIVA_LLVM_LIBS ${_LIVA_LLVM_COMPONENTS})

    set(LIVA_HAS_LLVM TRUE)
else()
    message(WARNING "LLVM not found. IR generation and code generation will be disabled.")
    message(WARNING "Set LLVM_DIR or install LLVM to enable full compilation.")
    set(LIVA_HAS_LLVM FALSE)
    set(LIVA_LLVM_LIBS "")
endif()
