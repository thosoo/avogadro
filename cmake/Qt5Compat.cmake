# Qt5 compatibility helpers for legacy Qt4 build files
if(NOT Qt5_FOUND)
  find_package(Qt5 REQUIRED COMPONENTS Widgets Gui OpenGL Network LinguistTools Core)
endif()

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

set(QT_LIBRARIES Qt5::Widgets Qt5::Gui Qt5::OpenGL Qt5::Network Qt5::Core)
set(QT_QTNETWORK_LIBRARY Qt5::Network)
set(QT_DEFINITIONS "")

function(qt4_wrap_cpp outfiles)
  qt5_wrap_cpp(${outfiles} ${ARGN})
endfunction()

function(qt4_wrap_ui outfiles)
  qt5_wrap_ui(${outfiles} ${ARGN})
endfunction()

function(qt4_add_resources outfiles)
  qt5_add_resources(${outfiles} ${ARGN})
endfunction()

function(qt4_automoc)
  # automoc enabled globally
endfunction()
