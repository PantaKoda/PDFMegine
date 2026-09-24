cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkTocDetection VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
get_filename_component(DETECTION_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(pdfbookmarkTocDetection "${DETECTION_ROOT}/src/detection/detection.cpp")
add_library(pdfbookmark::TocDetection ALIAS pdfbookmarkTocDetection)
target_compile_features(pdfbookmarkTocDetection PUBLIC cxx_std_17)
target_include_directories(pdfbookmarkTocDetection PUBLIC
  "$<BUILD_INTERFACE:${DETECTION_ROOT}/include>"
  "$<INSTALL_INTERFACE:include>"
)
set_target_properties(pdfbookmarkTocDetection PROPERTIES
  EXPORT_NAME TocDetection
  WINDOWS_EXPORT_ALL_SYMBOLS ON
  POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
  target_compile_options(pdfbookmarkTocDetection PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(pdfbookmarkTocDetection PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(PDFBOOKMARK_DETECTION_BUILD_TESTS "Build S2 pure-value tests" ON)
if(PDFBOOKMARK_DETECTION_BUILD_TESTS)
  enable_testing()
  add_executable(pdfbookmark_detection_tests "${DETECTION_ROOT}/tests/detection/detection_test.cpp")
  target_compile_features(pdfbookmark_detection_tests PRIVATE cxx_std_17)
  target_link_libraries(pdfbookmark_detection_tests PRIVATE pdfbookmarkTocDetection)
  add_test(NAME detection_pure COMMAND pdfbookmark_detection_tests)
endif()

install(TARGETS pdfbookmarkTocDetection EXPORT pdfbookmarkTocDetectionTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(DIRECTORY "${DETECTION_ROOT}/include/pdfbookmark/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark")
configure_package_config_file(
  "${DETECTION_ROOT}/cmake/pdfbookmarkTocDetectionConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkTocDetectionConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkTocDetection"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkTocDetectionConfig.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkTocDetection")
install(EXPORT pdfbookmarkTocDetectionTargets
  FILE pdfbookmarkTocDetectionTargets.cmake
  NAMESPACE pdfbookmark::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkTocDetection")
