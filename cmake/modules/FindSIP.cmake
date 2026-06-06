# - Find sip
# Find the python sip includes making sure it is loadable from python
# This module defines
#  SIP_INCLUDE_DIR, where to find sip.h, etc.
#  SIP_FOUND, If false, do not try to use SIP headers.

execute_process(COMMAND ${Python3_EXECUTABLE} -c
  "import sip; print(sip.SIP_VERSION)"
  OUTPUT_VARIABLE SIP_VERSION
  OUTPUT_STRIP_TRAILING_WHITESPACE)

if(SIP_VERSION)
  set(SIP_PYTHON_FOUND TRUE)
  message(STATUS "SIP version ${SIP_VERSION} found")
else()
  message(STATUS "SIP module not available in Python.")
  set(SIP_PYTHON_FOUND FALSE)
endif()

if(SIP_PYTHON_FOUND)
  if(SIP_INCLUDE_DIR)
    set(SIP_FOUND TRUE)
  else()
    set(_sip_include_hints ${Python3_INCLUDE_DIRS})
    find_path(SIP_INCLUDE_DIR NAMES sip.h
      HINTS ${_sip_include_hints}
      PATH_SUFFIXES SIP)
  endif()
  include(FindPackageHandleStandardArgs)
  find_package_handle_standard_args(SIP DEFAULT_MSG SIP_INCLUDE_DIR)
else()
  set(SIP_FOUND FALSE)
endif()

mark_as_advanced(SIP_INCLUDE_DIR)
