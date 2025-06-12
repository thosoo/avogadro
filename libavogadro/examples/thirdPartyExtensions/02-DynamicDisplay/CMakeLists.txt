# Dynamic Display Avogadro extension.
# See http://avogadro.cc/ for more information.

# Project name
project(DynamicDisplayExtension)

# Minimum version of CMake that can be used
cmake_minimum_required(VERSION 2.6)

include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/Qt5Compat.cmake)
find_package(Avogadro REQUIRED)
include(${Avogadro_USE_FILE})
include_directories(${CMAKE_CURRENT_BINARY_DIR})

set( dynamicdisplayextension_SRCS
     dynamicdisplayextension.cpp
     dynamicdisplaydialog.cpp
)

set( dynamicdisplayextension_UIS
     dynamicdisplaydialog.ui
)

avogadro_plugin( dynamicdisplayextension
                 "${dynamicdisplayextension_SRCS}"
		 "${dynamicdisplayextension_UIS}")
