# Qt5 compatibility helpers for legacy Qt4 build files
if(NOT Qt5_FOUND)
  find_package(Qt5 REQUIRED COMPONENTS Widgets Gui OpenGL Network LinguistTools Core)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(QT_LIBRARIES Qt5::Widgets Qt5::Gui Qt5::OpenGL Qt5::Network Qt5::Core Qt5::Concurrent)
set(QT_QTCORE_LIBRARY Qt5::Core)
set(QT_QTGUI_LIBRARY Qt5::Gui)
set(QT_QTOPENGL_LIBRARY Qt5::OpenGL)
set(QT_QTNETWORK_LIBRARY Qt5::Network)
set(QT_DEFINITIONS "")
set(QT_INCLUDES
  ${Qt5Widgets_INCLUDE_DIRS}
  ${Qt5Gui_INCLUDE_DIRS}
  ${Qt5OpenGL_INCLUDE_DIRS}
  ${Qt5Network_INCLUDE_DIRS}
  ${Qt5Core_INCLUDE_DIRS}
)
include_directories(${QT_INCLUDES})

function(qt4_wrap_cpp outfiles)
  qt5_wrap_cpp("${outfiles}" ${ARGN})
  set(${outfiles} ${${outfiles}} PARENT_SCOPE)
endfunction()

function(qt4_wrap_ui outfiles)
  qt5_wrap_ui("${outfiles}" ${ARGN})
  set(${outfiles} ${${outfiles}} PARENT_SCOPE)
endfunction()

function(qt4_add_resources outfiles)
  qt5_add_resources("${outfiles}" ${ARGN})
  set(${outfiles} ${${outfiles}} PARENT_SCOPE)
endfunction()

function(qt4_automoc)
  # automoc enabled globally
endfunction()
