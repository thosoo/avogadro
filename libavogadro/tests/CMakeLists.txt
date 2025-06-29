set(QT_USE_QTOPENGL true)

# define TESTDATADIR for tests that need input files
cmake_policy(SET CMP0005 NEW) # needed for line below (escapes value)
add_definitions( -DTESTDATADIR="${CMAKE_SOURCE_DIR}/testfiles/" )

# Ensure the Avogadro include directory is always first
get_directory_property(tmp_include_dirs INCLUDE_DIRECTORIES)
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES
  "${libavogadro_BINARY_DIR}/include;${tmp_include_dirs}")
include_directories(
  ${CMAKE_SOURCE_DIR}
  ${CMAKE_CURRENT_BINARY_DIR}
  ${libavogadro_BINARY_DIR}/src
  $<$<BOOL:${EIGEN3_FOUND}>:${EIGEN3_INCLUDE_DIR}>
  $<$<NOT:$<BOOL:${EIGEN3_FOUND}>>:${EIGEN2_INCLUDE_DIR}>
  ${OPENBABEL2_INCLUDE_DIR}
  ${BOOST_PYTHON_INCLUDES}
  ${PYTHON_INCLUDE_PATH}
)

link_directories(
  ${libavogadro_BINARY_DIR}/src
  ${OPENBABEL2_LIBRARY_DIRS}
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
  molecule
  moleculefile
  neighborlist
  addremovehydrogens
)
if(XTB_FOUND)
  list(APPEND tests xtbopttool xtbphysics)
endif()

foreach (test ${tests})
  message(STATUS "Test:  ${test}")
  set(test_SRCS ${test}test.cpp)
  set(test_MOC_CPPS ${test}test.cpp)
  QT4_WRAP_CPP(test_MOC_SRCS ${test_MOC_CPPS})
  ADD_CUSTOM_TARGET(${test}testmoc ALL DEPENDS ${test_MOC_SRCS})
  add_executable(${test}test ${test_SRCS})
  add_dependencies(${test}test ${test}testmoc)
  target_link_libraries(${test}test
    ${OPENBABEL2_LIBRARIES}
    ${QT_LIBRARIES}
    ${QT_QTTEST_LIBRARY}
    avogadro)
  add_test(${test}Test ${CMAKE_BINARY_DIR}/bin/${test}test)
  set_tests_properties(${test}Test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  set_property(SOURCE ${test_SRCS} PROPERTY LABELS avogadro)
  set_property(TARGET ${test}test PROPERTY LABELS avogadro)
  set_property(TEST ${test}Test PROPERTY LABELS avogadro)
endforeach ()

if(XTB_FOUND)
  include_directories(${XTB_INCLUDE_DIRS})
  link_directories(${XTB_LIBRARY_DIRS})
  target_link_libraries(xtbopttooltest ${XTB_LIBRARIES})
  target_link_libraries(xtbphysicstest ${XTB_LIBRARIES})
endif()

find_package(OpenMP)
if(OpenMP_CXX_FOUND AND XTB_FOUND)
  target_link_libraries(xtbopttooltest OpenMP::OpenMP_CXX)
  target_link_libraries(xtbphysicstest OpenMP::OpenMP_CXX)
endif()


# More complicated tests (i.e., with linking)
#message(STATUS "Test:  primitivemodeltest")
#  set(primitivemodeltest_SRCS primitivemodeltest.cpp modeltest.cpp)
#  set(primitivemodeltest_MOC_CPPS primitivemodeltest.cpp)
#  QT4_WRAP_CPP(primitivemodeltest_MOC_SRCS ${primitivemodeltest_MOC_CPPS})
#  ADD_CUSTOM_TARGET(primitivemodeltestmoc ALL DEPENDS 
#                                          ${primitivemodeltest_MOC_SRCS})
#  add_executable(primitivemodeltest ${primitivemodeltest_SRCS})
#  add_dependencies(primitivemodeltest primitivemodeltestmoc)
#  target_link_libraries(primitivemodeltest
#    ${OPENBABEL2_LIBRARIES}
#    ${QT_LIBRARIES}
#    ${QT_QTTEST_LIBRARY}
#    avogadro)
#add_test(primitivemodelTest ${CMAKE_BINARY_DIR}/bin/primitivemodeltest)

set(benches
  molecule
)

foreach (bench ${benches})
  message(STATUS "Benchmark:  ${bench}")
  set(bench_SRCS ${bench}bench.cpp)
  qt4_wrap_cpp(bench_MOC_SRCS ${bench_SRCS})
  add_custom_target(${bench}benchmoc ALL DEPENDS
    ${bench_MOC_SRCS})
  add_executable(${bench}bench ${bench_SRCS})
  add_dependencies(${bench}bench ${bench}benchmoc)
  target_link_libraries(${bench}bench
    ${OPENBABEL2_LIBRARIES}
    ${QT_LIBRARIES}
    ${QT_QTTEST_LIBRARY}
    avogadro)
  add_test(${bench}Bench ${CMAKE_BINARY_DIR}/bin/${bench}bench)
  set_tests_properties(${bench}Bench PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
  set_property(SOURCE ${bench_SRCS} PROPERTY LABELS avogadro)
  set_property(TARGET ${bench}bench PROPERTY LABELS avogadro)
  set_property(TEST ${bench}Bench PROPERTY LABELS avogadro)
endforeach (bench ${benches})
