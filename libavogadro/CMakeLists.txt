project(libavogadro)
# Library versioning
# We probably want to adopt the libtool scheme once we hit 1.0.0
set(AVO_SOVERSION 1)

if(WIN32)
  add_definitions( -DWIN32 )
  # add definitions for OB in WIN32
  add_definitions( -DUSING_OBDLL -DUSING_DYNAMIC_LIBS )
endif()

# Provide a list of plugins that should be built statically, or ALL
# Disable static plugins by default to simplify dynamic plugin builds.
set(Avogadro_STATIC_PLUGINS ""
  CACHE STRING
  "List of plugins to build statically. Use ALL to build all plugins statically.")

# Create an empty variable to accumulate statically linked plugins in
set(plugins_built_static "" CACHE INTERNAL "Static Avogadro plugins")
set(plugins_rcc_static "" CACHE INTERNAL "Static resources for Avogadro plugins")

# Use this function to add a new plugin. It also uses the directory variable
# LINK_LIBS to determine extra libraries the plugin should link to and
# DESTINATION_DIR to determine where the plugin will be installed.
function(avogadro_plugin_nogl plugin_name src_list)
  set(plugin_sources ${src_list} ${ARGN})
  foreach(source_file ${plugin_sources})
    if("${source_file}" MATCHES "\\.cpp$")
      get_filename_component(source_dir "${source_file}" DIRECTORY)
      get_filename_component(source_base "${source_file}" NAME_WE)
      foreach(header_suffix .h .hpp _p.h)
        if(source_dir)
          set(candidate_header "${source_dir}/${source_base}${header_suffix}")
        else()
          set(candidate_header "${source_base}${header_suffix}")
        endif()
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${candidate_header}")
          list(APPEND plugin_sources "${candidate_header}")
        endif()
      endforeach()
    endif()
  endforeach()
  list(REMOVE_DUPLICATES plugin_sources)

  # Check if the plugin should be compiled static or not.
  set(_plugin_type "MODULE")
  foreach(plugin ${Avogadro_STATIC_PLUGINS})
    if("${plugin}" STREQUAL "${plugin_name}" OR "${plugin}" STREQUAL "ALL")
      set(_plugin_type "STATIC")
      set(plugins_built_static ${plugins_built_static} ${plugin_name}
        CACHE INTERNAL "Static Avogadro plugins")
      foreach(fileName ${plugin_sources})
        if("${fileName}" MATCHES "\\.qrc$")
          set(plugins_rcc_static ${plugins_rcc_static} ${plugin_name}
            CACHE INTERNAL "Static resources for Avogadro plugins")
          break()
        endif()
      endforeach()
      break()
    endif()
  endforeach()

  add_library(${plugin_name} ${_plugin_type} ${plugin_sources})
  set_target_properties(${plugin_name} PROPERTIES
    AUTOMOC_MOC_OPTIONS "-DBOOST_TT_HAS_OPERATOR_HPP_INCLUDED")
  set(_plugin_link_libs
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    OpenBabel3::openbabel
    ${LINK_LIBS})
  if("${_plugin_type}" STREQUAL "MODULE")
    list(PREPEND _plugin_link_libs avogadro)
  else()
    list(REMOVE_ITEM _plugin_link_libs avogadro)
  endif()
  if(AVO_USE_X11)
    list(APPEND _plugin_link_libs ${X11_X11_LIB})
  endif()
  target_link_libraries(${plugin_name} PRIVATE ${_plugin_link_libs})
  target_compile_definitions(${plugin_name} PRIVATE QT_PLUGIN QT_SHARED)
  if(AVO_USE_X11)
    target_compile_definitions(${plugin_name} PRIVATE AVO_USE_X11)
  endif()

  if("${_plugin_type}" STREQUAL "MODULE")
    install(TARGETS ${plugin_name} DESTINATION ${DESTINATION_DIR})
  else()
    target_compile_definitions(${plugin_name} PRIVATE QT_STATICPLUGIN)
    set_target_properties(${plugin_name} PROPERTIES
      COMPILE_FLAGS "${CMAKE_SHARED_LIBRARY_CXX_FLAGS}")
  endif()

  set_target_properties(${plugin_name} PROPERTIES
                        OUTPUT_NAME ${plugin_name}
                        PREFIX ""
                        LABELS "${PLUGIN_LABEL}")
  # If we have set the PLUGIN_TARGET variable add this plugin to the custom
  # target. This assumes that the target was created before this function was
  # called.
  if(PLUGIN_TARGET)
    add_dependencies(${PLUGIN_TARGET} ${plugin_name})
  endif()
endfunction()

function(avogadro_plugin plugin_name src_list)
  avogadro_plugin_nogl("${plugin_name}" "${src_list}" ${ARGN})
  target_link_libraries(${plugin_name} PRIVATE OpenGL::OpenGL OpenGL::GLU)
endfunction()

# tell cmake to process CMakeLists.txt in that subdirectory
add_subdirectory(src)

if(ENABLE_TESTS)
  add_subdirectory(tests)
endif()

