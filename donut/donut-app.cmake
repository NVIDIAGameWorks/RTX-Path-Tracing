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


file(GLOB donut_app_src
    LIST_DIRECTORIES false
    include/donut/app/*.h
    src/app/*.cpp
)

file(GLOB donut_app_vr_src
    include/donut/app/vr/*.h
    src/app/vr/*.cpp
)

add_library(donut_app STATIC EXCLUDE_FROM_ALL ${donut_app_src})
target_include_directories(donut_app PUBLIC include)
target_link_libraries(donut_app donut_core donut_engine glfw imgui)

if(DONUT_WITH_DX11)
target_sources(donut_app PRIVATE src/app/dx11/DeviceManager_DX11.cpp)
target_compile_definitions(donut_app PUBLIC USE_DX11=1)
target_link_libraries(donut_app nvrhi_d3d11 d3d12 dxgi)
endif()

if(DONUT_WITH_DX12)
target_sources(donut_app PRIVATE src/app/dx12/DeviceManager_DX12.cpp)
target_compile_definitions(donut_app PUBLIC USE_DX12=1)
target_link_libraries(donut_app nvrhi_d3d12 d3d11 dxgi)
endif()

if(DONUT_USE_DXIL_ON_DX12)
target_compile_definitions(donut_app PRIVATE DONUT_USE_DXIL_ON_DX12=1)
endif()

if(DONUT_WITH_VULKAN)
target_sources(donut_app PRIVATE src/app/vulkan/DeviceManager_VK.cpp)
target_compile_definitions(donut_app PUBLIC USE_VK=1)
target_link_libraries(donut_app nvrhi_vk)
endif()

target_link_libraries(donut_app nvrhi) # needs to come after nvrhi_d3d11 etc. for link order

if(NVRHI_WITH_SHADER_COMPILER)
add_dependencies(donut_app donut_shaders)
endif()

set_target_properties(donut_app PROPERTIES FOLDER Donut)
