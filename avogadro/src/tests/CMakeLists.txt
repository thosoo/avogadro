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
  RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

target_link_libraries(clipboardformatdetectortest
  PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    OpenBabel3::openbabel
)
if(TARGET openbabel_ext)
  add_dependencies(clipboardformatdetectortest openbabel_ext)
endif()

add_test(clipboardFormatDetectorTest ${CMAKE_BINARY_DIR}/bin/clipboardformatdetectortest)
set_tests_properties(clipboardFormatDetectorTest PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
