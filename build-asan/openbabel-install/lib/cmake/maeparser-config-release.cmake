#----------------------------------------------------------------
# Generated CMake target import file for configuration "Release".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "maeparser" for configuration "Release"
set_property(TARGET maeparser APPEND PROPERTY IMPORTED_CONFIGURATIONS RELEASE)
set_target_properties(maeparser PROPERTIES
  IMPORTED_LOCATION_RELEASE "${_IMPORT_PREFIX}/lib/libmaeparser.so.1.2.3"
  IMPORTED_SONAME_RELEASE "libmaeparser.so.1"
  )

list(APPEND _cmake_import_check_targets maeparser )
list(APPEND _cmake_import_check_files_for_maeparser "${_IMPORT_PREFIX}/lib/libmaeparser.so.1.2.3" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
