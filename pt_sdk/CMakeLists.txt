# Copyright (c) 2023, NVIDIA CORPORATION.  All rights reserved.
#
# NVIDIA CORPORATION and its licensors retain all intellectual property
# and proprietary rights in and to this software, related documentation
# and any modifications thereto.  Any use, reproduction, disclosure or
# distribution of this software and related documentation without an express
# license agreement from NVIDIA CORPORATION is strictly prohibited.


include(../donut/compileshaders.cmake)
file(GLOB_RECURSE shaders "*.hlsl" "*.hlsli")
file(GLOB_RECURSE sources "*.cpp" "*.h" "*.md")

file(GLOB NRD_INTEGRATION_HEADERS "../external/RayTracingDenoiser/Integration/*.h" "../external/RayTracingDenoiser/Integration/*.hpp")
source_group("RayTracingDenoiser" FILES ${NRD_INTEGRATION_HEADERS})

set(project pt_sdk)
set(folder "!Path Tracing SDK")

set(pt_sdk_shaders ${project}_shaders)

donut_compile_shaders(
    TARGET pt_sdk_shaders
    CONFIG ${CMAKE_CURRENT_SOURCE_DIR}/shaders.cfg
    SOURCES ${shaders}
    FOLDER ${folder}
    DXIL ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${project}/dxil
    SPIRV_DXC ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders/${project}/spirv
    INCLUDE ${CMAKE_SOURCE_DIR}/external/RayTracingDenoiser/Shaders/Include ${CMAKE_SOURCE_DIR}/external/RTXDI/rtxdi-sdk/include
)

add_executable(${project} WIN32 ${sources})

if(NVRHI_WITH_SHADER_COMPILER)
add_dependencies(${project} pt_sdk_shaders nrd_shaders omm_sdk_shaders)
endif()

if(STREAMLINE_INTEGRATION)
set(SL_DLLS
    ${CMAKE_SOURCE_DIR}/external/Streamline/bin/x64/nvngx_dlss.dll
    ${CMAKE_SOURCE_DIR}/external/Streamline/bin/x64/nvngx_dlssg.dll
    ${CMAKE_SOURCE_DIR}/external/Streamline/bin/x64/sl.common.dll
    ${CMAKE_SOURCE_DIR}/external/Streamline/bin/x64/sl.dlss.dll
    ${CMAKE_SOURCE_DIR}/external/Streamline/bin/x64/sl.dlss_g.dll
    ${CMAKE_SOURCE_DIR}/external/Streamline/bin/x64/sl.interposer.dll
    ${CMAKE_SOURCE_DIR}/external/Streamline/bin/x64/sl.nis.dll
    ${CMAKE_SOURCE_DIR}/external/Streamline/bin/x64/sl.reflex.dll
)
add_compile_definitions(STREAMLINE_INTEGRATION)
add_library(sl.interposer STATIC IMPORTED)
set_property(TARGET sl.interposer PROPERTY IMPORTED_LOCATION ../../external/Streamline/lib/x64/sl.interposer.lib)
file(COPY ${SL_DLLS} DESTINATION ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})
target_link_libraries(${project} donut_render donut_app donut_engine NRD rtxdi-sdk omm-sdk omm-sdk-nvrhi sl.interposer)
else()
target_link_libraries(${project} donut_render donut_app donut_engine NRD rtxdi-sdk omm-sdk omm-sdk-nvrhi)
endif()

set_target_properties(${project} PROPERTIES FOLDER ${folder})

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${shaders})

target_include_directories(${project} PRIVATE "../external/RayTracingDenoiser/Include" "../external/Streamline/include")