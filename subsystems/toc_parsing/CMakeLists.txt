cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkTocParsing VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
get_filename_component(PARSING_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(pdfbookmarkTocParsing "${PARSING_ROOT}/src/parsing/parsing.cpp")
add_library(pdfbookmark::TocParsing ALIAS pdfbookmarkTocParsing)
target_compile_features(pdfbookmarkTocParsing PUBLIC cxx_std_17)
target_include_directories(pdfbookmarkTocParsing PUBLIC
  "$<BUILD_INTERFACE:${PARSING_ROOT}/include>"
  "$<INSTALL_INTERFACE:include>"
)
set_target_properties(pdfbookmarkTocParsing PROPERTIES
  EXPORT_NAME TocParsing
  WINDOWS_EXPORT_ALL_SYMBOLS ON
  POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
  target_compile_options(pdfbookmarkTocParsing PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(pdfbookmarkTocParsing PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(PDFBOOKMARK_PARSING_BUILD_TESTS "Build S3 pure-value tests" ON)
if(PDFBOOKMARK_PARSING_BUILD_TESTS)
  enable_testing()
  add_executable(pdfbookmark_parsing_tests "${PARSING_ROOT}/tests/parsing/parsing_test.cpp")
  target_compile_features(pdfbookmark_parsing_tests PRIVATE cxx_std_17)
  target_link_libraries(pdfbookmark_parsing_tests PRIVATE pdfbookmarkTocParsing)
  add_test(NAME parsing_pure COMMAND pdfbookmark_parsing_tests)
endif()

install(TARGETS pdfbookmarkTocParsing EXPORT pdfbookmarkTocParsingTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(DIRECTORY "${PARSING_ROOT}/include/pdfbookmark/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark")
configure_package_config_file(
  "${PARSING_ROOT}/cmake/pdfbookmarkTocParsingConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkTocParsingConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkTocParsing"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkTocParsingConfig.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkTocParsing")
install(EXPORT pdfbookmarkTocParsingTargets
  FILE pdfbookmarkTocParsingTargets.cmake
  NAMESPACE pdfbookmark::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkTocParsing")
