# Hello World Avogadro extension.
# See http://avogadro.cc/ for more information.

# Project name
project(RotateSelectionExtension)

# Minimum version of CMake that can be used
cmake_minimum_required(VERSION 2.6)

# Set up Qt
include(${CMAKE_CURRENT_SOURCE_DIR}/../../../cmake/Qt5Compat.cmake)

# Set up Avogadro
find_package(Avogadro REQUIRED)
include(${Avogadro_USE_FILE})

# Specify include directories
include_directories(
  # Needed to include Qt moc'd headers
  ${CMAKE_CURRENT_BINARY_DIR}
)

# Create a list of all source files
set( rotateselectionextension_SRCS
     rotateselectionextension.cpp
     rotateselectiondialog.cpp
)

# Create a list of all ui files
set( rotateselectionextension_UIS
     rotateselectiondialog.ui
)

# Create the extension target rotateselectionextension
avogadro_plugin(rotateselectionextension "${rotateselectionextension_SRCS}" "${rotateselectionextension_UIS}")

