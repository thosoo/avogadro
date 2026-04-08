include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR}/..
)

set(clipboardformatdetector_test_SRCS
  clipboardformatdetectortest.cpp
  ../clipboardformatdetector.cpp
)

add_executable(clipboardformatdetectortest
  ${clipboardformatdetector_test_SRCS}
)
set_target_properties(clipboardformatdetectortest PROPERTIES
  AUTOMOC ON
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_link_libraries(clipboardformatdetectortest
  ${QT_LIBRARIES}
  ${QT_QTTEST_LIBRARY}
  ${OPENBABEL2_LIBRARIES}
)

add_test(clipboardFormatDetectorTest ${CMAKE_BINARY_DIR}/bin/clipboardformatdetectortest)
set_tests_properties(clipboardFormatDetectorTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
