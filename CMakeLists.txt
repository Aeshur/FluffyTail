####################################################################################################
# Ashita - Copyright (c) 2025 Ashita Development Team
# Contact: https://www.ashitaxi.com/
# Contact: https://discord.gg/Ashita
#
# This file is part of Ashita.
#
# Ashita is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# Ashita is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with Ashita.  If not, see <https://www.gnu.org/licenses/>.
####################################################################################################

cmake_minimum_required(VERSION 3.22)

set(ASHITA_PROJECT_NAME "fluffytail")
project(${ASHITA_PROJECT_NAME} CXX)

set(CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

if (NOT CMAKE_BUILD_PARALLEL_LEVEL)
    include(ProcessorCount)
    ProcessorCount(N)

    if (NOT N EQUAL 0)
        set(CMAKE_BUILD_PARALLEL_LEVEL ${N})
    endif()
endif()

find_package(AshitaSDK REQUIRED)
ashita_sdk_initialize()

set(FLUFFYTAIL_SOURCES
    "src/defines.hpp"
    "src/exports.def"
    "src/fluffytail.cpp"
    "src/fluffytail.hpp"
    "src/hook_utils.cpp"
    "src/hook_utils.hpp"
    "src/render_runtime.cpp"
    "src/render_runtime.hpp"
    "src/tail_policy.hpp"
    "src/windower.cpp"
    "src/windower.hpp"
    )

add_library(${ASHITA_PROJECT_NAME} MODULE ${FLUFFYTAIL_SOURCES})
set_target_properties(${ASHITA_PROJECT_NAME} PROPERTIES
    CXX_EXTENSIONS ON
    CXX_STANDARD_REQUIRED ON
    LINKER_LANGUAGE CXX
    )

if (WIN32)
    set_target_properties(${ASHITA_PROJECT_NAME} PROPERTIES
        OUTPUT_NAME ${ASHITA_PROJECT_NAME}
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
        )
endif()

ashita_sdk_attach(${ASHITA_PROJECT_NAME})
ashita_sdk_set_compiler_defaults(${ASHITA_PROJECT_NAME})
ashita_sdk_set_compiler_warn_level(${ASHITA_PROJECT_NAME} 4)
ashita_sdk_set_compiler_warnings_as_errors(${ASHITA_PROJECT_NAME})
ashita_sdk_set_linker_defaults(${ASHITA_PROJECT_NAME})
# Required for D3D device calls in the 32-bit Ashita host.
target_link_options(${ASHITA_PROJECT_NAME} PRIVATE /SAFESEH:NO)
if (MSVC)
    # Host and COM callbacks use catch-all guards as their final fault boundary.
    # /EHa is required for those guards to include Windows structured exceptions.
    target_compile_options(${ASHITA_PROJECT_NAME} PRIVATE /EHa)
endif()

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    ashita_sdk_set_linker_enable_debug_linking(${ASHITA_PROJECT_NAME})
else()
    ashita_sdk_set_linker_disable_debug_linking(${ASHITA_PROJECT_NAME})
endif()

include(CTest)
if (BUILD_TESTING)
    add_executable(fluffytail_native_tests
        "tests/native_tests.cpp"
        "src/hook_utils.cpp"
        "src/hook_utils.hpp"
        "src/tail_policy.hpp"
        "src/windower.hpp"
        )
    target_include_directories(fluffytail_native_tests PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        )
    set_target_properties(fluffytail_native_tests PROPERTIES
        CXX_EXTENSIONS ON
        CXX_STANDARD_REQUIRED ON
        )
    ashita_sdk_attach(fluffytail_native_tests)
    ashita_sdk_set_compiler_defaults(fluffytail_native_tests)
    ashita_sdk_set_compiler_warn_level(fluffytail_native_tests 4)
    ashita_sdk_set_compiler_warnings_as_errors(fluffytail_native_tests)
    add_dependencies(fluffytail_native_tests ${ASHITA_PROJECT_NAME})
    add_test(NAME fluffytail_native_tests
        COMMAND fluffytail_native_tests "$<TARGET_FILE:${ASHITA_PROJECT_NAME}>"
        )
endif()
