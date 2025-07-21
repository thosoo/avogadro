# - Find the xTB library
#
# This module tries to locate the xTB headers and libraries. It first
# uses pkg-config and falls back to XTB_DIR (or the environment variable
# of the same name) if pkg-config fails.  The following variables are
# defined:
#
#  XTB_FOUND        - True if xTB was found
#  XTB_INCLUDE_DIRS - Include directories for xTB
#  XTB_LIBRARIES    - Libraries to link against
#  XTB_LIBRARY_DIRS - Library directories
#
# Usage: find_package(XTB REQUIRED)

find_package(PkgConfig)
pkg_check_modules(XTB xtb)

if(NOT XTB_FOUND)
  if(NOT XTB_DIR)
    set(XTB_DIR $ENV{XTB_DIR})
  endif()
  if(XTB_DIR)
    file(TO_CMAKE_PATH "${XTB_DIR}" XTB_DIR)
    find_path(XTB_INCLUDE_DIRS
      NAMES xtb.h
      HINTS "${XTB_DIR}"
      PATH_SUFFIXES include
    )
    find_library(XTB_LIBRARY
      NAMES xtb
      HINTS "${XTB_DIR}"
      PATH_SUFFIXES lib lib64
    )
    if(XTB_LIBRARY)
      get_filename_component(XTB_LIBRARY_DIRS "${XTB_LIBRARY}" DIRECTORY)
      set(XTB_LIBRARIES ${XTB_LIBRARY})
    endif()
    if(XTB_INCLUDE_DIRS AND XTB_LIBRARIES)
      set(XTB_FOUND TRUE)
    else()
      set(XTB_FOUND FALSE)
    endif()
  endif()
endif()

if(XTB_FOUND)
  if(NOT XTB_FIND_QUIETLY)
    message(STATUS "Found xTB: ${XTB_LIBRARIES}")
  endif()
else()
  if(XTB_FIND_REQUIRED)
    message(FATAL_ERROR "Could not find xTB")
  endif()
endif()

mark_as_advanced(XTB_INCLUDE_DIRS XTB_LIBRARIES XTB_LIBRARY_DIRS XTB_DIR)
