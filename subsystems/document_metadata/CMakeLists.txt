cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkMetadata VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
get_filename_component(METADATA_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# S6 Document Metadata Extraction: pure computation on S1 contract values.
add_library(pdfbookmarkMetadata "${METADATA_ROOT}/src/metadata/metadata.cpp")
add_library(pdfbookmark::Metadata ALIAS pdfbookmarkMetadata)
target_compile_features(pdfbookmarkMetadata PUBLIC cxx_std_17)
target_include_directories(pdfbookmarkMetadata PUBLIC
  "$<BUILD_INTERFACE:${METADATA_ROOT}/include>"
  "$<INSTALL_INTERFACE:include>")
set_target_properties(pdfbookmarkMetadata PROPERTIES
  EXPORT_NAME Metadata
  WINDOWS_EXPORT_ALL_SYMBOLS ON
  POSITION_INDEPENDENT_CODE ON)
if(MSVC)
  target_compile_options(pdfbookmarkMetadata PRIVATE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(pdfbookmarkMetadata PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(PDFBOOKMARK_METADATA_BUILD_TESTS "Build S6 pure-value tests" ON)
if(PDFBOOKMARK_METADATA_BUILD_TESTS)
  enable_testing()
  add_executable(pdfbookmark_metadata_tests
    "${METADATA_ROOT}/tests/metadata/metadata_test.cpp")
  target_compile_features(pdfbookmark_metadata_tests PRIVATE cxx_std_17)
  target_link_libraries(pdfbookmark_metadata_tests PRIVATE pdfbookmarkMetadata)
  if(MSVC)
    target_compile_options(pdfbookmark_metadata_tests PRIVATE /utf-8)
  endif()
  add_test(NAME metadata_pure COMMAND pdfbookmark_metadata_tests)
endif()

install(TARGETS pdfbookmarkMetadata EXPORT pdfbookmarkMetadataTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${METADATA_ROOT}/include/pdfbookmark/core/types.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark/core")
install(FILES "${METADATA_ROOT}/include/pdfbookmark/text/acquisition.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark/text")
install(FILES "${METADATA_ROOT}/include/pdfbookmark/metadata/metadata.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark/metadata")
configure_package_config_file(
  "${METADATA_ROOT}/cmake/pdfbookmarkMetadataConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkMetadataConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkMetadata")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkMetadataConfig.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkMetadata")
install(EXPORT pdfbookmarkMetadataTargets
  FILE pdfbookmarkMetadataTargets.cmake
  NAMESPACE pdfbookmark::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkMetadata")
