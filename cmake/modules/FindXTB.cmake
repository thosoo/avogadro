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
pkg_check_modules(XTB_PC QUIET xtb)

set(_XTB_HINTS "")
if(XTB_PC_FOUND)
  list(APPEND _XTB_HINTS ${XTB_PC_LIBRARY_DIRS} ${XTB_PC_INCLUDE_DIRS})
endif()

if(NOT XTB_DIR AND DEFINED ENV{XTB_DIR})
  set(XTB_DIR "$ENV{XTB_DIR}")
endif()
if(XTB_DIR)
  list(APPEND _XTB_HINTS "${XTB_DIR}")
endif()

find_path(XTB_INCLUDE_DIR NAMES xtb.h
  HINTS ${_XTB_HINTS}
  PATH_SUFFIXES include)

find_library(XTB_LIBRARY NAMES xtb
  HINTS ${_XTB_HINTS}
  PATH_SUFFIXES lib lib64)

if(XTB_LIBRARY)
  get_filename_component(XTB_LIBRARY_DIR "${XTB_LIBRARY}" DIRECTORY)
endif()

set(XTB_LIBRARIES ${XTB_LIBRARY})
set(XTB_INCLUDE_DIRS ${XTB_INCLUDE_DIR})
set(XTB_LIBRARY_DIRS ${XTB_LIBRARY_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(XTB DEFAULT_MSG XTB_LIBRARY XTB_INCLUDE_DIR)

mark_as_advanced(XTB_INCLUDE_DIR XTB_LIBRARY XTB_LIBRARY_DIR XTB_DIR)
