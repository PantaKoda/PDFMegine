cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkMapping VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
get_filename_component(MAPPING_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

add_library(pdfbookmarkMapping "${MAPPING_ROOT}/src/mapping/mapping.cpp")
add_library(pdfbookmark::Mapping ALIAS pdfbookmarkMapping)
target_compile_features(pdfbookmarkMapping PUBLIC cxx_std_17)
target_include_directories(pdfbookmarkMapping PUBLIC
  "$<BUILD_INTERFACE:${MAPPING_ROOT}/include>"
  "$<INSTALL_INTERFACE:include>"
)
set_target_properties(pdfbookmarkMapping PROPERTIES
  EXPORT_NAME Mapping
  WINDOWS_EXPORT_ALL_SYMBOLS ON
  POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
  target_compile_options(pdfbookmarkMapping PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(pdfbookmarkMapping PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(PDFBOOKMARK_MAPPING_BUILD_TESTS "Build S4 pure-value tests" ON)
if(PDFBOOKMARK_MAPPING_BUILD_TESTS)
  enable_testing()
  add_executable(pdfbookmark_mapping_tests
    "${MAPPING_ROOT}/tests/mapping/mapping_test.cpp")
  target_compile_features(pdfbookmark_mapping_tests PRIVATE cxx_std_17)
  target_link_libraries(pdfbookmark_mapping_tests PRIVATE pdfbookmarkMapping)
  add_test(NAME mapping_pure COMMAND pdfbookmark_mapping_tests)
endif()

install(TARGETS pdfbookmarkMapping EXPORT pdfbookmarkMappingTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(DIRECTORY "${MAPPING_ROOT}/include/pdfbookmark/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark")
configure_package_config_file(
  "${MAPPING_ROOT}/cmake/pdfbookmarkMappingConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkMappingConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkMapping"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkMappingConfig.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkMapping")
install(EXPORT pdfbookmarkMappingTargets
  FILE pdfbookmarkMappingTargets.cmake
  NAMESPACE pdfbookmark::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkMapping")
