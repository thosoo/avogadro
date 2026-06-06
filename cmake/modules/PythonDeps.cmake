# Find all Python dependencies.
#
# This module defines:
#   ALL_PYTHON_FOUND       - TRUE when all optional Python integration
#                            dependencies are available.
#   PYTHON_SUPPORT_ENABLED - TRUE when ENABLE_PYTHON was requested and all
#                            optional Python integration dependencies are
#                            available.
#
# It intentionally uses CMake's modern Python3 package so a failed optional
# Python probe does not leave NOTFOUND include or library paths attached to
# targets.

set(ALL_PYTHON_FOUND FALSE)
set(PYTHON_SUPPORT_ENABLED FALSE)

message(STATUS "Searching for python dependencies...")

# Boost Python
message(STATUS "[1/5] Boost Python")
# CMake's FindBoost has an option to look for additional versions.
set(Boost_ADDITIONAL_VERSIONS "1.45" "1.44" "1.43" "1.42" "1.41" "1.40"
  "1.40.0" "1.39" "1.39.0" "1.38" "1.38.0" "1.37" "1.37.0")
find_package(Boost COMPONENTS python)
if(Boost_PYTHON_FOUND)
  message(STATUS "Boost Python found...")
else()
  message(STATUS "Boost Python NOT found - Python support disabled.")
  return()
endif()

# Python interpreter and development files
message(STATUS "[2/5] Python3 Interpreter and Development")
find_package(Python3 COMPONENTS Interpreter Development)
if(NOT Python3_FOUND)
  message(STATUS "Python3 interpreter/development files NOT found - Python support disabled.")
  return()
endif()

# Numpy
message(STATUS "[3/5] Numpy Module")
find_package(Numpy)
if(NOT NUMPY_FOUND)
  message(STATUS "Numpy NOT found - Python support disabled.")
  return()
endif()

# SIP
message(STATUS "[4/5] SIP Module")
find_package(SIP)
if(NOT SIP_FOUND)
  message(STATUS "sip.h header NOT found - Python support disabled")
  return()
endif()

message(STATUS "[5/5] Python integration dependencies complete")
set(ALL_PYTHON_FOUND TRUE)
set(PYTHON_SUPPORT_ENABLED TRUE)
