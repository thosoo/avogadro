# define TESTDATADIR for tests that need input files
cmake_policy(SET CMP0005 NEW) # needed for line below (escapes value)
add_definitions( -DTESTDATADIR="${CMAKE_SOURCE_DIR}/testfiles/" )

# Ensure the Avogadro include directory is always first
get_directory_property(tmp_include_dirs INCLUDE_DIRECTORIES)
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES
  "${libavogadro_BINARY_DIR}/include;${tmp_include_dirs}")
set(_avogadro_test_include_dirs
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${libavogadro_BINARY_DIR}/src
  ${libavogadro_BINARY_DIR}/src/extensions
  ${CMAKE_SOURCE_DIR}/libavogadro/src
  ${CMAKE_SOURCE_DIR}/libavogadro/src/extensions
  $<$<BOOL:${EIGEN3_FOUND}>:${EIGEN3_INCLUDE_DIR}>
  $<$<NOT:$<BOOL:${EIGEN3_FOUND}>>:${EIGEN2_INCLUDE_DIR}>
  ${OPENBABEL3_INCLUDE_DIR}
  $<$<BOOL:${GLEW_INCLUDE_DIR}>:${GLEW_INCLUDE_DIR}>
  $<$<BOOL:${GLEW_INCLUDE_DIRS}>:${GLEW_INCLUDE_DIRS}>
)
if(PYTHON_SUPPORT_ENABLED)
  list(APPEND _avogadro_test_include_dirs
    ${Boost_INCLUDE_DIRS}
    ${Python3_INCLUDE_DIRS}
    ${NUMPY_INCLUDE_DIR}
    ${SIP_INCLUDE_DIR})
endif()
include_directories(${_avogadro_test_include_dirs})

link_directories(
  ${libavogadro_BINARY_DIR}/src
)

# Disabled hydrogenscommand test for now - needs some work. Moc issues here.
# The commands should not just be included, they either need linking to
# or building. As plugin code is not part of the library it may require a
# different testing strategy.
find_package(PkgConfig)
pkg_check_modules(XTB xtb)

set(tests
  chunkstore
  scenebuilder
  meshcapture
  paritybsdy
  paritysphere
  paritystick
  paritywire
  compatibility
  geometry
  modernrenderer
  modernpick
  drawcommand
  glselectoverflow
  selectionset
  meshrevision
  meshstream
  meshgenparallel
  meshlock
  consumerparity
  threadedparity
  builderpool
  renderertier
  placement
  renderbench
  visualparity
  hidpiviewport
#  hydrogenscommand
  forcefield
  insertfragmentextension
  molecule
  moleculefile
  neighborlist
  addremovehydrogens
  vdwradius
  autooptrepro
  energyhud
)
if(XTB_FOUND)
  list(APPEND tests xtbopttool xtbphysics)
endif()

foreach (test ${tests})
  message(STATUS "Test:  ${test}")
  set(test_SRCS ${test}test.cpp)
  if (${test} STREQUAL "geometry")
    # geometry.cpp defines SphereGeometry::generate() / CylinderGeometry::generate()
    # as visibility-hidden members of the shared avogadro lib, so the import lib
    # does not resolve them; compile the source directly into the test.
    list(APPEND test_SRCS ../src/rendering/geometry.cpp)
  endif()
  if (${test} STREQUAL "builderpool")
    # BuilderPool has no A_EXPORT, so its out-of-line methods are not
    # dllexported from avogadro.dll on Windows (LNK2019 otherwise); compile the
    # source directly into the test, exactly as for geometry.cpp above.
    list(APPEND test_SRCS ../src/rendering/builderpool.cpp)
    # builderpooltest also calls snapshotTessellateBSDY (no A_EXPORT, not
    # dllexported on Windows -> LNK2001), so compile the snapshot tessellator
    # directly into the test as well.
    list(APPEND test_SRCS ../src/rendering/snapshottessellator.cpp)
  endif()
  if (${test} STREQUAL "autooptrepro" OR ${test} STREQUAL "energyhud")
    # DrawTool and AutoOptTool live in standalone MODULE plugins (not exported
    # from libavogadro.so), so their sources are compiled directly into the
    # test. Compiling both drawtool.cpp and autoopttool.cpp collides the two
    # AVOGADRO_TOOL_FACTORY plugin-export symbols (qt_plugin_instance etc.), so
    # skip DrawToolFactory's Q_PLUGIN_METADATA for this build only (the test
    # instantiates the factory directly, never via Qt's plugin loader); the
    # guard is in drawtool.h under AVOGADRO_TEST_NOPLUGIN. This is
    # cross-platform (no -l:drawtool.so, which the MSVC linker rejects).
    # (target_compile_definitions for that macro is applied after add_executable.)
    list(APPEND test_SRCS ../src/tools/autoopttool.cpp)
    list(APPEND test_SRCS ../src/tools/drawtool.cpp)
    # drawtool.cpp references the change-element command (and the other
    # draw commands) defined in drawcommand.cpp, which has no plugin factory
    # so it is safe to compile directly into the test.
    list(APPEND test_SRCS ../src/tools/drawcommand.cpp)
    # drawtool.cpp also calls the click-to-extend placement helpers; they are
    # compiled into the drawtool plugin, not exported from libavogadro, so the
    # test compiles placement.cpp directly (as for drawcommand.cpp).
    list(APPEND test_SRCS ../src/tools/placement.cpp)
    # The live drag / bond-click scenario checks (tasks 2.2 / 3.1) render the
    # molecule through the real ball-and-stick engine so GL picking resolves
    # real geometry. BSDYEngine is not exported from libavogadro; compile it
    # directly (as glselectoverflow / modernpick do).
    list(APPEND test_SRCS ../src/engines/bsdyengine.cpp)
  endif()
  if (${test} STREQUAL "renderertier")
    # requestedRendererTier / resolveRendererTier / tierName live in
    # renderertier.cpp, which has no A_EXPORT and is therefore not dllexported
    # on Windows; compile it directly into the test.
    list(APPEND test_SRCS ../src/rendering/renderertier.cpp)
  endif()
  if (${test} STREQUAL "placement")
    # The placement helper (add-draw-tool-click-extend) is compiled into the
    # drawtool plugin (drawtool_SRCS), not exported from libavogadro, so the
    # test compiles placement.cpp directly, exactly as for renderertier.cpp.
    list(APPEND test_SRCS ../src/tools/placement.cpp)
  endif()
  if (${test} STREQUAL "insertfragmentextension")
    list(APPEND test_SRCS
      ../src/extensions/insertfragmentextension.cpp
      ../src/extensions/insertfragmentdialog.cpp
      ../src/extensions/insertcommand.cpp
      ../src/extensions/sortfiltertreeproxymodel.cpp)
  endif()
  if (${test} STREQUAL "scenebuilder")
    # Compile the real ball-and-stick engine so the molecule can be rendered
    # headlessly through the SceneBuilder emitter (task 1.2).
    list(APPEND test_SRCS ../src/engines/bsdyengine.cpp)
  endif()
  if (${test} STREQUAL "meshcapture")
    # meshcapturetest (fix-orbital-mesh-render, design D4) drives the
    # SceneBuilder emitter directly to pin the D1 uncolored-mesh acceptance
    # and the D3 "report once" diagnostic. SceneBuilder, Mesh, MeshGenerator
    # and Cube are all A_EXPORT (dllexported from avogadro), so they are
    # linked from the library rather than compiled directly: compiling an
    # A_EXPORT class directly into a test TU would set A_EXPORT to
    # __declspec(dllimport) on Windows and fail with C2491 (the direct-compile
    # pattern in this file is reserved for NON-exported sources such as
    # glpainter_p.cpp / geometry.cpp).
  endif()
  if (${test} STREQUAL "modernrenderer")
    # Compile the real ball-and-stick engine so the parity test (task 3.1)
    # can render a molecule through the modern backend's instanced path.
    list(APPEND test_SRCS ../src/engines/bsdyengine.cpp)
    # SphereGeometry/CylinderGeometry, ChunkedUploader and CompatRendererBackend
    # have no A_EXPORT, so on Windows their out-of-line methods are not
    # dllexported from avogadro.dll (LNK2019 otherwise); compile the sources
    # directly into the test, exactly as for bsdyengine.cpp above.
    list(APPEND test_SRCS
      ../src/rendering/geometry.cpp
      ../src/rendering/chunkeduploader.cpp
      ../src/rendering/compatbackend.cpp)
  endif()
  if (${test} STREQUAL "modernpick")
    # Compile the real ball-and-stick engine so the pick-affordance test can
    # render a molecule through the SceneBuilder emitter and drive the modern
    # backend's pick() path. SceneBuilder and ModernRendererBackend are exported
    # from avogadro; BSDYEngine is not, so its out-of-line methods must be
    # compiled directly into the test (as for modernrenderer above).
    list(APPEND test_SRCS ../src/engines/bsdyengine.cpp)
  endif()
  if (${test} STREQUAL "meshstream" OR ${test} STREQUAL "visualparity")
    # meshstreamtest and visualparitytest instantiate the private GLPainter
    # directly and call its out-of-line methods (constructor, destructor,
    # drawColorMesh, begin, end). GLPainter has no A_EXPORT, so on Windows its
    # methods are not dllexported from avogadro.dll and the link fails
    # (LNK2019). Compile the private GL implementation directly into the test,
    # exactly as for geometry.cpp above. GLPainter pulls in its rendering
    # helpers (Sphere, Cylinder, TextRenderer) via createObjects/drawSphere/
    # drawCylinder/drawText, so those private TUs are compiled too;
    # GLPainterDevice and GLPainterPrivate stay resolved from avogadro. The
    # four files are self-contained (no other avogadro-src includes), so this
    # is a closed set. visualparitytest also includes snapshottessellator.h
    # (for the snapshot structs) but never calls snapshotTessellateBSDY, so it
    # needs nothing from the tessellator TU.
    list(APPEND test_SRCS
      ../src/glpainter_p.cpp
      ../src/sphere_p.cpp
      ../src/cylinder_p.cpp
      ../src/textrenderer_p.cpp)
  endif()
  if (${test} STREQUAL "glselectoverflow" OR ${test} STREQUAL "hidpiviewport")
    # Compile the real ball-and-stick engine so the widget can drive the
    # picking path (legacy GL_SELECT for glselectoverflow, the colour-ID
    # pick pass for the HiDPI modern-tier test).
    list(APPEND test_SRCS ../src/engines/bsdyengine.cpp)
  endif()
  if (${test} STREQUAL "consumerparity")
    # consumerparitytest drives the private GLPainter directly (task 5.1: the
    # mesh-flush upload counter) and also drives a real GLWidget on the modern
    # tier (tasks 5.2 overlay + unit cell). GLPainter has no A_EXPORT, so on
    # Windows its out-of-line methods are not dllexported from avogadro.dll
    # (LNK2019); compile the private GL implementation directly into the test,
    # exactly as for meshstream. GLPainter pulls in Sphere/Cylinder/TextRenderer
    # via drawSphere/drawCylinder/drawText, so those private TUs compile too.
    # bsdyengine is compiled so a full widget scene has instanced content.
    list(APPEND test_SRCS
      ../src/glpainter_p.cpp
      ../src/sphere_p.cpp
      ../src/cylinder_p.cpp
      ../src/textrenderer_p.cpp
      ../src/engines/bsdyengine.cpp)
  endif()
  if (${test} STREQUAL "threadedparity" OR ${test} STREQUAL "renderbench" OR ${test} STREQUAL "visualparity")
    # Compile the real ball-and-stick engine so the snapshot tessellator can
    # be compared against the GUI-thread engine emission (task 6.1), and so
    # the render bench can drive the worker path through a real engine (8.1).
    list(APPEND test_SRCS ../src/engines/bsdyengine.cpp)
  endif()
  if (${test} STREQUAL "threadedparity")
    # threadedparitytest calls snapshotTessellateBSDY, which has no A_EXPORT
    # and is therefore not dllexported from avogadro.dll on Windows (LNK2019
    # otherwise). Compile the snapshot tessellator directly into the test, as
    # for bsdyengine.cpp above. renderbench/visualparity include the header
    # but never call the function, so they need nothing extra.
    list(APPEND test_SRCS ../src/rendering/snapshottessellator.cpp)
  endif()
  # The ported engines are Qt plugins: their plugin metadata cannot be
  # duplicated inside one binary, so the legacy-vs-modern parity harness gets
  # one binary per engine. Compiling the engine .cpp also makes AUTOUIC
  # generate the settings-widget ui_*.h header its engine header includes.
  foreach (_parity_engine "bsdy" "sphere" "stick" "wire")
    if (${test} STREQUAL "parity${_parity_engine}")
      list(APPEND test_SRCS ../src/engines/${_parity_engine}engine.cpp)
    endif()
  endforeach()
  add_executable(${test}test ${test_SRCS})
  target_link_libraries(${test}test
    PRIVATE
    OpenBabel3::openbabel
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    OpenGL::GL
    Qt6::OpenGLWidgets
    Qt6::Test
    avogadro)
  # meshstreamtest compiles glpainter_p.cpp directly (its GLPainter methods
  # are not dllexported), and glpainter_p.cpp calls GLU (gluNurbsProperty).
  # avogadro links GLU internally, but that does not propagate to a consumer
  # that pulls the TU in directly, so link it explicitly for this test only.
  if(${test} STREQUAL "meshstream" OR ${test} STREQUAL "visualparity")
    # These compile glpainter_p.cpp directly (its GLPainter methods are not
    # dllexported), and glpainter_p.cpp calls GLU (gluNurbsProperty). avogadro
    # links GLU internally, but that does not propagate to a consumer that
    # pulls the TU in directly, so link it explicitly for these tests only.
    target_link_libraries(${test}test PRIVATE OpenGL::GLU)
  endif()
  if(${test} STREQUAL "modernrenderer")
    # modernrendertest uses gluProject (GLU) to cross-check the modern tier's
    # composed uMVP against the native fixed-function stack. avogadro links GLU
    # internally, but that does not propagate to a consumer that calls it
    # directly, so link it explicitly for this test only.
    target_link_libraries(${test}test PRIVATE OpenGL::GLU)
  endif()
  if(${test} STREQUAL "consumerparity")
    # consumerparitytest compiles glpainter_p.cpp directly (its GLPainter
    # methods are not dllexported), and glpainter_p.cpp calls GLU
    # (gluNurbsProperty). Link it explicitly for this test only.
    target_link_libraries(${test}test PRIVATE OpenGL::GLU)
  endif()
  if(TARGET openbabel_ext)
    add_dependencies(${test}test openbabel_ext)
  endif()
  if(${test} STREQUAL "autooptrepro" OR ${test} STREQUAL "energyhud")
    # Skip DrawToolFactory's Q_PLUGIN_METADATA for this build only (see the
    # test_SRCS block above): compiling drawtool.cpp alongside autoopttool.cpp
    # would otherwise collide the two AVOGADRO_TOOL_FACTORY plugin-export
    # symbols. The guard is in drawtool.h under this macro.
    target_compile_definitions(${test}test PRIVATE AVOGADRO_TEST_NOPLUGIN)
  endif()
  if(${test} STREQUAL "consumerparity")
    # consumerparitytest reads the mesh-flush upload counter hook
    # (g_glpainterMeshUploadCount) that lives behind this macro in
    # glpainter_p.cpp; the test compiles that TU so the symbol resolves.
    target_compile_definitions(${test}test PRIVATE AVOGADRO_TEST_UPLOAD_COUNTER)
  endif()
  add_test(${test}Test ${CMAKE_BINARY_DIR}/bin/${test}test)
  set(_test_env "QT_QPA_PLATFORM=offscreen")
  # The parity binaries drive a real GLWidget for the legacy tier, which
  # QOpenGLWidget does not support on the offscreen platform: they need a
  # real X display (run ctest under xvfb-run) with the default xcb platform.
  if(${test} MATCHES "^(parity|compatibility|glselectoverflow|meshstream|consumerparity|threadedparity|renderbench|visualparity|hidpiviewport|energyhud)")
    set(_test_env "LIBGL_ALWAYS_SOFTWARE=1")
  endif()
  if(${test} STREQUAL "forcefield")
    list(APPEND _test_env
      "BABEL_DATADIR=${CMAKE_BINARY_DIR}/openbabel-install/share")
  endif()
  set_tests_properties(${test}Test PROPERTIES ENVIRONMENT "${_test_env}")
  set_property(SOURCE ${test_SRCS} PROPERTY LABELS avogadro)
  set_property(TARGET ${test}test PROPERTY LABELS avogadro)
  set_property(TEST ${test}Test PROPERTY LABELS avogadro)
endforeach ()

set(dialogsmoketest_SRCS
  dialogsmoketest.cpp

  # ORCA input/analyse smoke coverage. OrcaInputDialog uses
  # OrcaExtension::staticMetaObject, and the real input-generator regression is
  # reached through OrcaExtension::performAction(), so the extension source and
  # header are included explicitly to provide the generated metaobject and the
  # menu/action path under test.
  ../src/extensions/orca/orcaextension.cpp
  ../src/extensions/orca/orcaextension.h
  ../src/extensions/orca/orcainputdialog.cpp
  ../src/extensions/orca/orcainputdialog.h
  ../src/extensions/orca/orcaanalysedialog.cpp
  ../src/extensions/orca/orcaanalysedialog.h
  ../src/extensions/orca/orcaspectra.cpp
  ../src/extensions/orca/orcadata.cpp
  ../src/extensions/orca/orcadata.h
  ../src/extensions/orca/orcainputdialog.ui
  ../src/extensions/orca/orcaanalysedialog.ui
  ../src/extensions/orca/orcaspectra.ui

  # Concrete quantum input dialogs. inputdialog.cpp provides the abstract base
  # implementation, but InputDialog itself is not instantiated.
  ../src/extensions/quantuminput/inputdialog.cpp
  ../src/extensions/quantuminput/abinitinputdialog.cpp
  ../src/extensions/quantuminput/daltoninputdialog.cpp
  ../src/extensions/quantuminput/gamessukinputdialog.cpp
  ../src/extensions/quantuminput/gaussianinputdialog.cpp
  ../src/extensions/quantuminput/molproinputdialog.cpp
  ../src/extensions/quantuminput/mopacinputdialog.cpp
  ../src/extensions/quantuminput/nwcheminputdialog.cpp
  ../src/extensions/quantuminput/psi4inputdialog.cpp
  ../src/extensions/quantuminput/qcheminputdialog.cpp
  ../src/extensions/quantuminput/teracheminputdialog.cpp
  ../src/extensions/quantuminput/lammpsinputdialog.cpp
  ../src/extensions/quantuminput/gamessinputdialog.cpp
  ../src/extensions/quantuminput/gamessinputdata.cpp
  ../src/extensions/quantuminput/gamesshighlighter.cpp
  ../src/extensions/quantuminput/abinitinputdialog.ui
  ../src/extensions/quantuminput/gaussianinputdialog.ui
  ../src/extensions/quantuminput/daltoninputdialog.ui
  ../src/extensions/quantuminput/gamessukinputdialog.ui
  ../src/extensions/quantuminput/molproinputdialog.ui
  ../src/extensions/quantuminput/mopacinputdialog.ui
  ../src/extensions/quantuminput/nwcheminputdialog.ui
  ../src/extensions/quantuminput/psi4inputdialog.ui
  ../src/extensions/quantuminput/qcheminputdialog.ui
  ../src/extensions/quantuminput/teracheminputdialog.ui
  ../src/extensions/quantuminput/lammpsinputdialog.ui
  ../src/extensions/quantuminput/gamessinputdialog.ui

  # Simple force-field dialogs. ConformerSearchDialog is skipped because it
  # references ForceFieldCommand from forcefieldextension.cpp.
  ../src/extensions/forcefielddialog.cpp
  ../src/extensions/constraintsdialog.cpp
  ../src/extensions/constraintsmodel.cpp
  ../src/extensions/forcefielddialog.ui
  ../src/extensions/constraintsdialog.ui

  # Insert-fragment dialog and model helper.
  ../src/extensions/insertfragmentdialog.cpp
  ../src/extensions/sortfiltertreeproxymodel.cpp
  ../src/extensions/insertfragmentdialog.ui

  # Simple core application dialogs.
  ../../avogadro/src/addenginedialog.cpp
  ../../avogadro/src/updatedialog.cpp
  ../../avogadro/src/addenginedialog.ui)

add_executable(dialogsmoketest ${dialogsmoketest_SRCS})
target_include_directories(dialogsmoketest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/orca
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/quantuminput
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions
  ${CMAKE_CURRENT_SOURCE_DIR}/../../avogadro/src)
set_target_properties(dialogsmoketest PROPERTIES
  AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/orca;${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/quantuminput;${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions;${CMAKE_CURRENT_SOURCE_DIR}/../../avogadro/src")
target_link_libraries(dialogsmoketest
  PRIVATE
  OpenBabel3::openbabel
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  Qt6::Test
  avogadro)
if(TARGET openbabel_ext)
  add_dependencies(dialogsmoketest openbabel_ext)
endif()
add_test(dialogsmokeTest ${CMAKE_BINARY_DIR}/bin/dialogsmoketest)
set_tests_properties(dialogsmokeTest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen;BABEL_DATADIR=${CMAKE_BINARY_DIR}/openbabel-install/share")
set_property(SOURCE ${dialogsmoketest_SRCS} PROPERTY LABELS avogadro)
set_property(TARGET dialogsmoketest PROPERTY LABELS avogadro)
set_property(TEST dialogsmokeTest PROPERTY LABELS avogadro)

# Surfaces stale-callback guard regression (fix-second-document-open-crash,
# tasks 1.3 / 2.2). Compiles the real SurfaceExtension + SurfaceDialog +
# VdWSurface so the calculate() -> calculateDone() generation-guard path can be
# exercised against a real molecule/basis offscreen.
set(surfaceextensiontest_SRCS
  surfaceextensiontest.cpp
  ../src/extensions/surfaces/surfaceextension.cpp
  ../src/extensions/surfaces/surfaceextension.h
  ../src/extensions/surfaces/surfacedialog.cpp
  ../src/extensions/surfaces/surfacedialog.h
  ../src/extensions/surfaces/surfacedialog.ui
  ../src/extensions/surfaces/vdwsurface.cpp
  ../src/extensions/surfaces/vdwsurface.h)
add_executable(surfaceextensiontest ${surfaceextensiontest_SRCS})
target_include_directories(surfaceextensiontest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces
  ${CMAKE_CURRENT_SOURCE_DIR}/../../avogadro/src)
set_target_properties(surfaceextensiontest PROPERTIES
  AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions")
target_link_libraries(surfaceextensiontest
  PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  Qt6::Test
  Qt6::Concurrent
  avogadro
  OpenQube
  OpenBabel3::openbabel)
target_compile_definitions(surfaceextensiontest PRIVATE
  TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testfiles")
add_test(surfaceextensionTest ${CMAKE_BINARY_DIR}/bin/surfaceextensiontest)
set_tests_properties(surfaceextensionTest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(SOURCE ${surfaceextensiontest_SRCS} PROPERTY LABELS avogadro)
set_property(TARGET surfaceextensiontest PROPERTY LABELS avogadro)
set_property(TEST surfaceextensionTest PROPERTY LABELS avogadro)

# Surface-engine hygiene regression (fix-orbital-visualization, tasks 2.1-2.4,
# design D2): drives the real SurfaceEngine (which is not exported from
# libavogadro, so surfaceengine.cpp is compiled directly into the test) to
# assert the stale-cube lock leak is gone, repeated setMolecule does not
# multiply per-primitive connections, and the combo contains only valid MO
# entries. AVOGADRO_TEST_COMBO_REBUILDS makes the combo-rebuild counter hook
# linkable (mirrors the AVOGADRO_TEST_UPLOAD_COUNTER pattern).
set(surfaceenginehygienetest_SRCS
  surfaceenginehygienetest.cpp
  ../src/engines/surfaceengine.cpp
  ../src/engines/surfaceengine.h)
add_executable(surfaceenginehygienetest ${surfaceenginehygienetest_SRCS})
target_include_directories(surfaceenginehygienetest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/engines)
set_target_properties(surfaceenginehygienetest PROPERTIES
  AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../src/engines")
target_link_libraries(surfaceenginehygienetest
  PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  Qt6::Test
  avogadro
  OpenBabel3::openbabel)
target_compile_definitions(surfaceenginehygienetest PRIVATE
  AVOGADRO_TEST_COMBO_REBUILDS)
add_test(surfaceenginehygieneTest ${CMAKE_BINARY_DIR}/bin/surfaceenginehygienetest)
set_tests_properties(surfaceenginehygieneTest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(SOURCE ${surfaceenginehygienetest_SRCS} PROPERTY LABELS avogadro)
set_property(TARGET surfaceenginehygienetest PROPERTY LABELS avogadro)
set_property(TEST surfaceenginehygieneTest PROPERTY LABELS avogadro)

# Guards the OpenSpec task 5 (2.3, D3) refactor: the MO cube-data handoff copy
# runs on a worker thread and resumes the phase machine exactly once, so
# calculateMesh() runs once (2 meshes) and both lobes become stable.
set(surfaceextensionmothrottletest_SRCS
  surfaceextensionmothrottletest.cpp
  ../src/extensions/surfaces/surfaceextension.cpp
  ../src/extensions/surfaces/surfaceextension.h
  ../src/extensions/surfaces/surfacedialog.cpp
  ../src/extensions/surfaces/surfacedialog.h
  ../src/extensions/surfaces/surfacedialog.ui
  ../src/extensions/surfaces/vdwsurface.cpp
  ../src/extensions/surfaces/vdwsurface.h)
add_executable(surfaceextensionmothrottletest ${surfaceextensionmothrottletest_SRCS})
target_include_directories(surfaceextensionmothrottletest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces
  ${CMAKE_CURRENT_SOURCE_DIR}/../../avogadro/src)
set_target_properties(surfaceextensionmothrottletest PROPERTIES
  AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions")
target_link_libraries(surfaceextensionmothrottletest
  PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  Qt6::Test
  Qt6::Concurrent
  avogadro
  OpenQube
  OpenBabel3::openbabel)
target_compile_definitions(surfaceextensionmothrottletest PRIVATE
  TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testfiles")
add_test(surfaceextensionmothrottletestTest ${CMAKE_BINARY_DIR}/bin/surfaceextensionmothrottletest)
set_tests_properties(surfaceextensionmothrottletestTest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(SOURCE ${surfaceextensionmothrottletest_SRCS} PROPERTY LABELS avogadro)
set_property(TARGET surfaceextensionmothrottletest PROPERTY LABELS avogadro)
set_property(TEST surfaceextensionmothrottletestTest PROPERTY LABELS avogadro)

# D7 distance-screening guard (fix-surface-visualization-performance, task
# 7.1): screened vs unscreened grids agree within tolerance, screening is
# deterministic, and the density path behaves the same. Needs only the OpenQube
# basis-set machinery (linked from the OpenQube library) plus the molecule
# file reader from avogadro; no plugin/dialog sources.
add_executable(screeningtest screeningtest.cpp)
target_include_directories(screeningtest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces
  ${CMAKE_CURRENT_SOURCE_DIR}/../../avogadro/src)
target_link_libraries(screeningtest
  PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Test
  Qt6::Concurrent
  avogadro
  OpenQube
  OpenBabel3::openbabel)
target_compile_definitions(screeningtest PRIVATE
  TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testfiles")
add_test(screeningTest ${CMAKE_BINARY_DIR}/bin/screeningtest)
set_tests_properties(screeningTest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(SOURCE screeningtest.cpp PROPERTY LABELS avogadro)
set_property(TARGET screeningtest PROPERTY LABELS avogadro)
set_property(TEST screeningTest PROPERTY LABELS avogadro)

# D5 slab-based cube-evaluation determinism guard (fix-orbital-visualization,
# task 5.4): asserts bitwise-identical grid values across slab partitions
# (testSlabCount = 1 vs 4 vs auto), that repeated auto runs are bitwise
# identical, and that all values are finite. Needs only the OpenQube
# basis-set machinery plus the synthetic basis built via addAtom/addBasis/
# addMO; no plugin/dialog sources.
add_executable(cubeevaltest cubeevaltest.cpp)
target_include_directories(cubeevaltest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces
  ${CMAKE_CURRENT_SOURCE_DIR}/../../avogadro/src)
target_link_libraries(cubeevaltest
  PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Test
  Qt6::Concurrent
  avogadro
  OpenQube
  OpenBabel3::openbabel)
target_compile_definitions(cubeevaltest PRIVATE
  TESTDATA_DIR="${CMAKE_SOURCE_DIR}/testfiles")
add_test(cubeevalTest ${CMAKE_BINARY_DIR}/bin/cubeevaltest)
set_tests_properties(cubeevalTest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(SOURCE cubeevaltest.cpp PROPERTY LABELS avogadro)
set_property(TARGET cubeevaltest PROPERTY LABELS avogadro)
set_property(TEST cubeevalTest PROPERTY LABELS avogadro)

# D11 precalculation-policy pin (fix-surface-visualization-performance, task
# 7.3): unit-tests OrbitalWidget::clampPrecalcQuality (Low/Medium pass
# through, High/VeryHigh clamp to Medium) and pins that precalculateOrbitals
# routes its queue resolution through the clamp. Includes the real
# orbitalwidget.h, so AUTOUIC must generate ui_orbitalwidget.h — mirror the
# surfaceextensiontest include/AUTOUIC setup (header-only; no extension TU
# compiled). AUTOUIC cannot see the ui_ include inside the transitively
# included orbitalwidget.h, so generate the header explicitly with
# qt6_wrap_ui into this directory's binary dir (already on the include path).
qt6_wrap_ui(precalcpolicytest_UI
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces/orbitalwidget.ui)
add_executable(precalcpolicytest precalcpolicytest.cpp
  ${precalcpolicytest_UI})
target_include_directories(precalcpolicytest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces
  ${CMAKE_CURRENT_SOURCE_DIR}/../../avogadro/src)
set_target_properties(precalcpolicytest PROPERTIES
  AUTOUIC "OFF")
target_compile_definitions(precalcpolicytest PRIVATE
  AVOGADRO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(precalcpolicytest
  PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  Qt6::Test
  Qt6::Concurrent
  avogadro
  OpenQube
  OpenBabel3::openbabel)
add_test(precalcpolicyTest ${CMAKE_BINARY_DIR}/bin/precalcpolicytest)
set_tests_properties(precalcpolicyTest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(SOURCE precalcpolicytest.cpp PROPERTY LABELS avogadro)
set_property(TARGET precalcpolicytest PROPERTY LABELS avogadro)
set_property(TEST precalcpolicyTest PROPERTY LABELS avogadro)

# Orbital-selection semantics regression (fix-orbital-visualization, task 3.5,
# design D4): unit-tests planOrbitalSelection and the pending-orbital lifecycle
# (latest click wins; pending cleared after completion). OrbitalExtension is a
# plugin (not exported from libavogadro), so orbitalextension.cpp is compiled
# directly into the test; it needs orbitalwidget.cpp + orbitaltablemodel.cpp
# for the completion path, and the generated ui_orbitalwidget.h via qt6_wrap_ui
# (AUTOUIC cannot see the ui_ include inside orbitalwidget.h).
qt6_wrap_ui(orbitalselectiontest_UI
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces/orbitalwidget.ui
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces/orbitalsettingsdialog.ui)
set(orbitalselectiontest_SRCS
  orbitalselectiontest.cpp
  ../src/extensions/surfaces/orbitalextension.cpp
  ../src/extensions/surfaces/orbitalwidget.cpp
  ../src/extensions/surfaces/orbitaltablemodel.cpp
  ../src/extensions/surfaces/orbitalsettingsdialog.cpp
  ../src/extensions/surfaces/htmldelegate.cpp
  ${orbitalselectiontest_UI})
add_executable(orbitalselectiontest ${orbitalselectiontest_SRCS})
target_include_directories(orbitalselectiontest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces)
set_target_properties(orbitalselectiontest PROPERTIES
  AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces")
target_compile_definitions(orbitalselectiontest PRIVATE
  AVOGADRO_TEST_ORBITAL_SELECTION)
target_link_libraries(orbitalselectiontest
  PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  Qt6::Test
  Qt6::Concurrent
  avogadro
  OpenQube
  OpenBabel3::openbabel)
add_test(orbitalselectionTest ${CMAKE_BINARY_DIR}/bin/orbitalselectiontest)
set_tests_properties(orbitalselectionTest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(SOURCE ${orbitalselectiontest_SRCS} PROPERTY LABELS avogadro)
set_property(TARGET orbitalselectiontest PROPERTY LABELS avogadro)
set_property(TEST orbitalselectionTest PROPERTY LABELS avogadro)

# Orbital-status-column regression (fix-orbital-status-column, tasks 4.2-4.4,
# design D4): offscreen regression for the status-column paint path, the
# bounds-safe progress mutators, and first-fill HOMO scroll. Compiles the real
# OrbitalWidget + OrbitalTableModel + ProgressBarDelegate directly (the surfaces
# extension is a plugin, not exported from libavogadro, so its sources compile
# into the test). Needs the generated ui_orbitalwidget.h via qt6_wrap_ui.
qt6_wrap_ui(orbitalstatustest_UI
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces/orbitalwidget.ui)
set(orbitalstatustest_SRCS
  orbitalstatustest.cpp
  ../src/extensions/surfaces/orbitalextension.cpp
  ../src/extensions/surfaces/orbitalwidget.cpp
  ../src/extensions/surfaces/orbitaltablemodel.cpp
  ../src/extensions/surfaces/orbitalsettingsdialog.cpp
  ../src/extensions/surfaces/htmldelegate.cpp
  ${orbitalstatustest_UI})
add_executable(orbitalstatustest ${orbitalstatustest_SRCS})
target_include_directories(orbitalstatustest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces)
set_target_properties(orbitalstatustest PROPERTIES
  AUTOUIC_SEARCH_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/../src/extensions/surfaces")
target_compile_definitions(orbitalstatustest PRIVATE
  AVOGADRO_TEST_ORBITAL_STATUS)
target_link_libraries(orbitalstatustest
  PRIVATE
  Qt6::Core
  Qt6::Gui
  Qt6::Widgets
  Qt6::OpenGL
  Qt6::OpenGLWidgets
  Qt6::Test
  Qt6::Concurrent
  avogadro
  OpenQube
  OpenBabel3::openbabel)
add_test(orbitalstatustest ${CMAKE_BINARY_DIR}/bin/orbitalstatustest)
set_tests_properties(orbitalstatustest PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_property(SOURCE ${orbitalstatustest_SRCS} PROPERTY LABELS avogadro)
set_property(TARGET orbitalstatustest PROPERTY LABELS avogadro)
set_property(TEST orbitalstatustest PROPERTY LABELS avogadro)


add_executable(qt6signalpatternstest qt6signalpatternstest.cpp)
target_compile_definitions(qt6signalpatternstest PRIVATE
  AVOGADRO_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_link_libraries(qt6signalpatternstest
  PRIVATE
  Qt6::Core
  Qt6::Test)
add_test(qt6signalpatternsTest ${CMAKE_BINARY_DIR}/bin/qt6signalpatternstest)
set_property(SOURCE qt6signalpatternstest.cpp PROPERTY LABELS avogadro)
set_property(TARGET qt6signalpatternstest PROPERTY LABELS avogadro)
set_property(TEST qt6signalpatternsTest PROPERTY LABELS avogadro)

if(XTB_FOUND)
  include_directories(${XTB_INCLUDE_DIRS})
  link_directories(${XTB_LIBRARY_DIRS})
  target_link_libraries(xtbopttooltest PRIVATE ${XTB_LIBRARIES})
  target_link_libraries(xtbphysicstest PRIVATE ${XTB_LIBRARIES})
endif()

find_package(OpenMP)
if(OpenMP_CXX_FOUND AND XTB_FOUND)
  target_link_libraries(xtbopttooltest PRIVATE OpenMP::OpenMP_CXX)
  target_link_libraries(xtbphysicstest PRIVATE OpenMP::OpenMP_CXX)
endif()


# More complicated tests (i.e., with linking)
#message(STATUS "Test:  primitivemodeltest")
#  set(primitivemodeltest_SRCS primitivemodeltest.cpp modeltest.cpp)
#  set(primitivemodeltest_MOC_CPPS primitivemodeltest.cpp)
#  automoc(primitivemodeltest_MOC_SRCS ${primitivemodeltest_MOC_CPPS})
#  ADD_CUSTOM_TARGET(primitivemodeltestmoc ALL DEPENDS 
#                                          ${primitivemodeltest_MOC_SRCS})
#  add_executable(primitivemodeltest ${primitivemodeltest_SRCS})
#  add_dependencies(primitivemodeltest primitivemodeltestmoc)
#  target_link_libraries(primitivemodeltest
#    OpenBabel3::openbabel
#    Qt6 libraries
#    Qt6::Test
#    avogadro)
#add_test(primitivemodelTest ${CMAKE_BINARY_DIR}/bin/primitivemodeltest)

set(benches
  molecule
)

foreach (bench ${benches})
  message(STATUS "Benchmark:  ${bench}")
  set(bench_SRCS ${bench}bench.cpp)
  add_executable(${bench}bench ${bench_SRCS})
  target_link_libraries(${bench}bench
    PRIVATE
    OpenBabel3::openbabel
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::OpenGL
    Qt6::OpenGLWidgets
    Qt6::Test
    avogadro)
  if(TARGET openbabel_ext)
    add_dependencies(${bench}bench openbabel_ext)
  endif()
  add_test(${bench}Bench ${CMAKE_BINARY_DIR}/bin/${bench}bench)
  set_tests_properties(${bench}Bench PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  set_property(SOURCE ${bench_SRCS} PROPERTY LABELS avogadro)
  set_property(TARGET ${bench}bench PROPERTY LABELS avogadro)
  set_property(TEST ${bench}Bench PROPERTY LABELS avogadro)
endforeach (bench ${benches})
