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


file(GLOB donut_core_src
    include/donut/core/chunk/*.h
    include/donut/core/math/*.h
    include/donut/core/vfs/Compression.h
    include/donut/core/vfs/TarFile.h
    include/donut/core/vfs/VFS.h
    include/donut/core/*.h
    src/core/chunk/*.cpp
    src/core/math/*.cpp
    src/core/vfs/Compression.cpp
    src/core/vfs/TarFile.cpp
    src/core/vfs/VFS.cpp
    src/core/*.cpp
)

add_library(donut_core STATIC EXCLUDE_FROM_ALL ${donut_core_src})
target_include_directories(donut_core PUBLIC include)
target_link_libraries(donut_core jsoncpp_static)

if(NOT WIN32)
    target_link_libraries(donut_core stdc++fs dl pthread)
endif()

if(WIN32)
    target_sources(donut_core PRIVATE
        include/donut/core/vfs/WinResFS.h
        src/core/vfs/WinResFS.cpp
    )
    target_compile_definitions(donut_core PUBLIC NOMINMAX _CRT_SECURE_NO_WARNINGS)
endif()

if(DONUT_WITH_LZ4)
    target_link_libraries(donut_core lz4)
    target_compile_definitions(donut_core PUBLIC DONUT_WITH_LZ4)
endif()

if(DONUT_WITH_MINIZ)
    target_link_libraries(donut_core miniz)
    target_sources(donut_core PRIVATE
        include/donut/core/vfs/ZipFile.h
        src/core/vfs/ZipFile.cpp
    )
    target_compile_definitions(donut_core PUBLIC DONUT_WITH_MINIZ)
endif()

set_target_properties(donut_core PROPERTIES FOLDER Donut)
