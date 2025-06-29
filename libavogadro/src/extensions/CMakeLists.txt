ADD_DEFINITIONS(${QT_DEFINITIONS})
ADD_DEFINITIONS(-DQT_PLUGIN)
ADD_DEFINITIONS(-DQT_SHARED)

set(DESTINATION_DIR ${Avogadro_PLUGIN_INSTALL_DIR}/extensions)

include_directories(${CMAKE_CURRENT_BINARY_DIR}
  "${CMAKE_CURRENT_SOURCE_DIR}/gl2ps"
)

set(LINK_LIBS avogadro)
set(PLUGIN_LABEL extensions)
set(PLUGIN_TARGET extensions)
add_custom_target(extensions COMMENT "Meta target to build all Avogadro extensions.")

### Template extension - template for extensions
#avogadro_plugin(templateextension templateextension.cpp)

### Properties
avogadro_plugin_nogl(propextension
  "../tools/skeletontree.cpp;propmodel.cpp;propextension.cpp")

avogadro_plugin_nogl(cartesianextension
  cartesianextension.cpp
  cartesianeditor.ui
  cartesianextension.qrc)

avogadro_plugin_nogl(molecularpropextension
  molecularpropextension.cpp
  molecularpropdialog.ui)
target_link_libraries(molecularpropextension ${QT_QTNETWORK_LIBRARY})

### Molecular Mechanics force fields
set(forcefieldextension_SRCS forcefieldextension.cpp forcefielddialog.cpp
  constraintsdialog.cpp constraintsmodel.cpp conformersearchdialog.cpp)
avogadro_plugin_nogl(forcefieldextension
  "${forcefieldextension_SRCS}"
  "forcefielddialog.ui;constraintsdialog.ui;conformersearchdialog.ui")
if(${CMAKE_CXX_COMPILER_ID} MATCHES Intel AND UNIX)
  set_target_properties(forcefieldextension PROPERTIES COMPILE_FLAGS "-fvisibility=default")
endif()

### hydrogens
avogadro_plugin_nogl(hydrogensextension hydrogensextension.cpp)

### invertchirality
avogadro_plugin_nogl(invertchiralextension invertchiralextension.cpp)

### Selections
avogadro_plugin_nogl(selectextension selectextension.cpp)

### insert fragment
avogadro_plugin_nogl(insertfragmentextension
  "insertfragmentextension.cpp;insertfragmentdialog.cpp;insertcommand.cpp;sortfiltertreeproxymodel.cpp"
  insertfragmentdialog.ui)

### insert peptide (builder)
avogadro_plugin_nogl(insertpeptideextension
  "insertpeptideextension.cpp;insertcommand.cpp"
  insertpeptidedialog.ui)

### insert dna (builder)
avogadro_plugin_nogl(insertdnaextension
  "insertdnaextension.cpp;insertcommand.cpp"
  insertdnadialog.ui)

### h->methyl
avogadro_plugin_nogl(h2methylextension h2methylextension.cpp)

if(ENABLE_PYTHON AND ALL_PYTHON_FOUND)
  ### python terminal
  message(STATUS "Enabled python terminal")
  avogadro_plugin_nogl(pythonterminal
    "pythonterminal.cpp;highlighter.cpp"
    pythonterminalwidget.ui)
  target_link_libraries(pythonterminal ${PYTHON_LIBRARIES})

  file(GLOB python_SCRIPTS "python/*.py")
  install(FILES ${python_SCRIPTS} DESTINATION ${SHARE_INSTALL_PREFIX}/libavogadro/extensionScripts)
endif()

### Animation
set(animationextension_SRCS animationextension.cpp animationdialog.cpp
  povpainter.cpp trajvideomaker.cpp)
avogadro_plugin_nogl(animationextension
  "${animationextension_SRCS}"
  animationdialog.ui
  animationextension.qrc)

### POV-Ray extension
avogadro_plugin_nogl(povrayextension
  "povrayextension.cpp;povpainter.cpp;povraydialog.cpp"
  povraydialog.ui)

### VRML export extension
avogadro_plugin_nogl(vrmlextension
  "vrmlextension.cpp;vrmlpainter.cpp;vrmldialog.cpp"
  vrmldialog.ui)

### File import extension
avogadro_plugin_nogl(fileimportextension fileimportextension.cpp fileimportdialog.ui)

### Shader extension
if(GLEW_FOUND)
  avogadro_plugin(shaderextension
    shaderextension.cpp
    shaderdialog.ui)
  target_link_libraries(shaderextension ${GLEW_LIBRARY})
  file(GLOB shader_VERT "shaders/*.vert")
  file(GLOB shader_FRAG "shaders/*.frag")
  file(GLOB shader_PARAMS "shaders/*.params")
  install(FILES ${shader_VERT} ${shader_FRAG} ${shader_PARAMS}
      DESTINATION ${SHARE_INSTALL_PREFIX}/libavogadro/shaders)
endif()

### super cells
avogadro_plugin_nogl(supercellextension
  "supercellextension.cpp;supercelldialog.cpp"
  supercelldialog.ui)

# Network fetch
avogadro_plugin_nogl(networkfetchextension networkfetchextension.cpp)
target_link_libraries(networkfetchextension ${QT_QTNETWORK_LIBRARY})

# Fit view / zoom to molecule
avogadro_plugin_nogl(fitviewextension fitviewextension.cpp)

# GL2PS
avogadro_plugin(gl2psextension
  "gl2psextension.cpp;gl2ps/gl2ps.c")

# Symmetry (libmsym) has compile issues on Windows
# due to VLA not supported by C compiler
if (MSVC)
  OPTION(BUILD_SYMMETRY "Build symmetry extension and libmsym - not supported with Visual Studio" OFF)
else()
  OPTION(BUILD_SYMMETRY
  "Build symmetry extension and libmsym" ON)
endif()

if(BUILD_SYMMETRY)
  add_subdirectory(symmetry)
endif()

# Subdirs
add_subdirectory(crystallography)
add_subdirectory(spectra)
add_subdirectory(surfaces)
add_subdirectory(swcntbuilder)
add_subdirectory(qtaim)
add_subdirectory(quantuminput)
add_subdirectory(orca)
add_subdirectory(yaehmop)
