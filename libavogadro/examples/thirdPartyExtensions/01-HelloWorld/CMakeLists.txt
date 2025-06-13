# Hello World Avogadro extension.
# See http://avogadro.cc/ for more information.

# Project name
project(HelloWorldExtension)

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
set( helloworldextension_SRCS
     helloworldextension.cpp
     helloworlddialog.cpp
)

# Create a list of all ui files
set( helloworldextension_UIS
     helloworlddialog.ui
)

# Create the extension target helloworldextension
avogadro_plugin(helloworldextension "${helloworldextension_SRCS}" "${helloworldextension_UIS}")

