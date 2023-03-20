#
# Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
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


# Required dependencies
set(third_party_folder "Third-Party Libraries")

set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)

if (DONUT_WITH_NVRHI AND NOT TARGET imgui)
    include(imgui.cmake)
    set_target_properties(imgui PROPERTIES FOLDER ${third_party_folder})
endif()

if (NOT TARGET jsoncpp_static)
    include(jsoncpp.cmake)
    set_target_properties(jsoncpp_static PROPERTIES FOLDER ${third_party_folder})
endif()

if (NOT TARGET stb)
    add_library(stb INTERFACE)
    target_include_directories(stb INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/stb)
endif()

if (NOT TARGET cgltf)
    add_library(cgltf INTERFACE)
    target_include_directories(cgltf INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/cgltf)
endif()

# Optional dependencies

if (DONUT_WITH_TINYEXR AND NOT TARGET tinyexr)
    add_library(tinyexr INTERFACE)
    target_include_directories(tinyexr INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/tinyexr)
endif()

if (DONUT_WITH_TASKFLOW AND NOT TARGET taskflow)
    add_library(taskflow INTERFACE)
    target_include_directories(taskflow INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/taskflow)
endif()

if (DONUT_WITH_NVRHI AND NOT TARGET glfw)
    option(GLFW_BUILD_EXAMPLES "" OFF)
    option(GLFW_BUILD_TESTS "" OFF)
    option(GLFW_BUILD_DOCS "" OFF)
    option(GLFW_INSTALL "" OFF)
    add_subdirectory(glfw)
    set_target_properties(glfw PROPERTIES FOLDER ${third_party_folder})
endif()

if (DONUT_WITH_LZ4 AND NOT TARGET lz4)
    include(lz4.cmake)
    set_target_properties(lz4 PROPERTIES FOLDER ${third_party_folder})
endif()

if (DONUT_WITH_MINIZ AND NOT TARGET miniz)
    add_subdirectory(miniz)
    set_target_properties(miniz PROPERTIES FOLDER ${third_party_folder})
endif()
