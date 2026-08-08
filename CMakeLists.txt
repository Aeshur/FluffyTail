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

if (CMAKE_BUILD_TYPE STREQUAL "Debug")
    ashita_sdk_set_linker_enable_debug_linking(${ASHITA_PROJECT_NAME})
else()
    ashita_sdk_set_linker_disable_debug_linking(${ASHITA_PROJECT_NAME})
endif()
