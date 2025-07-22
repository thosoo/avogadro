# - Find the xTB library
#
# This module tries to locate the xTB headers and libraries. It first
# uses pkg-config and falls back to XTB_DIR (or the environment variable
# of the same name) if pkg-config fails.  The following variables are
# defined:
#
#  XTB_FOUND         - True if xTB was found
#  XTB_INCLUDE_DIRS  - Include directories for xTB
#  XTB_LIBRARIES     - Libraries to link against (always includes libxtb)
#  XTB_LIBRARY_DIRS  - Library directories
#
# Usage: find_package(XTB REQUIRED)

find_package(PkgConfig)
pkg_check_modules(XTB_PC QUIET xtb)

if(XTB_PC_FOUND)
  set(XTB_FOUND TRUE)
  set(XTB_INCLUDE_DIRS ${XTB_PC_INCLUDE_DIRS})
  set(XTB_LIBRARIES ${XTB_PC_LIBRARIES})
  set(XTB_LIBRARY_DIRS ${XTB_PC_LIBRARY_DIRS})
  if(NOT TARGET XTB::xtb)
    add_library(XTB::xtb INTERFACE IMPORTED)
    set_target_properties(XTB::xtb PROPERTIES
      INTERFACE_INCLUDE_DIRECTORIES "${XTB_INCLUDE_DIRS}"
      INTERFACE_LINK_LIBRARIES "${XTB_LIBRARIES}")
  endif()
  return()
endif()

set(_XTB_HINTS "")

if(NOT XTB_DIR AND DEFINED ENV{XTB_DIR})
  set(XTB_DIR "$ENV{XTB_DIR}")
endif()
if(XTB_DIR)
  list(APPEND _XTB_HINTS "${XTB_DIR}")
endif()

find_path(XTB_INCLUDE_DIR NAMES xtb.h
  HINTS ${_XTB_HINTS}
  PATH_SUFFIXES include)

find_library(XTB_CORE_LIBRARY NAMES xtb
  HINTS ${_XTB_HINTS}
  PATH_SUFFIXES lib lib64)

find_library(MKL_RT_LIBRARY
  NAMES mkl_rt
  HINTS "$ENV{ONEAPI_ROOT}/mkl/latest/lib/intel64"
        "$ENV{MKLROOT}/lib/intel64"
        "${XTB_DIR}/lib"
  NO_DEFAULT_PATH)

find_library(MKL_OMP_LIBRARY
  NAMES libiomp5md iomp5md
  HINTS "$ENV{ONEAPI_ROOT}/compiler/latest/windows/redist/intel64_win/compiler"
  NO_DEFAULT_PATH)

if(XTB_CORE_LIBRARY)
  get_filename_component(XTB_LIBRARY_DIR "${XTB_CORE_LIBRARY}" DIRECTORY)
endif()

set(XTB_LIBRARIES ${XTB_CORE_LIBRARY})
set(_XTB_EXTRA_LIBS "")
if(MKL_RT_LIBRARY)
  list(APPEND XTB_LIBRARIES ${MKL_RT_LIBRARY} ${MKL_OMP_LIBRARY})
  list(APPEND _XTB_EXTRA_LIBS ${MKL_RT_LIBRARY} ${MKL_OMP_LIBRARY})
endif()

if(XTB_INCLUDE_DIR AND XTB_CORE_LIBRARY)
  set(XTB_FOUND TRUE)
endif()

set(XTB_INCLUDE_DIRS ${XTB_INCLUDE_DIR})
set(XTB_LIBRARY_DIRS ${XTB_LIBRARY_DIR})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(XTB DEFAULT_MSG
  XTB_CORE_LIBRARY XTB_INCLUDE_DIR)

if(XTB_FOUND AND NOT TARGET XTB::xtb)
  add_library(XTB::xtb UNKNOWN IMPORTED)
  set_target_properties(XTB::xtb PROPERTIES
    IMPORTED_LOCATION "${XTB_CORE_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${XTB_INCLUDE_DIR}"
    INTERFACE_LINK_LIBRARIES "${_XTB_EXTRA_LIBS}")
endif()

mark_as_advanced(
  XTB_INCLUDE_DIR XTB_CORE_LIBRARY XTB_LIBRARY_DIR XTB_DIR)
