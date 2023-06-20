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
    set(oneValueArgs TARGET CONFIG FOLDER DXIL DXBC SPIRV_DXC CFLAGS)
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
        DEPENDS shaderCompiler
        SOURCES ${params_SOURCES})

	set(INCLUDE_PATH "")
	foreach(path ${params_INCLUDE})
		list(APPEND INCLUDE_PATH -I ${path})
	endforeach()

    if (params_DXIL AND (DONUT_WITH_DX12 AND DONUT_USE_DXIL_ON_DX12))
        if (NOT DXC_DXIL_EXECUTABLE)
            message(FATAL_ERROR "donut_compile_shaders: DXC not found --- please set DXC_DXIL_EXECUTABLE to the full path to the DXC binary")
        endif()

        if (NOT params_CFLAGS)
            set(CFLAGS "-Zi -Qembed_debug -O3 -WX")
        else()
            set(CFLAGS ${params_CFLAGS})
        endif()

        add_custom_command(TARGET ${params_TARGET} PRE_BUILD
                          COMMAND shaderCompiler
                                   --infile ${params_CONFIG}
                                   --parallel
                                   --out ${params_DXIL}
                                   --platform dxil
                                   --cflags "${CFLAGS}"
                                   -I ${DONUT_SHADER_INCLUDE_DIR}
								   ${INCLUDE_PATH}
                                   --compiler ${DXC_DXIL_EXECUTABLE})
    endif()

    if (params_DXBC AND (DONUT_WITH_DX11 OR (DONUT_WITH_DX12 AND NOT DONUT_USE_DXIL_ON_DX12)))
        if (NOT FXC_EXECUTABLE)
            message(FATAL_ERROR "donut_compile_shaders: FXC not found --- please set FXC_EXECUTABLE to the full path to the FXC binary")
        endif()

        if (NOT params_CFLAGS)
            set(CFLAGS "$<IF:$<CONFIG:Debug>,-Zi,-Qstrip_priv -Qstrip_debug -Qstrip_reflect> -O3 -WX")
        else()
            set(CFLAGS ${params_CFLAGS})
        endif()

        add_custom_command(TARGET ${params_TARGET} PRE_BUILD
                          COMMAND shaderCompiler
                                   --infile ${params_CONFIG}
                                   --parallel
                                   --out ${params_DXBC}
                                   --platform dxbc
                                   --cflags "${CFLAGS}"
                                   -I ${DONUT_SHADER_INCLUDE_DIR}
                                   ${INCLUDE_PATH}
                                   --compiler ${FXC_EXECUTABLE})
    endif()

    if (params_SPIRV_DXC AND DONUT_WITH_VULKAN)
        if (NOT DXC_SPIRV_EXECUTABLE)
            message(FATAL_ERROR "donut_compile_shaders: DXC for SPIR-V not found --- please set DXC_SPIRV_EXECUTABLE to the full path to the DXC binary")
        endif()

        if (NOT params_CFLAGS)
            set(CFLAGS "$<IF:$<CONFIG:Debug>,-Zi,> -fspv-target-env=vulkan1.2 -O3 -WX")
        else()
            set(CFLAGS ${params_CFLAGS})
        endif()

        add_custom_command(TARGET ${params_TARGET} PRE_BUILD
                          COMMAND shaderCompiler
                                   --infile ${params_CONFIG}
                                   --parallel
                                   --out ${params_SPIRV_DXC}
                                   --platform spirv
                                   -I ${DONUT_SHADER_INCLUDE_DIR}
                                   ${INCLUDE_PATH}
                                   -D SPIRV
                                   --cflags "${CFLAGS}"
                                   --compiler ${DXC_SPIRV_EXECUTABLE})
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
    set(oneValueArgs TARGET CONFIG FOLDER OUTPUT_BASE CFLAGS)
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
                          CFLAGS ${params_CFLAGS}
                          DXBC ${params_OUTPUT_BASE}/dxbc
                          DXIL ${params_OUTPUT_BASE}/dxil
                          SPIRV_DXC ${params_OUTPUT_BASE}/spirv
                          SOURCES ${params_SOURCES}
                          INCLUDE ${params_INCLUDE}
    )

endfunction()
