# Legacy consumer helper kept for third-party plugin projects. Prefer linking
# to exported Avogadro CMake targets directly when possible.
include(CMakeFindDependencyMacro)
find_dependency(Qt6 COMPONENTS Core Gui Widgets OpenGL OpenGLWidgets)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Set up the include directories and link directories for older consumers.
include_directories(${Avogadro_INCLUDE_DIRS})
link_directories(${Avogadro_LIBRARY_DIRS})

# Add the Avogadro modules directory to the CMake module path.
set(CMAKE_MODULE_PATH ${Avogadro_PLUGIN_DIR}/cmake ${CMAKE_MODULE_PATH})

find_package(Eigen3)
if(EIGEN3_FOUND)
  include_directories(${EIGEN3_INCLUDE_DIR})
else()
  message("Eigen3 not found. Trying to find Eigen2...")
  find_package(Eigen2 REQUIRED)
  include_directories(${EIGEN2_INCLUDE_DIR})
endif()

if(Avogadro_ENABLE_GLSL)
  find_package(GLEW)
  if(GLEW_FOUND)
    include_directories(${GLEW_INCLUDE_DIR})
    add_definitions(-DENABLE_GLSL)
  endif()
endif()

# Add a third-party Avogadro plugin. Pass source, .ui, and .qrc files directly;
# CMAKE_AUTOMOC, CMAKE_AUTOUIC, and CMAKE_AUTORCC process them.
function(avogadro_plugin plugin_name src_list)
  add_library(${plugin_name} MODULE ${src_list} ${ARGN})
  target_include_directories(${plugin_name} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(${plugin_name}
    PRIVATE
      avogadro
      Qt6::Core
      Qt6::Gui
      Qt6::Widgets
      Qt6::OpenGL
      Qt6::OpenGLWidgets)
  target_compile_definitions(${plugin_name} PRIVATE QT_PLUGIN QT_SHARED)

  if(UNIX)
    add_custom_target("${plugin_name}.mf"
      COMMAND avopkg -wizard "${plugin_name}"
    )
    add_custom_target("${plugin_name}.manifest"
      DEPENDS "${plugin_name}.mf"
    )
    add_custom_target("${plugin_name}.avo"
      COMMAND avopkg -pack "${plugin_name}.mf"
    )
    add_custom_target("${plugin_name}.package"
      DEPENDS "${plugin_name}.avo"
    )
    add_custom_target("${plugin_name}.install_package"
      COMMAND avopkg "${plugin_name}.avo"
      DEPENDS "${plugin_name}.avo"
    )
  endif()

  install(TARGETS ${plugin_name} DESTINATION "${Avogadro_PLUGIN_DIR}/contrib")

  set_target_properties(${plugin_name} PROPERTIES
    OUTPUT_NAME ${plugin_name}
    PREFIX "")
endfunction()
