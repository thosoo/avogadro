include_directories(
  ${libavogadro_SOURCE_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR}
  ${OPENBABEL3_INCLUDE_DIR}
  ${Boost_INCLUDE_DIRS}
  ${Python3_INCLUDE_DIRS}
  ${NUMPY_INCLUDE_DIR}
  ${SIP_INCLUDE_DIR}
)

# use all cpp files in this directory
file(GLOB wrapper_SRCS "*.cpp")

add_library(python-module MODULE ${wrapper_SRCS})
set_target_properties(python-module PROPERTIES OUTPUT_NAME Avogadro)
set_target_properties(python-module PROPERTIES PREFIX "")
if(WIN32)
  set_target_properties(python-module PROPERTIES SUFFIX ".pyd")
  target_link_libraries(python-module PRIVATE
    avogadro
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Python3::Python)
else()
  target_link_libraries(python-module PRIVATE
    stdc++
    avogadro
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Python3::Python
    ${Boost_LIBRARIES}
    OpenBabel3::openbabel)
endif()

execute_process(COMMAND ${Python3_EXECUTABLE} -c
  "import sysconfig; print(sysconfig.get_paths().get('platlib') or sysconfig.get_paths().get('purelib'), end='')"
  OUTPUT_VARIABLE PYTHON_LIB_PATH)

message(STATUS "Python site-packages directory: ${PYTHON_LIB_PATH}")

option(INSTALL_PYTHON_MODULE
  "Install the avogadro python module for the system wide python installation."
  ON)
if(INSTALL_PYTHON_MODULE)
  install(TARGETS python-module DESTINATION ${PYTHON_LIB_PATH})
endif()
