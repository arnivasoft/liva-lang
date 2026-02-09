# LLVM.cmake - Find and configure LLVM

# Try common LLVM installation paths on Windows
if(WIN32)
    list(APPEND CMAKE_PREFIX_PATH
        "C:/LLVM"
        "C:/Program Files/LLVM"
        "$ENV{LLVM_DIR}"
    )
endif()

find_package(LLVM CONFIG)

if(LLVM_FOUND)
    message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
    message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

    include_directories(${LLVM_INCLUDE_DIRS})
    separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
    add_definitions(${LLVM_DEFINITIONS_LIST})

    # Fix DIA SDK path if LLVM was built with a different VS version
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
    # Use 'native' to automatically resolve the host target (e.g. X86 on Windows x64)
    llvm_map_components_to_libnames(LIVA_LLVM_LIBS
        Core
        Support
        IRReader
        nativecodegen
        X86AsmParser
        Passes
        MC
        Analysis
        TransformUtils
        ScalarOpts
        InstCombine
        CodeGen
        AsmPrinter
        ObjCARCOpts
    )

    set(LIVA_HAS_LLVM TRUE)
else()
    message(WARNING "LLVM not found. IR generation and code generation will be disabled.")
    message(WARNING "Set LLVM_DIR or install LLVM to enable full compilation.")
    set(LIVA_HAS_LLVM FALSE)
    set(LIVA_LLVM_LIBS "")
endif()
