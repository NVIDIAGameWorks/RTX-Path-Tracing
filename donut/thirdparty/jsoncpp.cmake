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


set(JSONCPP_WITH_TESTS OFF CACHE BOOL "")
set(JSONCPP_WITH_POST_BUILD_UNITTEST OFF CACHE BOOL "")
set(JSONCPP_WITH_PKGCONFIG_SUPPORT OFF CACHE BOOL "")
set(JSONCPP_WITH_CMAKE_PACKAGE OFF CACHE BOOL "")

set(__tmp_shared_libs ${BUILD_SHARED_LIBS})
set(__tmp_static_libs ${BUILD_STATIC_LIBS})
set(__tmp_object_libs ${BUILD_OBJECT_LIBS})

# Save the paths because jsoncpp overrides them.
set(__tmp_archive_output_dir ${CMAKE_ARCHIVE_OUTPUT_DIRECTORY})
set(__tmp_library_output_dir ${CMAKE_LIBRARY_OUTPUT_DIRECTORY})
set(__tmp_pdb_output_dir ${CMAKE_PDB_OUTPUT_DIRECTORY})
set(__tmp_runtime_output_dir ${CMAKE_RUNTIME_OUTPUT_DIRECTORY})

set(BUILD_SHARED_LIBS OFF)
set(BUILD_STATIC_LIBS ON)
set(BUILD_OBJECT_LIBS OFF)

add_subdirectory(jsoncpp)

set(BUILD_SHARED_LIBS ${__tmp_shared_libs})
set(BUILD_STATIC_LIBS ${__tmp_static_libs})
set(BUILD_OBJECT_LIBS ${__tmp_object_libs})

# Restore the paths.
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${__tmp_archive_output_dir} CACHE STRING "" FORCE)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${__tmp_library_output_dir} CACHE STRING "" FORCE)
set(CMAKE_PDB_OUTPUT_DIRECTORY ${__tmp_pdb_output_dir} CACHE STRING "" FORCE)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${__tmp_runtime_output_dir} CACHE STRING "" FORCE)
