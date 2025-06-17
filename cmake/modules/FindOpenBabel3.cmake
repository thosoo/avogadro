# - Try to find OpenBabel3
# Once done this will define
#
#  OPENBABEL3_FOUND - system has OpenBabel3
#  OPENBABEL3_INCLUDE_DIR - the OpenBabel3 include directory
#  OPENBABEL3_LIBRARIES - Link these to use OpenBabel3
# Copyright (c) 2006, 2007 Carsten Niehaus, <cniehaus@gmx.de>
# Copyright (C) 2008 Marcus D. Hanwell <marcus@cryos.org>
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

include(MacroEnsureVersion)

if(OPENBABEL3_INCLUDE_DIR AND OPENBABEL3_LIBRARIES AND OPENBABEL3_VERSION_MET)
  # in cache already
  set(OPENBABEL3_FOUND TRUE)

else()
  if(NOT WIN32)

    # Use the newer PkgConfig stuff
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(OPENBABEL3 openbabel-3)
    if(NOT OPENBABEL3_FOUND)
      pkg_check_modules(OPENBABEL3 openbabel3>=3.0.0)
    endif()

    # Maintain backwards compatibility with previous version of module
    if(OPENBABEL3_FOUND STREQUAL "1")
      set(OPENBABEL3_VERSION_MET TRUE)
      set(OPENBABEL3_INCLUDE_DIR ${OPENBABEL3_INCLUDE_DIRS})
    endif()

  else(NOT WIN32)
    set(OPENBABEL3_VERSION_MET TRUE)
  endif()

  if(OPENBABEL3_LIBRARIES)
    get_filename_component(OPENBABEL3_LIBRARY_DIRS
      ${OPENBABEL3_LIBRARIES} DIRECTORY)
  endif()

  if(OPENBABEL3_VERSION_MET)

    if(WIN32)
      if(NOT OPENBABEL3_INCLUDE_DIR)
        find_path(OPENBABEL3_INCLUDE_DIR openbabel3/openbabel/obconversion.h
          PATHS
          ${_obIncDir}
          ${GNUWIN32_DIR}/include
          $ENV{OPENBABEL3_INCLUDE_DIR}
        )
        if(OPENBABEL3_INCLUDE_DIR)
          set(OPENBABEL3_INCLUDE_DIR ${OPENBABEL3_INCLUDE_DIR}/openbabel3)
        endif(OPENBABEL3_INCLUDE_DIR)
      endif(NOT OPENBABEL3_INCLUDE_DIR)
    endif()

    find_library(OPENBABEL3_LIBRARIES NAMES openbabel openbabel3
      PATHS
      ${_obLinkDir}
      ${GNUWIN32_DIR}/lib
      $ENV{OPENBABEL3_LIBRARIES}
    )
  endif()

  if(OPENBABEL3_INCLUDE_DIR AND OPENBABEL3_LIBRARIES AND OPENBABEL3_VERSION_MET)
    set(OPENBABEL3_FOUND TRUE)
  endif(OPENBABEL3_INCLUDE_DIR AND OPENBABEL3_LIBRARIES AND OPENBABEL3_VERSION_MET)

  if(OPENBABEL3_FOUND)
    if(NOT OpenBabel3_FIND_QUIETLY)
      message(STATUS "Found OpenBabel 3.0 or later: ${OPENBABEL3_LIBRARIES}")
    endif()
  else()
    if(OpenBabel3_FIND_REQUIRED)
      message(FATAL_ERROR "Could NOT find OpenBabel 3.0 or later ")
    endif()
  endif()

  mark_as_advanced(OPENBABEL3_INCLUDE_DIR OPENBABEL3_LIBRARIES)

endif()

# Search for Open Babel3 executable
if(OPENBABEL3_EXECUTABLE)

  # in cache already
  set(OPENBABEL3_EXECUTABLE_FOUND TRUE)

else()
  find_program(OPENBABEL3_EXECUTABLE NAMES babel
    PATHS
    [HKEY_CURRENT_USER\\SOFTWARE\\OpenBabel\ 3.0.0]
    $ENV{OPENBABEL3_EXECUTABLE}
  )

  if(OPENBABEL3_EXECUTABLE)
    set(OPENBABEL3_EXECUTABLE_FOUND TRUE)
  endif(OPENBABEL3_EXECUTABLE)

  if(OPENBABEL3_EXECUTABLE_FOUND)
    message(STATUS "Found OpenBabel3 executable: ${OPENBABEL3_EXECUTABLE}")
  endif(OPENBABEL3_EXECUTABLE_FOUND)

endif()

if(OPENBABEL3_FOUND)
  set(OPENBABEL2_FOUND ${OPENBABEL3_FOUND})
  set(OPENBABEL2_INCLUDE_DIR ${OPENBABEL3_INCLUDE_DIR})
  set(OPENBABEL2_LIBRARIES ${OPENBABEL3_LIBRARIES})
  if(OPENBABEL3_LIBRARY_DIRS)
    set(OPENBABEL2_LIBRARY_DIRS ${OPENBABEL3_LIBRARY_DIRS})
  endif()
  if(OPENBABEL3_EXECUTABLE)
    set(OPENBABEL2_EXECUTABLE ${OPENBABEL3_EXECUTABLE})
  endif()
endif()

