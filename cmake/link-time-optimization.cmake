#
# Copyright (C) 2021 by George Cave - gcave@stablecoder.ca
#
# Licensed under the Apache License, Version 2.0 (the "License"); you may not
# use this file except in compliance with the License. You may obtain a copy of
# the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
# WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
# License for the specific language governing permissions and limitations under
# the License.

include(CheckIPOSupported)

# Checks for, and enables IPO/LTO for all following targets
#
# Running with GCC seems to have no effect
# ~~~
# Optional:
# REQUIRED - If this is passed in, CMake configuration will fail with an error if LTO/IPO is not supported
# ~~~
macro(link_time_optimization)
  # Argument parsing
  set(options REQUIRED)
  set(single_value_keywords)
  set(multi_value_keywords)
  cmake_parse_arguments(
    link_time_optimization "${options}" "${single_value_keywords}"
    "${multi_value_keywords}" ${ARGN})

  check_ipo_supported(RESULT result OUTPUT output)
  if(result)
    # It's available, set it for all following items
    set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ON)
  else()
    if(link_time_optimization_REQUIRED)
      message(
        FATAL_ERROR
          "Link Time Optimization not supported, but listed as REQUIRED: ${output}"
      )
    else()
      message(WARNING "Link Time Optimization not supported: ${output}")
    endif()
  endif()
endmacro()

# Checks for, and enables IPO/LTO for the specified target
#
# Running with GCC seems to have no effect
# ~~~
# Required:
# TARGET_NAME - Name of the target to generate code coverage for
# Optional:
# REQUIRED - If this is passed in, CMake configuration will fail with an error if LTO/IPO is not supported
# ~~~
function(target_link_time_optimization TARGET_NAME)
  # Argument parsing
  set(options REQUIRED)
  set(single_value_keywords)
  set(multi_value_keywords)
  cmake_parse_arguments(
    target_link_time_optimization "${options}" "${single_value_keywords}"
    "${multi_value_keywords}" ${ARGN})

  check_ipo_supported(RESULT result OUTPUT output)
  if(result)
    # It's available, set it for all following items
    set_property(TARGET ${TARGET_NAME} PROPERTY INTERPROCEDURAL_OPTIMIZATION
                                                TRUE)
  else()
    if(target_link_time_optimization_REQUIRED)
      message(
        FATAL_ERROR
          "Link Time Optimization not supported, but listed as REQUIRED for the ${TARGET_NAME} target: ${output}"
      )
    else()
      message(WARNING "Link Time Optimization not supported: ${output}")
    endif()
  endif()
endfunction()
