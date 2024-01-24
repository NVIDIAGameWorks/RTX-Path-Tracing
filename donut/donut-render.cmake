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


file(GLOB donut_render_src
    LIST_DIRECTORIES false
    include/donut/render/*.h
    src/render/*.cpp
)

add_library(donut_render STATIC EXCLUDE_FROM_ALL ${donut_render_src})
target_include_directories(donut_render PUBLIC include)
target_link_libraries(donut_render donut_core donut_engine)

add_dependencies(donut_render donut_shaders)

if(DONUT_WITH_DX11)
target_compile_definitions(donut_render PUBLIC USE_DX11=1)
endif()

if(DONUT_WITH_DX12)
target_compile_definitions(donut_render PUBLIC USE_DX12=1)
endif()

if(DONUT_WITH_VULKAN)
target_compile_definitions(donut_render PUBLIC USE_VK=1)
endif()

set_target_properties(donut_render PROPERTIES FOLDER Donut)
