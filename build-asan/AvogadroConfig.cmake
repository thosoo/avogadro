# Avogadro CMake configuration file - http://avogadro.cc/

include(CMakeFindDependencyMacro)

# If this file was found, then Avogadro has been found
set(Avogadro_FOUND 1)

set(Avogadro_VERSION_MAJOR   "1")
set(Avogadro_VERSION_MINOR   "2")
set(Avogadro_VERSION_PATCH   "0")
set(Avogadro_VERSION         "${Avogadro_VERSION_MAJOR}.${Avogadro_VERSION_MINOR}.${Avogadro_VERSION_PATCH}")

set(Avogadro_ENABLE_PYTHON   "FALSE")
set(Avogadro_ENABLE_GLSL     "OFF")

set(Avogadro_INSTALL_PREFIX  "/usr/local")
set(Avogadro_INCLUDE_DIRS    "/usr/local/include")
set(Avogadro_LIBRARY_DIRS    "/usr/local/lib")
set(Avogadro_LIB_SUFFIX      "")
set(Avogadro_PLUGIN_DIR      "/usr/local/lib/avogadro/1_2")
set(Avogadro_BIN_DIRS        "/usr/local/bin")

list(APPEND CMAKE_MODULE_PATH "${Avogadro_PLUGIN_DIR}/cmake")
find_dependency(Qt6 COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
find_dependency(Eigen3)
find_dependency(OpenBabel3)

include("${CMAKE_CURRENT_LIST_DIR}/AvogadroTargets.cmake")

# Legacy use file for third-party plugin projects. Prefer exported targets.
set(Avogadro_USE_FILE
    "${Avogadro_PLUGIN_DIR}/AvogadroUse.cmake")
