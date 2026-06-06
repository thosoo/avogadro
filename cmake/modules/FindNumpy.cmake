# - Find numpy
# Find the native numpy includes
# This module defines
#  NUMPY_INCLUDE_DIR, where to find numpy/arrayobject.h, etc.
#  NUMPY_FOUND, If false, do not try to use numpy headers.

execute_process(COMMAND ${Python3_EXECUTABLE} -c
    "import numpy; print(numpy.get_include())"
    OUTPUT_VARIABLE NUMPY_INCLUDE_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE)

if(NUMPY_INCLUDE_DIR)
  if(EXISTS ${NUMPY_INCLUDE_DIR}/numpy/arrayobject.h)
    set(NUMPY_FOUND TRUE)
    set(NUMPY_INCLUDE_DIR ${NUMPY_INCLUDE_DIR} CACHE STRING "Numpy include path")
  else()
    set(NUMPY_FOUND FALSE)
  endif()
else()
  set(NUMPY_FOUND FALSE)
endif()

if(NUMPY_FOUND)
  if(NOT NUMPY_FIND_QUIETLY)
    message(STATUS "Numpy headers found")
  endif()
else()
  if(NUMPY_FIND_REQUIRED)
    message(FATAL_ERROR "Numpy headers missing")
  endif()
endif()

mark_as_advanced(NUMPY_INCLUDE_DIR)
