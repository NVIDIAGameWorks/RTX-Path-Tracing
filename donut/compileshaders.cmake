#
# Copyright (c) 2014-2020, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.


# generates a build target that will compile shaders for a given config file
#
# usage: donut_compile_shaders(TARGET <generated build target name>
#                              CONFIG <shader-config-file>
#                              [DXIL <dxil-output-path>]
#                              [DXBC <dxbc-output-path>]
#                              [SPIRV_DXC <spirv-output-path>])

function(donut_compile_shaders)
    set(options "")
    set(oneValueArgs TARGET CONFIG FOLDER DXIL DXBC SPIRV_DXC COMPILER_OPTIONS_DXIL COMPILER_OPTIONS_DXBC COMPILER_OPTIONS_SPIRV)
    set(multiValueArgs SOURCES INCLUDE)
    cmake_parse_arguments(params "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT params_TARGET)
        message(FATAL_ERROR "donut_compile_shaders: TARGET argument missing")
    endif()
    if (NOT params_CONFIG)
        message(FATAL_ERROR "donut_compile_shaders: CONFIG argument missing")
    endif()

    # just add the source files to the project as documents, they are built by the script
    set_source_files_properties(${params_SOURCES} PROPERTIES VS_TOOL_OVERRIDE "None") 

    add_custom_target(${params_TARGET}
        DEPENDS ShaderMake
        SOURCES ${params_SOURCES})

	set(INCLUDE_PATH "")
	foreach(path ${params_INCLUDE})
		list(APPEND INCLUDE_PATH -I ${path})
	endforeach()

    if (WIN32)
        set(useApiArgument --useAPI)
    else()
        set(useApiArgument "")
    endif()

    if (params_DXIL AND DONUT_WITH_DX12)
        if (NOT DXC_PATH)
            message(FATAL_ERROR "donut_compile_shaders: DXC not found --- please set DXC_PATH to the full path to the DXC binary")
        endif()
        
        set(compilerCommand ShaderMake
           --config ${params_CONFIG}
           --out ${params_DXIL}
           --platform DXIL
           --binaryBlob
           -I ${DONUT_SHADER_INCLUDE_DIR}
           ${INCLUDE_PATH}
           --compiler "${DXC_PATH}"
           --outputExt .bin
           --shaderModel 6_5
           --embedPDB
           --stripReflection
           ${useApiArgument})

        separate_arguments(params_COMPILER_OPTIONS_DXIL NATIVE_COMMAND "${params_COMPILER_OPTIONS_DXIL}")

        list(APPEND compilerCommand ${params_COMPILER_OPTIONS_DXIL})
        
        add_custom_command(TARGET ${params_TARGET} PRE_BUILD COMMAND ${compilerCommand})
    endif()

    if (params_DXBC AND DONUT_WITH_DX11)
        if (NOT FXC_PATH)
            message(FATAL_ERROR "donut_compile_shaders: FXC not found --- please set FXC_PATH to the full path to the FXC binary")
        endif()
        
        set(compilerCommand ShaderMake
           --config ${params_CONFIG}
           --out ${params_DXBC}
           --platform DXBC
           --binaryBlob
           -I ${DONUT_SHADER_INCLUDE_DIR}
           ${INCLUDE_PATH}
           --compiler "${FXC_PATH}"
           --outputExt .bin
           ${useApiArgument})

        separate_arguments(params_COMPILER_OPTIONS_DXBC NATIVE_COMMAND "${params_COMPILER_OPTIONS_DXBC}")

        list(APPEND compilerCommand ${params_COMPILER_OPTIONS_DXBC})
        
        add_custom_command(TARGET ${params_TARGET} PRE_BUILD COMMAND ${compilerCommand})
    endif()

    if (params_SPIRV_DXC AND DONUT_WITH_VULKAN)
        if (NOT DXC_SPIRV_PATH)
            message(FATAL_ERROR "donut_compile_shaders: DXC for SPIR-V not found --- please set DXC_SPIRV_PATH to the full path to the DXC binary")
        endif()
        
        set(compilerCommand ShaderMake
           --config ${params_CONFIG}
           --out ${params_SPIRV_DXC}
           --platform SPIRV
           --binaryBlob
           -I ${DONUT_SHADER_INCLUDE_DIR}
           ${INCLUDE_PATH}
           -D SPIRV
           --compiler "${DXC_SPIRV_PATH}"
           --tRegShift 0
           --sRegShift 128
           --bRegShift 256
           --uRegShift 384
           --vulkanVersion 1.2
           --outputExt .bin
           --shaderModel 6_5
           ${useApiArgument})

        separate_arguments(params_COMPILER_OPTIONS_SPIRV NATIVE_COMMAND "${params_COMPILER_OPTIONS_SPIRV}")

        list(APPEND compilerCommand ${params_COMPILER_OPTIONS_SPIRV})

        add_custom_command(TARGET ${params_TARGET} PRE_BUILD COMMAND ${compilerCommand})
    endif()

    if(params_FOLDER)
        set_target_properties(${params_TARGET} PROPERTIES FOLDER ${params_FOLDER})
    endif()
endfunction()

# Generates a build target that will compile shaders for a given config file for all enabled Donut platforms.
#
# The shaders will be placed into subdirectories of ${OUTPUT_BASE}, with names compatible with
# the FindDirectoryWithShaderBin framework function.

function(donut_compile_shaders_all_platforms)
    set(options "")
    set(oneValueArgs TARGET CONFIG FOLDER OUTPUT_BASE COMPILER_OPTIONS_DXIL COMPILER_OPTIONS_DXBC COMPILER_OPTIONS_SPIRV)
    set(multiValueArgs SOURCES)
    cmake_parse_arguments(params "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT params_TARGET)
        message(FATAL_ERROR "donut_compile_shaders_all_platforms: TARGET argument missing")
    endif()
    if (NOT params_CONFIG)
        message(FATAL_ERROR "donut_compile_shaders_all_platforms: CONFIG argument missing")
    endif()
    if (NOT params_OUTPUT_BASE)
        message(FATAL_ERROR "donut_compile_shaders_all_platforms: OUTPUT_BASE argument missing")
    endif()

    donut_compile_shaders(TARGET ${params_TARGET}
                          CONFIG ${params_CONFIG}
                          FOLDER ${params_FOLDER}
                          DXBC ${params_OUTPUT_BASE}/dxbc
                          DXIL ${params_OUTPUT_BASE}/dxil
                          SPIRV_DXC ${params_OUTPUT_BASE}/spirv
                          COMPILER_OPTIONS_DXIL ${params_COMPILER_OPTIONS_DXIL}
                          COMPILER_OPTIONS_DXBC ${params_COMPILER_OPTIONS_DXBC}
                          COMPILER_OPTIONS_SPIRV ${params_COMPILER_OPTIONS_SPIRV}
                          SOURCES ${params_SOURCES})

endfunction()
