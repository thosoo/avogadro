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
  find_path(libxml2_DIR "libxml-2.0.pc.in" PATHS
      "C:/src/libxml2-2.12.10"
      "C:/src/libxml2"
  )
  find_file(libxml2_DLL "libxml2.dll" PATHS
    "${libxml2_DIR}/bin"
    "${libxml2_DIR}/lib"
    "${CMAKE_PREFIX_PATH}/bin"
    "${CMAKE_PREFIX_PATH}/lib"
  )
  install(FILES ${libxml2_DLL} DESTINATION bin)

  ##############################################
  # OpenBabel                                  #
  ##############################################
  find_path(openbabel_SRCDIR "openbabel-3.pc.in" PATHS
      "C:/src/openbabel"
  )
  find_path(openbabel_BINDIR "openbabel.dll" PATHS
      "${CMAKE_PREFIX_PATH}/bin"
      "${openbabel_SRCDIR}/output/Release"
      "${openbabel_SRCDIR}/build/src/Release"
      "${openbabel_SRCDIR}/src/Release"
      "${openbabel_SRCDIR}/Release"
      "${openbabel_SRCDIR}"
  )

  # Data files needed by OpenBabel
  if(openbabel_SRCDIR)
    file(GLOB openbabel_FILES "${openbabel_SRCDIR}/data/*.txt")
    install(FILES ${openbabel_FILES} DESTINATION bin)
    file(GLOB openbabel_FILES "${openbabel_SRCDIR}/data/*.par")
    install(FILES ${openbabel_FILES} DESTINATION bin)
    file(GLOB openbabel_FILES "${openbabel_SRCDIR}/data/*.prm")
    install(FILES ${openbabel_FILES} DESTINATION bin)
    file(GLOB openbabel_FILES "${openbabel_SRCDIR}/data/*.ff")
    install(FILES ${openbabel_FILES} DESTINATION bin)
    file(GLOB openbabel_FILES "${openbabel_SRCDIR}/data/*.dat")
    install(FILES ${openbabel_FILES} DESTINATION bin)
  else()
    # Should be able to find them in the installed tree too
    file(GLOB openbabel_FILES "${openbabel_BINDIR}/data/*.txt")
    install(FILES ${openbabel_FILES} DESTINATION bin)
    file(GLOB openbabel_FILES "${openbabel_BINDIR}/data/*.par")
    install(FILES ${openbabel_FILES} DESTINATION bin)
    file(GLOB openbabel_FILES "${openbabel_BINDIR}/data/*.prm")
    install(FILES ${openbabel_FILES} DESTINATION bin)
    file(GLOB openbabel_FILES "${openbabel_BINDIR}/data/*.ff")
    install(FILES ${openbabel_FILES} DESTINATION bin)
    file(GLOB openbabel_FILES "${openbabel_BINDIR}/data/*.dat")
    install(FILES ${openbabel_FILES} DESTINATION bin)
  endif()

  set(openbabel_DLLs
      "${openbabel_BINDIR}/openbabel.dll"
      "${openbabel_BINDIR}/inchi.dll")
  install(FILES ${openbabel_DLLs} DESTINATION bin)

  file(GLOB openbabel_FORMATS "${openbabel_BINDIR}/*.obf")
  install(FILES ${openbabel_FORMATS} DESTINATION bin)

  ##############################################
  # Qt                                         #
  ##############################################
  get_filename_component(QT_BIN_DIR ${QT_QMAKE_EXECUTABLE} PATH)
  find_path(qt_BINDIR "Qt5Core.dll" PATH ${QT_BIN_DIR})
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
        "C:/src/glew/bin"
    )
    install(FILES ${glew_DLL} DESTINATION bin)
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
      "C:/src/Python-3.11"
  )
  find_file(python_DLL "python311.dll" PATHS
      "${python_DIR}/Libs"
      "${python_DIR}/DLLs"
      "${python_DIR}/bin"
      "${python_DIR}/PCbuild"
  )
    install(FILES ${python_DLL} DESTINATION bin)

    #
    # boost python
    #
  find_path(boost_DIR "boost.png" PATHS
      "C:/src/boost_1_83_0"
      "C:/src/boost_1_82_0"
  )
  find_file(boost_python_DLL "boost_python311-vc143-mt.dll" PATHS
      "${boost_DIR}/lib/"
      "${boost_DIR}/bin/"
  )
    install(FILES ${boost_python_DLL} DESTINATION bin)

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
    set(pyqt_DEPS
      "${pyqt_DIR}/__init__.py"
      "${pyqt_DIR}/Qt/Qt.pyd"
      "${pyqt_DIR}/QtCore/QtCore.pyd"
      "${pyqt_DIR}/QtGui/QtGui.pyd"
      "${pyqt_DIR}/QtOpenGL/QtOpenGL.pyd"
      "${pyqt_DIR}/QtCore/QtCore.pyd")
    install(FILES ${pyqt_DEPS} DESTINATION bin/lib/site-packages/PyQt5)
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

  # Ensure Qt runtime libraries and plugins are installed
  install(CODE "execute_process(\n    COMMAND windeployqt --release --dir \"${CMAKE_INSTALL_PREFIX}/bin\" \"${CMAKE_INSTALL_PREFIX}/bin/Avogadro.exe\"\n  )")

endif()

if(APPLE)
  set(CMAKE_OSX_ARCHITECTURES "ppc;i386")
endif(APPLE)

configure_file("${CMAKE_MODULE_PATH}/AvoCPackOptions.cmake.in"
  "${CMAKE_BINARY_DIR}/AvoCPackOptions.cmake" @ONLY)
set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_BINARY_DIR}/AvoCPackOptions.cmake")

include(CPack)
include(InstallRequiredSystemLibraries)
