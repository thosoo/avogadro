# Set up CPack with the correct version, external libraries etc

set (CPACK_PACKAGE_NAME "Avogadro")
set (CPACK_PACKAGE_VERSION_MAJOR ${Avogadro_VERSION_MAJOR})
set (CPACK_PACKAGE_VERSION_MINOR ${Avogadro_VERSION_MINOR})
set (CPACK_PACKAGE_VERSION_PATCH ${Avogadro_VERSION_PATCH})
set (CPACK_PACKAGE_VERSION ${Avogadro_VERSION_MAJOR}.${Avogadro_VERSION_MINOR}.${Avogadro_VERSION_PATCH})
set (CPACK_PACKAGE_INSTALL_DIRECTORY "Avogadro")
set (CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/COPYING")

set (CPACK_PACKAGE_EXECUTABLES "avogadro" "Avogadro")
set (CPACK_CREATE_DESKTOP_LINKS "avogadro")

if(WIN32)
  option(ENABLE_DEPRECATED_INSTALL_RULES "Should deprecated, Windows specific, install rules be enabled?" OFF)

  # Deploy Qt runtime libraries before collecting DLLs
  install(CODE "
    set(_exe \"${CMAKE_INSTALL_PREFIX}/bin/avogadro.exe\")
    if(NOT EXISTS \"${_exe}\")
      set(_exe \"${CMAKE_INSTALL_PREFIX}/bin/Avogadro.exe\")
    endif()
    execute_process(COMMAND windeployqt --release --dir \"${CMAKE_INSTALL_PREFIX}/bin\" \"${_exe}\")
  ")
endif()
if (WIN32 AND ENABLE_DEPRECATED_INSTALL_RULES)
  # Set the directories to defaults if not set

  ##############################################
  # Zlib                                       #
  ##############################################
  find_file(zlib_DLL "zlib1.dll" PATHS
    ${CMAKE_PREFIX_PATH}/bin
    ${zlib_DIR}
  )
  install(FILES ${zlib_DLL} DESTINATION bin)

  ##############################################
  # libxml2                                    #
  ##############################################
  # libxml2 install directory may come via LIBXML2_LIBRARY or CMAKE_PREFIX_PATH
  if(NOT libxml2_DIR AND LIBXML2_LIBRARY)
    get_filename_component(libxml2_DIR "${LIBXML2_LIBRARY}" DIRECTORY)
    get_filename_component(libxml2_DIR "${libxml2_DIR}" DIRECTORY)
  endif()
  find_file(libxml2_DLL "libxml2.dll" PATHS
    "${libxml2_DIR}/bin"
    "${libxml2_DIR}/lib"
    "${CMAKE_PREFIX_PATH}/bin"
    "${CMAKE_PREFIX_PATH}/lib"
  )
  if(libxml2_DLL)
    install(FILES ${libxml2_DLL} DESTINATION bin)
  endif()

  ##############################################
  # OpenBabel                                  #
  ##############################################
  # OpenBabel install prefix may come from the workflow via OPENBABEL_INSTALL_DIR
  set(openbabel_PREFIX "$ENV{OPENBABEL_INSTALL_DIR}")
  if(NOT openbabel_PREFIX AND OPENBABEL3_LIBRARIES)
    get_filename_component(openbabel_PREFIX "${OPENBABEL3_LIBRARIES}" DIRECTORY)
    get_filename_component(openbabel_PREFIX "${openbabel_PREFIX}" DIRECTORY)
  endif()
  find_path(openbabel_BINDIR NAMES openbabel.dll openbabel-3.dll PATHS
      "${openbabel_PREFIX}/bin"
      "${CMAKE_PREFIX_PATH}/bin"
  )

  # Determine the OpenBabel prefix for data and plugin lookup
  get_filename_component(openbabel_PREFIX "${openbabel_BINDIR}" DIRECTORY)

  # Data files needed by OpenBabel
  if(EXISTS "${openbabel_PREFIX}/share/openbabel")
    install(DIRECTORY "${openbabel_PREFIX}/share/openbabel" DESTINATION share)
  endif()

  find_file(openbabel_DLL NAMES openbabel.dll openbabel-3.dll
            PATHS "${openbabel_BINDIR}")
  if(NOT openbabel_DLL)
    message(FATAL_ERROR "OpenBabel DLL not found in ${openbabel_BINDIR}")
  endif()
  install(FILES "${openbabel_DLL}" DESTINATION bin)

  # InChI support may be built statically with OpenBabel 3 so this DLL might not
  # be present. Install it only when found.
  find_file(inchi_DLL "inchi.dll" PATHS "${openbabel_BINDIR}")
  if(inchi_DLL)
    install(FILES "${inchi_DLL}" DESTINATION bin)
  endif()

  # Format plugins
  if(EXISTS "${openbabel_PREFIX}/lib/openbabel")
    file(GLOB openbabel_FORMATS "${openbabel_PREFIX}/lib/openbabel/*${CMAKE_SHARED_LIBRARY_SUFFIX}")
    install(FILES ${openbabel_FORMATS} DESTINATION bin)
  endif()

  ##############################################
  # Qt                                         #
  ##############################################
  get_filename_component(QT_BIN_DIR "${QT_QMAKE_EXECUTABLE}" PATH)
  find_path(qt_BINDIR Qt5Core.dll PATHS "${QT_BIN_DIR}")
  set(qt_LIBS
    Qt5Core.dll
    Qt5Gui.dll
    Qt5Widgets.dll
    Qt5OpenGL.dll
    Qt5Network.dll)
  foreach(_lib IN LISTS qt_LIBS)
    find_file(_dll "${_lib}" PATHS "${qt_BINDIR}")
    if(_dll)
      install(FILES "${_dll}" DESTINATION bin)
    endif()
  endforeach()

  ##############################################
  # GLSL shaders (Optional)                    #
  ##############################################
  if(ENABLE_GLSL AND GLEW_FOUND)
    find_file(glew_DLL "glew32.dll" PATHS
        "$ENV{GLEW_DIR}/bin"
        "${CMAKE_PREFIX_PATH}/bin"
        "C:/src/glew/bin"
    )
    if(glew_DLL)
      install(FILES ${glew_DLL} DESTINATION bin)
    endif()
  endif()

  ##############################################
  # Python (Optional)                          #
  ##############################################
  if(ENABLE_PYTHON AND ALL_PYTHON_FOUND)
    # Python support - optionally enabled and installed

    #
    # python library
    #
  find_path(python_DIR "pyconfig.h.in" PATHS
      "$ENV{PYTHON_DIR}"
      "C:/src/Python-3.11"
  )
  if(NOT python_DIR AND PYTHON_LIBRARY)
    get_filename_component(python_DIR "${PYTHON_LIBRARY}" DIRECTORY)
    get_filename_component(python_DIR "${python_DIR}" DIRECTORY)
  endif()
  find_file(python_DLL "python311.dll" PATHS
      "${python_DIR}/Libs"
      "${python_DIR}/DLLs"
      "${python_DIR}/bin"
      "${python_DIR}/PCbuild"
  )
  if(python_DLL)
    install(FILES ${python_DLL} DESTINATION bin)
  endif()

    #
    # boost python
    #
  find_path(boost_DIR "boost.png" PATHS
      "$ENV{BOOST_ROOT}"
      "C:/src/boost_1_83_0"
      "C:/src/boost_1_82_0"
  )
  find_file(boost_python_DLL "boost_python311-vc143-mt.dll" PATHS
      "${boost_DIR}/lib/"
      "${boost_DIR}/bin/"
  )
  if(boost_python_DLL)
    install(FILES ${boost_python_DLL} DESTINATION bin)
  endif()

    # lib/*: (includes all sip & numpy runtime files needed)
    file(GLOB python_lib_FILES "${python_DIR}/lib/*.py")
    install(FILES ${python_lib_FILES} DESTINATION bin/lib)
    # lib/encodings/*.py
    file(GLOB python_lib_encodings_FILES "${python_DIR}/lib/encodings/*.py")
    install(FILES ${python_lib_encodings_FILES} DESTINATION bin/lib/encodings)
    #
    # sip
    #
    set(python_lib_sip_FILES
        "${python_DIR}/lib/site-packages/sip.pyd"
        "${python_DIR}/lib/site-packages/sipconfig.py"
    )
    install(FILES ${python_lib_sip_FILES} DESTINATION bin/lib/site-packages)
    #
    # numpy
    #
    install(DIRECTORY ${python_DIR}/lib/site-packages/numpy DESTINATION bin/lib/site-packages)
    #
    # PyQt5
    #
    find_path(pyqt_DIR "pyqtconfig.py.in" PATHS
        "C:/src/PyQt5-5.15"
    )
    if(pyqt_DIR)
      set(pyqt_DEPS
        "${pyqt_DIR}/__init__.py"
        "${pyqt_DIR}/Qt/Qt.pyd"
        "${pyqt_DIR}/QtCore/QtCore.pyd"
        "${pyqt_DIR}/QtGui/QtGui.pyd"
        "${pyqt_DIR}/QtOpenGL/QtOpenGL.pyd"
        "${pyqt_DIR}/QtCore/QtCore.pyd")
      foreach(_dep IN LISTS pyqt_DEPS)
        if(EXISTS "${_dep}")
          install(FILES "${_dep}" DESTINATION bin/lib/site-packages/PyQt5)
        endif()
      endforeach()
    endif()
    #
    # Avogadro python module
    #
    install(FILES ${Avogadro_BINARY_DIR}/lib/_Avogadro.pyd DESTINATION bin/lib/site-packages)
    install(FILES ${Avogadro_SOURCE_DIR}/libavogadro/src/python/Avogadro.py DESTINATION bin/lib/site-packages)
    #
    # Avogadro plugin scripts
    #
    file(GLOB toolScripts "${Avogadro_SOURCE_DIR}/libavogadro/src/tools/python/*.py")
    install(FILES ${toolScripts} DESTINATION bin/toolScripts)
    file(GLOB engineScripts "${Avogadro_SOURCE_DIR}/libavogadro/src/engines/python/*.py")
    install(FILES ${engineScripts} DESTINATION bin/engineScripts)
    file(GLOB extensionScripts "${Avogadro_SOURCE_DIR}/libavogadro/src/extensions/python/*.py")
    install(FILES ${extensionScripts} DESTINATION bin/extensionScripts)

  endif()


endif()

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "ppc;i386")
endif(APPLE)

configure_file("${CMAKE_MODULE_PATH}/AvoCPackOptions.cmake.in"
  "${CMAKE_BINARY_DIR}/AvoCPackOptions.cmake" @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_BINARY_DIR}/AvoCPackOptions.cmake")

include(CPack)
include(InstallRequiredSystemLibraries)
