#  LIBMSYM_INCLUDE_DIRS - include directories for FooBar
#  LIBMSYM_LIBRARIES    - libraries to link against
 
# Compute paths
get_filename_component(LIBMSYM_CMAKE_DIR "${CMAKE_CURRENT_LIST_FILE}" PATH)
set(LIBMSYM_INCLUDE_DIRS "/home/thaison/avogadro/libavogadro/src/extensions/symmetry/libmsym;/home/thaison/avogadro/build-asan/libavogadro/src/extensions/symmetry/libmsym")
 
# Our library dependencies (contains definitions for IMPORTED targets)
if(NOT TARGET msym AND NOT LIBMSYM_BINARY_DIR)
  include("${LIBMSYM_CMAKE_DIR}/libmsymTargets.cmake")
endif()
   
# These are IMPORTED targets created by FooBarTargets.cmake
set(LIBMSYM_LIBRARIES msym)
