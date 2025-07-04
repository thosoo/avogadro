cmake_minimum_required(VERSION 3.5)
project(Avogadro) # the name of your project
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The Avogadro version number, currently used by the Avogadro library too
set(Avogadro_VERSION_MAJOR 1)
set(Avogadro_VERSION_MINOR 2)
set(Avogadro_VERSION_PATCH 0)
set(Avogadro_VERSION "${Avogadro_VERSION_MAJOR}.${Avogadro_VERSION_MINOR}")
set(Avogadro_VERSION_FULL "${Avogadro_VERSION}.${Avogadro_VERSION_PATCH}")

set(CMAKE_MODULE_PATH ${Avogadro_SOURCE_DIR}/cmake/modules)

cmake_policy(SET CMP0003 NEW)
if(POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

# this is adapted from KDE's FindKDE4Internal.cmake : default the build type to
# "release with debug info".
#
# We will define two other build types: Debug and Release.
# These names are case-insensitive i.e. you can do -DCMAKE_BUILD_TYPE=debug
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()
message(STATUS "The build type is ${CMAKE_BUILD_TYPE}")

include(CheckCXXCompilerFlag)
include(MacroEnsureVersion)
include(TestIfWeNeedFPermissive)

# use mulithreaded dll runtime on windows when using MSVC
if (MSVC)
  # Make sure the build is parallelised for faster builds
  set(CMAKE_CXX_FLAGS"${CMAKE_CXX_FLAGS} /MP")
  # Do not generate manifests for the plugins - caused issues loading plugins
  set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /MANIFEST:NO")
endif()

###########################################################
# Here I am adding GCC magic from KDE. With these settings#
# compiler issues are fixed and more warnings are issued. #
###########################################################
# We should always respect the users CXXFLAGS if they have set them
if (CMAKE_COMPILER_IS_GNUCXX)
  set (CMAKE_CXX_FLAGS_WARN "-Wnon-virtual-dtor -Wno-long-long -ansi -Wcast-align -Wchar-subscripts -Wall -Wextra -Wpointer-arith -Wformat-security -fno-check-new -fno-common -pedantic")
  # On Linux with GCC 4.3+ these flags generate thousands of warnings, so they
  # are now optional. Most of the warnings are in included headers.
  option(EXTRA_WARNINGS
    "Enable extra GCC warnings - can generate thousands" OFF)
  if(EXTRA_WARNINGS)
    # Enable extra compiler warnings for extra checking
    set (CMAKE_CXX_FLAGS_WARN "${CMAKE_CXX_FLAGS_WARN} -Wconversion -Woverloaded-virtual -Wfloat-equal")
#  set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wshadow -Wold-style-cast")
  endif()
  add_definitions (-D_DEFAULT_SOURCE)
  # Set some linker flags
  if(CMAKE_SYSTEM_NAME MATCHES Linux)
    set(CMAKE_SHARED_LINKER_FLAGS
      "-Wl,--fatal-warnings -Wl,--no-undefined -lc ${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_SHARED_LINKER_FLAGS
      "-Wl,--enable-new-dtags ${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS
      "-Wl,--fatal-warnings -Wl,--no-undefined -lc ${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS
      "-Wl,--enable-new-dtags ${CMAKE_SHARED_LINKER_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS
      "-Wl,--fatal-warnings -Wl,--no-undefined -lc ${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_EXE_LINKER_FLAGS
      "-Wl,--enable-new-dtags ${CMAKE_EXE_LINKER_FLAGS}")
  endif()
  # Now check if we can use visibility to selectively export symbols
  # Get the GCC version - from KDE4 cmake files
  exec_program(${CMAKE_C_COMPILER} ARGS --version OUTPUT_VARIABLE	_gcc_version_info)
  string (REGEX MATCH "[3-9]\\.[0-9]\\.[0-9]" _gcc_version "${_gcc_version_info}")
  # gcc on mac just reports: "gcc (GCC) 3.3 20030304 ..." without the
  # patch level, handle this here:
  if (NOT _gcc_version)
    string (REGEX REPLACE ".*\\(GCC\\).* ([34]\\.[0-9]) .*" "\\1.0" _gcc_version "${_gcc_version_info}")
  endif ()

  # No RTTI flag
  check_cxx_compiler_flag(-fno-rtti HAVE_NO_RTTI)
  if(HAVE_NO_RTTI)
    set(NO_RTTI_FLAG "-fno-rtti")
    # RTTI flag
    check_cxx_compiler_flag(-frtti HAVE_RTTI)
    if(HAVE_RTTI)
      set(RTTI_FLAG "-frtti")
    else()
      set(RTTI_FLAG "")
    endif()
  else()
    set(NO_RTTI_FLAG "")
    set(RTTI_FLAG "")
  endif()

  if(NOT APPLE) # Mac GCC42 compiler has bug
    check_cxx_compiler_flag(-fvisibility=hidden HAVE_GCC_VISIBILITY)
    set(HAVE_GCC_VISIBILITY ${HAVE_GCC_VISIBILITY} CACHE BOOL "GCC support for hidden visibility")
    macro_ensure_version("4.1.0" "${_gcc_version}" GCC_IS_NEWER_THAN_4_1)
    macro_ensure_version("4.2.0" "${_gcc_version}" GCC_IS_NEWER_THAN_4_2)
    set(_GCC_COMPILED_WITH_BAD_ALLOCATOR FALSE)
    if (GCC_IS_NEWER_THAN_4_1)
      exec_program(${CMAKE_C_COMPILER} ARGS -v OUTPUT_VARIABLE _gcc_alloc_info)
      string(REGEX MATCH "(--enable-libstdcxx-allocator=mt)" _GCC_COMPILED_WITH_BAD_ALLOCATOR "${_gcc_alloc_info}")
    endif ()

    if (HAVE_GCC_VISIBILITY AND GCC_IS_NEWER_THAN_4_1 AND NOT _GCC_COMPILED_WITH_BAD_ALLOCATOR)
      # We have all the parts necessary - use visibility support
      add_definitions(-DHAVE_GCC_VISIBILITY)
      set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
      if (GCC_IS_NEWER_THAN_4_2)
        set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")
      endif()
    endif()
  endif()
  # Set up additional build flags for particular build types. These will be added to CMAKE_CXX_FLAGS,
  # they will not replace them.
  set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG -DQT_NO_DEBUG_OUTPUT -Wl,-s")
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO
    "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_CXX_FLAGS_WARN}")
  set(CMAKE_CXX_FLAGS_DEBUG   "-g3 -fno-inline ${CMAKE_CXX_FLAGS_WARN}")

  TEST_IF_WE_NEED_FPERMISSIVE(NEED_FPERMISSIVE)
  if(NEED_FPERMISSIVE)
    #OK, adding -fpermissive to the C++ compiler flags
    #Workaround for a problem with Boost.Python and GCC >= 4.3.1
    #Further information: cmake/modules/TestIfWeNeedFPermissive.cmake
    set (CMAKE_CXX_FLAGS   "${CMAKE_CXX_FLAGS} -fpermissive")
  endif()

  # define WITH_SSE2 to enable the SSE2 instruction set.
  # Available on Pentium 4, Athlon 64, and newer CPUs.
  # Enabled by default on the x86_64 architecture, but not on the x86 architecture.
  # If SSE2 is enabled, Eigen uses it by default; this can be disabled by
  # definining EIGEN_DONT_VECTORIZE.
  if(WITH_SSE2)
    message(STATUS "Enabling the SSE2 instruction set")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
  endif()
endif()


# Intel C++ Compiler
# Note: Windows version uses different options!
if(${CMAKE_CXX_COMPILER_ID} MATCHES Intel AND UNIX)
  # Disable warnings:
  # 981 - operands are evaluated in unspecified order
  # 1599 - foreach
  set (CMAKE_CXX_FLAGS_WARN "-Wnon-virtual-dtor -ansi -Wcheck -Wall -Wpointer-arith -wd981,1599")
  option(EXTRA_WARNINGS
    "Enable extra Intel Compiler warnings and remarks" OFF)
  if(EXTRA_WARNINGS)
    # Enable extra compiler warnings for extra checking
    set (CMAKE_CXX_FLAGS_WARN "${CMAKE_CXX_FLAGS_WARN} -w2 -Weffc++ -wd2012,2015")
  else()
    # 810 - conversion to float
    # 383 - value copied to temporary, reference to temporary used
    # 1418 - external function definition with no prior declaration
    set (CMAKE_CXX_FLAGS_WARN "${CMAKE_CXX_FLAGS_WARN} -wd383,810,1418")
  endif()
  add_definitions (-D_DEFAULT_SOURCE)
  # Set some linker flags
  if(CMAKE_SYSTEM_NAME MATCHES Linux)
    set(CMAKE_SHARED_LINKER_FLAGS
      "-lc ${CMAKE_SHARED_LINKER_FLAGS}")
    set(CMAKE_MODULE_LINKER_FLAGS
      "-lc ${CMAKE_MODULE_LINKER_FLAGS}")
    set (CMAKE_EXE_LINKER_FLAGS
      "-lc ${CMAKE_EXE_LINKER_FLAGS}")
  endif()

  # No RTTI flag
  check_cxx_compiler_flag(-fno-rtti HAVE_NO_RTTI)
  if(HAVE_NO_RTTI)
    set(NO_RTTI_FLAG "-fno-rtti")
    # RTTI flag
    check_cxx_compiler_flag(-frtti HAVE_RTTI)
    if(HAVE_RTTI)
      set(RTTI_FLAG "-frtti")
    else()
      set(RTTI_FLAG "")
    endif()
  else()
    set(NO_RTTI_FLAG "")
    set(RTTI_FLAG "")
  endif()

  check_cxx_compiler_flag(-fvisibility=hidden HAVE_INTEL_VISIBILITY)
  set(HAVE_INTEL_VISIBILITY ${HAVE_INTEL_VISIBILITY} CACHE BOOL "Intel Compiler support for hidden visibility")
  if(HAVE_INTEL_VISIBILITY)
    add_definitions(-DHAVE_GCC_VISIBILITY)
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden")
    check_cxx_compiler_flag(-fvisibility-inlines-hidden HAVE_INTEL_VISIBILITY_INLINES)
    set(HAVE_INTEL_VISIBILITY_INLINES ${HAVE_INTEL_VISIBILITY_INLINES} CACHE BOOL "Intel Compiler support for -fvisibility-inlines-hidden")
    if(HAVE_INTEL_VISIBILITY_INLINES)
      set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility-inlines-hidden")
    endif()
  endif()

  set(CMAKE_C_FLAGS_RELEASE "-O3 -ipo1 -DNDEBUG -DQT_NO_DEBUG_OUTPUT -Wl,-s")
  set(CMAKE_CXX_FLAGS_RELEASE "-O3 -ipo1 -DNDEBUG -DQT_NO_DEBUG_OUTPUT -Wl,-s")

  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO
    "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} ${CMAKE_CXX_FLAGS_WARN}")
  set(CMAKE_CXX_FLAGS_DEBUG   "-g3 -fno-inline ${CMAKE_CXX_FLAGS_WARN}")

  TEST_IF_WE_NEED_FPERMISSIVE(NEED_FPERMISSIVE)
  if(NEED_FPERMISSIVE)
    #OK, adding -fpermissive to the C++ compiler flags
    #Workaround for a problem with Boost.Python and GCC >= 4.3.1
    #Further information: cmake/modules/TestIfWeNeedFPermissive.cmake
    set (CMAKE_CXX_FLAGS   "${CMAKE_CXX_FLAGS} -fpermissive")
  endif()

  # define WITH_SSE2 to enable the SSE2 instruction set.
  # Available on Pentium 4, Athlon 64, and newer CPUs.
  # If SSE2 is enabled, Eigen uses it by default; this can be disabled by
  # definining EIGEN_DONT_VECTORIZE.
  if(CMAKE_SIZEOF_VOID_P EQUAL 8 OR WITH_SSE2)
    message(STATUS "Enabling the SSE2 instruction set")
    set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse2")
  endif()
endif()

set(I18N_LANGUAGE "" CACHE STRING "Build i18n only for selected language")

include(cmake/Qt5Compat.cmake)
find_package(Qt5 REQUIRED COMPONENTS Widgets Gui OpenGL Network LinguistTools Core Concurrent Test)
find_package(OpenGL REQUIRED) # find and setup OpenGL
find_package(X11) # find and setup X11 (not required for Mac and Windows)
find_package(Linguist) # find and setup Linguist
if(NOT Linguist_FOUND)
  message(WARNING "Qt5 Linguist not found, please install it if you want Avogadro translations")
endif()

find_package(Eigen3 REQUIRED) # find and setup Eigen3 if available

find_package(ZLIB REQUIRED)
find_package(OpenBabel3 REQUIRED) # find and setup OpenBabel

if (Q_WS_X11)
  find_package(X11 REQUIRED) # avogadro/src/main.cpp calls XInitThread().
endif()

# Check if we are building from a Git clone or a released version
function(git_version dir version_var)
  # Function to figure out the Git short version hash
  if(EXISTS ${dir}/.git)
    execute_process(COMMAND git rev-parse --short HEAD
            WORKING_DIRECTORY ${dir}
            OUTPUT_VARIABLE var)
    string(REGEX REPLACE "\n" "" var "${var}")
    message(STATUS "Git revision found to be ${var}")
    set(${version_var} "${var}" PARENT_SCOPE)
  endif(EXISTS ${dir}/.git)
endfunction()

git_version(${CMAKE_SOURCE_DIR} SCM_REVISION)
message(STATUS "Found Git revision is: ${SCM_REVISION}")
if(SCM_REVISION)
  message(STATUS "Building development version from Git clone.")
else()
  message(STATUS "Building released version.")
endif()

###########################################################
# Python Support: Detect and report all stuff here.       #
# Subdirectories can use: ALL_PYTHON_FOUND                #
###########################################################
option(ENABLE_PYTHON "Enable python support" ON)
if(ENABLE_PYTHON)
  include(PythonDeps)
  if(ALL_PYTHON_FOUND)
    message(STATUS "All python dependencies found - Python support enabled")
  else()
    set(ENABLE_PYTHON FALSE)
    message(WARNING "Not all python dependencies are found - Python support disabled")
  endif()
else()
  message(STATUS "Python support disabled.")
endif()

# Unit testing, mainly useful for developers and so defaults to off
option(ENABLE_TESTS "Enable unit testing" OFF)
if(ENABLE_TESTS)
  set(QT_USE_QTTEST TRUE)
  enable_testing()
  include (CTest)
  message(STATUS "Unit testing enabled, run make test to run them")
endif()

# GLSL support is needed for OpenGL shaders
option(ENABLE_GLSL "Enable GLSL support" OFF)
if(ENABLE_GLSL)
  find_package(GLEW)
  if(GLEW_FOUND)
    message(STATUS "GLEW found and GLSL support enabled")
    add_definitions( -DENABLE_GLSL )
  else()
    message(WARNING "GLEW not found, please install it if you require GLSL")
  endif()
endif()

# Multithreaded OpenGL. Seems mostly broken on Linux right now.
option(ENABLE_THREADEDGL "Enable threaded OpenGL rendering" OFF)
if(ENABLE_THREADEDGL)
  add_definitions( -DENABLE_THREADED_GL )
  set(THREADED_GL true)
  message(STATUS "Threaded OpenGL rendering enabled - needs more testing")
else()
  set(THREADED_GL false)
  message(STATUS "Threaded OpenGL rendering not enabled")
endif()

# Should a self-contained package be created?
option(ENABLE_AVO_PACKAGE "Enable creation of a self-contained package" OFF)
if(ENABLE_AVO_PACKAGE)
  add_definitions(-DAVO_APP_BUNDLE)
endif()

# Should a versioned plugin directory be used, i.e. avogadro/1.0.
option(ENABLE_VERSIONED_PLUGIN_DIR "Enable versioned plugin directory" ON)
mark_as_advanced(ENABLE_VERSIONED_PLUGIN_DIR)

# Set up our path handling, inspired by the path handling used in KDE
set(LIB_SUFFIX "" CACHE STRING "Suffix of the directory name, e.g. 64 for lib64")
set(SHARE_INSTALL_PREFIX "share"
    CACHE STRING "Base directory for shared data")
set(BIN_INSTALL_DIR      "bin"
    CACHE STRING "Install dir for binaries")

# Set up the plugin directory
if(ENABLE_VERSIONED_PLUGIN_DIR)
  set(Plugin_Version "/${Avogadro_VERSION_MAJOR}_${Avogadro_VERSION_MINOR}")
else()
  set(Plugin_Version "")
endif()

if(UNIX)
  set(LIB_INSTALL_DIR      "lib${LIB_SUFFIX}"
      CACHE STRING "Install dir for libraries" FORCE)
  set(Avogadro_PLUGIN_INSTALL_DIR
      "${LIB_INSTALL_DIR}/avogadro${Plugin_Version}"
      CACHE STRING "Install dir for plugins" FORCE)
else()
  set(LIB_INSTALL_DIR      "${BIN_INSTALL_DIR}"
      CACHE STRING "Install dir for libraries" FORCE)
  set(Avogadro_PLUGIN_INSTALL_DIR
      "lib${LIB_SUFFIX}/avogadro${Plugin_Version}"
      CACHE STRING "Install dir for plugins" FORCE)
endif()
set(INCLUDE_INSTALL_DIR  "include"
    CACHE STRING "Install dir for include files")
set(I18N_INSTALL_DIR     "${SHARE_INSTALL_PREFIX}/avogadro/i18n"
    CACHE STRING "Install dir for i18n files")
set(XDG_APPS_INSTALL_DIR "${SHARE_INSTALL_PREFIX}/applications"
    CACHE STRING "Install dir for application files")
set(XDG_ICON_INSTALL_DIR "${SHARE_INSTALL_PREFIX}/pixmaps"
    CACHE STRING "Install dir for application icons")

# Some rpath handling for Linux and Mac
option(ENABLE_RPATH "Enable rpath support on Linux and Mac" ON)
if(UNIX AND ENABLE_RPATH)
  message(STATUS "RPath support enabled for installed binaries and libraries")
  if(APPLE)
    # Funky path logic in order to get the Avogadro application bundle working
    set(CMAKE_BUNDLE_LOCATION "/Applications")
    if(USE_VERSIONED_BUNDLE_LOCATION)
      set(CMAKE_BUNDLE_NAME "Avogadro${Avogadro_VERSION}")
    else(USE_VERSIONED_BUNDLE_LOCATION)
      set(CMAKE_BUNDLE_NAME "Avogadro")
    endif(USE_VERSIONED_BUNDLE_LOCATION)
    set(CMAKE_INSTALL_PREFIX "/Applications/${CMAKE_BUNDLE_NAME}.app/Contents")
    set(CMAKE_INSTALL_NAME_DIR "@executable_path/../${LIB_INSTALL_DIR}")
  else()
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${LIB_INSTALL_DIR}")
    set(CMAKE_SKIP_BUILD_RPATH FALSE)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
  endif()
endif()

# Add support for an uninstall target
configure_file(
  "${CMAKE_MODULE_PATH}/cmake_uninstall.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
  IMMEDIATE @ONLY)

add_custom_target(uninstall
  "${CMAKE_COMMAND}" -P
  "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake")

# Configure and install an Avogadro CMake style configuration file
configure_file(
  "${CMAKE_MODULE_PATH}/AvogadroConfig.cmake.in"
  "${Avogadro_BINARY_DIR}/AvogadroConfig.cmake"
  IMMEDIATE @ONLY)

configure_file(
  "${CMAKE_MODULE_PATH}/AvogadroConfigVersion.cmake.in"
  "${Avogadro_BINARY_DIR}/AvogadroConfigVersion.cmake"
  IMMEDIATE @ONLY)

if(APPLE)
  install(FILES
    "${Avogadro_BINARY_DIR}/AvogadroConfig.cmake"
    "${Avogadro_BINARY_DIR}/AvogadroConfigVersion.cmake"
    DESTINATION Resources)
else()
  install(FILES
    "${Avogadro_BINARY_DIR}/AvogadroConfig.cmake"
    "${Avogadro_BINARY_DIR}/AvogadroConfigVersion.cmake"
    DESTINATION "${LIB_INSTALL_DIR}/avogadro")
endif()

if (UNIX)
  if(ENABLE_AVO_PACKAGE)
    if(APPLE)
      # Make sure to copy Qt plugins and translations
      file(GLOB qt_TRANSLATIONS "${QT_TRANSLATIONS_DIR}/qt*.qm")
      install(FILES ${qt_TRANSLATIONS}
        DESTINATION "${I18N_INSTALL_DIR}"
        )
      # FIXME: These need to have the install-name tool run on them
      install(DIRECTORY "${QT_PLUGINS_DIR}"
        DESTINATION "${CMAKE_INSTALL_PREFIX}"
        )
    else()
#      # Make sure to copy Qt plugins and translations
#      file(GLOB qt_TRANSLATIONS "${QT_TRANSLATIONS_DIR}/qt*.qm")
#      install(FILES ${qt_TRANSLATIONS}
#    	DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/qt/translations"
#    	)
#      # FIXME: These need to have the install-name tool run on them
#      install(DIRECTORY "${QT_PLUGINS_DIR}"
#    	DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/qt"
#    	)
#      file(GLOB qt_libs "${QT_LIBRARY_DIR}/libQtCore.so* ${QT_LIBRARY_DIR}/libQtGui.so* ${QT_LIBRARY_DIR}/libQtOpenGL.so* ${QT_LIBRARY_DIR}/libQtNetwork.so*")
#      install(FILES ${qt_libs}
#        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/qt"
#        )
#      # FIXME: Add others
#      set(X11_LIBRARIES ${X11_ICE_LIB} ${X11_SM_LIB} ${X11_X11_LIB})
#      install(LIBRARY ${X11_LIBRARIES}
#        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/X11"
#        )
#      install(LIBRARY ${OPENGL_LIBRARIES}
#        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/gl"
#        )
#      install (LIBRARY ${ZLIB_LIBRARY}
#        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib"
#        )
       set(about_files ${CMAKE_CURRENT_SOURCE_DIR}/README
                       ${CMAKE_CURRENT_SOURCE_DIR}/COPYING
                       ${CMAKE_CURRENT_SOURCE_DIR}/AUTHORS
         )
       install(FILES ${about_files}
         DESTINATION "${CMAKE_INSTALL_PREFIX}"
         )
    endif()
  endif()
endif()

# Install the configuration files to help find Avogadro
install(FILES
  "${CMAKE_MODULE_PATH}/AvogadroUse.cmake"
  DESTINATION "${Avogadro_PLUGIN_INSTALL_DIR}")

# Install the find modules we require to be present
install(FILES
  "${CMAKE_MODULE_PATH}/FindEigen2.cmake"
  "${CMAKE_MODULE_PATH}/FindEigen3.cmake"
  "${CMAKE_MODULE_PATH}/FindGLEW.cmake"
  DESTINATION "${Avogadro_PLUGIN_INSTALL_DIR}/cmake")

configure_file(
  "${CMAKE_MODULE_PATH}/CTestCustom.cmake.in"
  "${Avogadro_BINARY_DIR}/CTestCustom.cmake"
  IMMEDIATE @ONLY
)

# Place binaries and libraries in the root of the build directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    CACHE STRING "Binary build output directory")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib${LIB_SUFFIX}"
    CACHE STRING "Library build output directory")
mark_as_advanced(CMAKE_LIBRARY_OUTPUT_DIRECTORY
  CMAKE_RUNTIME_OUTPUT_DIRECTORY)

# tell cmake to process CMakeLists.txt in that subdirectory
add_subdirectory(libavogadro)
add_subdirectory(avogadro)
add_subdirectory(doc)

# Linux/Windows default: ${PREFIX}/share/avogadro/fragments
set(avogadro_FRAGMENTS ${SHARE_INSTALL_PREFIX}/avogadro)

# When installing, make sure to install some fragments
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/fragments"
        DESTINATION ${avogadro_FRAGMENTS})
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/builder"
        DESTINATION ${avogadro_FRAGMENTS})
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/crystals"
        DESTINATION ${avogadro_FRAGMENTS})

# Prefix to use in avopkg, pkg-config and QMake integration
if(ENABLE_AVO_PACKAGE)
  set(USER_PREFIX "@prefix@")
else()
  set(USER_PREFIX ${CMAKE_INSTALL_PREFIX})
endif()

# pkg-config
if(UNIX)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/avogadro.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/avogadro.pc" @ONLY)
  set(PC_FILES "${CMAKE_CURRENT_BINARY_DIR}/avogadro.pc")
  install(FILES ${PC_FILES} DESTINATION ${LIB_INSTALL_DIR}/pkgconfig)
endif()

# avopkg
if(UNIX)
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/avopkg.in"
    "${CMAKE_CURRENT_BINARY_DIR}/avopkg" @ONLY)
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/avopkg"
    DESTINATION ${BIN_INSTALL_DIR}
    PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE
    WORLD_READ WORLD_EXECUTE)
endif()

# .prf file for QMake projects
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/avogadro.prf.in"
  "${CMAKE_CURRENT_BINARY_DIR}/avogadro.prf" @ONLY)

option(INSTALL_QMAKE_PRF "Install QMake integration files to Qt prefix" ON)
if(QT_MKSPECS_DIR AND INSTALL_QMAKE_PRF)
#  if ("${QT_MKSPECS_DIR}" MATCHES ^$"${CMAKE_INSTALL_PREFIX}/")
#    string(REPLACE
#      "${CMAKE_INSTALL_PREFIX}"
#      ""
#      QT_MKSPECS_RELATIVE
#      "${QT_MKSPECS_DIR}"
#    )
    install(FILES
      "${CMAKE_CURRENT_BINARY_DIR}/avogadro.prf"
      DESTINATION "${CMAKE_INSTALL_PREFIX}/${QT_MKSPECS_RELATIVE}/features"
    )
#  endif ("${QT_MKSPECS_DIR}" MATCHES ^$"${CMAKE_INSTALL_PREFIX}/")
else()
  message(WARNING "Qt mkspecs directory not found. File 'avogadro.prf' will be installed to ${SHARE_INSTALL_PREFIX}/avogadro/qmake.")
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/avogadro.prf"
    DESTINATION "${SHARE_INSTALL_PREFIX}/avogadro/qmake"
  )
endif()

