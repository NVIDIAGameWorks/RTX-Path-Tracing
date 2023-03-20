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


# build small library of common untilities for tests

add_library(donut_tests_utils STATIC src/utils.cpp)
target_include_directories(donut_tests_utils PUBLIC "include")
set_property(TARGET donut_tests_utils PROPERTY FOLDER "Donut/donut_tests")

# XXXX mk : CTest does not create (yet?) a default build target for all tests
add_custom_target(donut_all_tests)
set_property(TARGET donut_all_tests PROPERTY FOLDER "Donut/donut_tests")

#
# Add tests for each donut module
#

add_definitions(-DDONUT_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
add_definitions(-DDONUT_TEST_BINARY_DIR="${CMAKE_CURRENT_BINARY_DIR}")

include(test-core.cmake)

if (DONUT_WITH_NVRHI) 
    include(test-engine.cmake)
endif()
