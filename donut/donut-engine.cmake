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


file(GLOB donut_engine_src
    include/donut/engine/*.h
    src/engine/*.cpp
    src/engine/*.c
    src/engine/*.h
)

if (MSVC)
    list (APPEND donut_engine_src donut.natvis)
endif()

add_library(donut_engine STATIC EXCLUDE_FROM_ALL ${donut_engine_src})
target_include_directories(donut_engine PUBLIC include)

target_link_libraries(donut_engine donut_core nvrhi jsoncpp_static stb tinyexr cgltf)

if (DONUT_WITH_TASKFLOW)
    target_link_libraries(donut_engine taskflow)
    target_compile_definitions(donut_engine PUBLIC DONUT_WITH_TASKFLOW)
endif()

if(WIN32)
    target_compile_definitions(donut_engine PUBLIC NOMINMAX)
endif()

if (DONUT_WITH_AUDIO)
    target_link_libraries(donut_engine Xaudio2)
    target_compile_definitions(donut_engine PUBLIC DONUT_WITH_AUDIO)
endif()

if (DONUT_WITH_TINYEXR)
    target_link_libraries(donut_engine tinyexr)
    target_compile_definitions(donut_engine PUBLIC DONUT_WITH_TINYEXR)
endif()

set_target_properties(donut_engine PROPERTIES FOLDER Donut)
