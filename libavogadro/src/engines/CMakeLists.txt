include_directories(${CMAKE_CURRENT_BINARY_DIR})

set(DESTINATION_DIR ${Avogadro_PLUGIN_INSTALL_DIR}/engines)


link_directories(${CMAKE_CURRENT_BINARY_DIR}/..)

set(LINK_LIBS)
set(PLUGIN_LABEL engines)
set(PLUGIN_TARGET engines)
add_custom_target(engines COMMENT "Meta target to build all Avogadro engines.")

# ball and stick engine
avogadro_plugin(bsdyengine bsdyengine.cpp bsdysettingswidget.ui)

# label engine
avogadro_plugin(labelengine labelengine.cpp labelsettingswidget.ui)

# wireframe
avogadro_plugin(wireengine wireengine.cpp wiresettingswidget.ui)

# hydrogen bond
avogadro_plugin(hbondengine hbondengine.cpp hbondsettingswidget.ui)

# force engine
avogadro_plugin(forceengine forceengine.cpp)
if(${CMAKE_CXX_COMPILER_ID} MATCHES Intel AND UNIX)
  set_target_properties(forceengine PROPERTIES COMPILE_FLAGS "-fvisibility=default")
endif()

# sphere
avogadro_plugin(sphereengine sphereengine.cpp spheresettingswidget.ui)

# stick (cylinder) engine
avogadro_plugin(stickengine stickengine.cpp sticksettingswidget.ui)

# Crystallographic ellipsoid engine
avogadro_plugin(ellipsoidengine ellipsoidengine.cpp ellipsoidssettingswidget.ui)

# Ribbon engine (biological structure)
avogadro_plugin(ribbonengine ribbonengine.cpp ribbonsettingswidget.ui)

# Cartoon engine (biological structure)
avogadro_plugin(cartoonengine "cartoonengine.cpp;cartoonmeshgenerator.cpp" cartoonsettingswidget.ui)

# Ring engine - display cyclic rings
avogadro_plugin(ringengine ringengine.cpp ringsettingswidget.ui)

# Polygon engine
avogadro_plugin(polygonengine polygonengine.cpp)

# Surface engine - molecular surfaces and 3D functions
avogadro_plugin(surfaceengine surfaceengine.cpp surfacesettingswidget.ui)

# Axes engine - display the axes
avogadro_plugin(axesengine axesengine.cpp axessettingswidget.ui)

# Dipole engine -- dipole moment
avogadro_plugin(dipoleengine dipoleengine.cpp dipolesettingswidget.ui)

# Simple wire engine - designed for efficiency with really big systems
avogadro_plugin(simplewireengine simplewireengine.cpp)

# Hair engine - overlay fur-like lines on atoms
find_package(OpenMP)
if(OpenMP_CXX_FOUND)
  set(LINK_LIBS ${LINK_LIBS} OpenMP::OpenMP_CXX)
endif()
avogadro_plugin(hairengine hairengine.cpp hairsettingswidget.ui)

# QTAIM engine - Quantum Theory of Atoms In Molecules
avogadro_plugin(qtaimengine qtaimengine.cpp qtaimsettingswidget.ui)

if(PYTHON_SUPPORT_ENABLED)
  file(GLOB python_SCRIPTS "python/*.py")
  install(FILES ${python_SCRIPTS} DESTINATION ${SHARE_INSTALL_PREFIX}/libavogadro/engineScripts)
endif()
