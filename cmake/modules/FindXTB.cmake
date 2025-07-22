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

find_library(XTB_LAPACK_LIBRARY
  NAMES liblapack lapack openblas libopenblas
  HINTS ${_XTB_HINTS}
  PATH_SUFFIXES lib lib64)

find_library(XTB_BLAS_LIBRARY
  NAMES libblas blas openblas libopenblas
  HINTS ${_XTB_HINTS}
  PATH_SUFFIXES lib lib64)

if(NOT XTB_LAPACK_LIBRARY AND XTB_DIR)
  file(GLOB _lapack_candidates
    "${XTB_DIR}/lib/*lapack*.lib" "${XTB_DIR}/lib/*openblas*.lib")
  list(GET _lapack_candidates 0 XTB_LAPACK_LIBRARY)
endif()

if(NOT XTB_BLAS_LIBRARY AND XTB_DIR)
  file(GLOB _blas_candidates
    "${XTB_DIR}/lib/*blas*.lib" "${XTB_DIR}/lib/*openblas*.lib")
  list(GET _blas_candidates 0 XTB_BLAS_LIBRARY)
endif()

if(XTB_CORE_LIBRARY)
  get_filename_component(XTB_LIBRARY_DIR "${XTB_CORE_LIBRARY}" DIRECTORY)
endif()

set(XTB_LIBRARIES ${XTB_CORE_LIBRARY})
set(_XTB_EXTRA_LIBS "")
foreach(_lib IN LISTS XTB_LAPACK_LIBRARY XTB_BLAS_LIBRARY)
  if(_lib)
    list(APPEND XTB_LIBRARIES ${_lib})
    list(APPEND _XTB_EXTRA_LIBS ${_lib})
  endif()
endforeach()

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
  XTB_INCLUDE_DIR XTB_CORE_LIBRARY XTB_LAPACK_LIBRARY XTB_BLAS_LIBRARY
  XTB_LIBRARY_DIR XTB_DIR)
