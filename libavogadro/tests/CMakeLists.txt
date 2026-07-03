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
  drawcommand
#  hydrogenscommand
  forcefield
  insertfragmentextension
  molecule
  moleculefile
  neighborlist
  addremovehydrogens
  vdwradius
)
if(XTB_FOUND)
  list(APPEND tests xtbopttool xtbphysics)
endif()

foreach (test ${tests})
  message(STATUS "Test:  ${test}")
  set(test_SRCS ${test}test.cpp)
  if (${test} STREQUAL "insertfragmentextension")
    list(APPEND test_SRCS
      ../src/extensions/insertfragmentextension.cpp
      ../src/extensions/insertfragmentdialog.cpp
      ../src/extensions/insertcommand.cpp
      ../src/extensions/sortfiltertreeproxymodel.cpp)
  endif()
  add_executable(${test}test ${test_SRCS})
  target_link_libraries(${test}test
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
    add_dependencies(${test}test openbabel_ext)
  endif()
  add_test(${test}Test ${CMAKE_BINARY_DIR}/bin/${test}test)
  set(_test_env "QT_QPA_PLATFORM=offscreen")
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
  ../src/extensions/orca/orcainputdialog.cpp
  ../src/extensions/orca/orcaanalysedialog.cpp
  ../src/extensions/orca/orcaspectra.cpp
  ../src/extensions/orca/orcadata.cpp
  ../src/extensions/orca/orcainputdialog.ui
  ../src/extensions/orca/orcaanalysedialog.ui
  ../src/extensions/orca/orcaspectra.ui
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
  ../src/extensions/forcefielddialog.cpp
  ../src/extensions/constraintsdialog.cpp
  ../src/extensions/constraintsmodel.cpp
  ../src/extensions/conformersearchdialog.cpp
  ../src/extensions/forcefielddialog.ui
  ../src/extensions/constraintsdialog.ui
  ../src/extensions/conformersearchdialog.ui
  ../src/extensions/insertfragmentdialog.cpp
  ../src/extensions/sortfiltertreeproxymodel.cpp
  ../src/extensions/insertfragmentdialog.ui
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
