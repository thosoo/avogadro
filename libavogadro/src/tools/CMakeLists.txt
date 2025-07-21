add_definitions(${QT_DEFINITIONS})
add_definitions(-DQT_PLUGIN)
add_definitions(-DQT_SHARED)

set(DESTINATION_DIR ${Avogadro_PLUGIN_INSTALL_DIR}/tools)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(LINK_LIBS avogadro)
option(ENABLE_XTB_OPTTOOL "Build xTB optimization tool" ON)
if(ENABLE_XTB_OPTTOOL)
  find_package(XTB QUIET)
  if(XTB_FOUND)
    include_directories(${XTB_INCLUDE_DIRS})
    if(XTB_LIBRARY_DIRS)
      link_directories(${XTB_LIBRARY_DIRS})
    endif()
    set(LINK_LIBS ${LINK_LIBS} XTB::xtb)
    set(ENABLE_XTB_OPTTOOL TRUE)
  else()
    message(STATUS "xTB not found - xtbopttool will not be built")
    set(ENABLE_XTB_OPTTOOL FALSE)
  endif()
endif()
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  set(LINK_LIBS ${LINK_LIBS} OpenMP::OpenMP_CXX)
endif()
set(PLUGIN_LABEL tools)
set(PLUGIN_TARGET tools)
add_custom_target(tools COMMENT "Meta target to build all Avogadro tools.")

### drawtool
set(drawtool_SRCS
  drawtool.cpp
  drawcommand.cpp)
avogadro_plugin(drawtool
  "${drawtool_SRCS}"
  drawtool.qrc)

option(ENABLE_ZMATRIX_TOOL "Enable the z-matrix tool (experimental)" OFF)
if(ENABLE_ZMATRIX_TOOL)
  ### zmatrixtool
  set(zmatrixtool_SRCS zmatrixtool.cpp zmatrixdialog.cpp zmatrixmodel.cpp)
  avogadro_plugin(zmatrixtool
    "${zmatrixtool_SRCS}"
    zmatrixdialog.ui;zmatrixtool.qrc)
endif(ENABLE_ZMATRIX_TOOL)

### bondcentrictool
set(bondcentrictool_SRCS bondcentrictool.cpp skeletontree.cpp)
avogadro_plugin(bondcentrictool
  "${bondcentrictool_SRCS}"
  bondcentrictool.qrc)

### clickmeasuretool
avogadro_plugin(clickmeasuretool clickmeasuretool.cpp clickmeasuretool.qrc)

### selectrotatetool
avogadro_plugin(selectrotatetool selectrotatetool.cpp selectrotatetool.qrc)

### autoopttool
avogadro_plugin(autoopttool autoopttool.cpp autoopttool.qrc)
if(${CMAKE_CXX_COMPILER_ID} MATCHES Intel AND UNIX)
  set_target_properties(autoopttool PROPERTIES COMPILE_FLAGS "-fvisibility=default")
endif()

### xtbopttool
if(ENABLE_XTB_OPTTOOL)
  avogadro_plugin(xtbopttool xtbopttool.cpp xtbopttool.qrc)
  target_link_libraries(xtbopttool ${LINK_LIBS})
endif()

### navigatetool
avogadro_plugin(navigatetool
  "navigatetool.cpp;eyecandy.cpp" "navigatesettingswidget.ui;navigatetool.qrc")

### manipulatetool
avogadro_plugin(manipulatetool
 "manipulatetool.cpp;eyecandy.cpp;moveatomcommand.cpp" "manipulatesettingswidget.ui;manipulatetool.qrc")

### autorotatetool
avogadro_plugin(autorotatetool autorotatetool.cpp autorotatetool.qrc)

### aligntool
avogadro_plugin(aligntool aligntool.cpp aligntool.qrc)

if(ALL_PYTHON_FOUND)
  file(GLOB python_SCRIPTS "python/*.py")
  install(FILES ${python_SCRIPTS} DESTINATION ${SHARE_INSTALL_PREFIX}/libavogadro/toolScripts)
endif()
