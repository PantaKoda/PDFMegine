cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkWriter VERSION 0.1.0 LANGUAGES CXX)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
get_filename_component(WRITER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

# Backend-free plan contract and structural validator (no qpdf).
add_library(pdfbookmarkWriterPlan "${WRITER_ROOT}/src/writer/plan.cpp")
add_library(pdfbookmark::WriterPlan ALIAS pdfbookmarkWriterPlan)

# qpdf-backed writer; qpdf stays a private implementation dependency.
find_package(qpdf CONFIG REQUIRED)
add_library(pdfbookmarkWriter "${WRITER_ROOT}/src/writer/writer.cpp")
add_library(pdfbookmark::Writer ALIAS pdfbookmarkWriter)
target_link_libraries(pdfbookmarkWriter
  PUBLIC pdfbookmarkWriterPlan
  PRIVATE qpdf::libqpdf)

foreach(_target pdfbookmarkWriterPlan pdfbookmarkWriter)
  target_compile_features(${_target} PUBLIC cxx_std_17)
  target_include_directories(${_target} PUBLIC
    "$<BUILD_INTERFACE:${WRITER_ROOT}/include>"
    "$<INSTALL_INTERFACE:include>")
  set_target_properties(${_target} PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    POSITION_INDEPENDENT_CODE ON)
  if(MSVC)
    target_compile_options(${_target} PRIVATE /W4 /WX /permissive- /utf-8)
  else()
    target_compile_options(${_target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endforeach()
set_target_properties(pdfbookmarkWriterPlan PROPERTIES EXPORT_NAME WriterPlan)
set_target_properties(pdfbookmarkWriter PROPERTIES EXPORT_NAME Writer)

option(PDFBOOKMARK_TEST_HOOKS "Compile test-only write fault injection" ON)
if(PDFBOOKMARK_TEST_HOOKS)
  target_compile_definitions(pdfbookmarkWriter PRIVATE PDFBOOKMARK_TEST_HOOKS)
endif()

set(_copy_dlls "${WRITER_ROOT}/cmake/pdfbookmarkCopyDlls.cmake")

option(PDFBOOKMARK_WRITER_BUILD_TESTS "Build S5 tests" ON)
if(PDFBOOKMARK_WRITER_BUILD_TESTS)
  enable_testing()
  # Links only the backend-free plan library: proves no qpdf is required.
  add_executable(pdfbookmark_writer_plan_tests
    "${WRITER_ROOT}/tests/writer/plan_test.cpp")
  target_link_libraries(pdfbookmark_writer_plan_tests PRIVATE pdfbookmarkWriterPlan)
  if(MSVC)
    target_compile_options(pdfbookmark_writer_plan_tests PRIVATE /utf-8)
  endif()
  add_test(NAME writer_plan COMMAND pdfbookmark_writer_plan_tests)

  # Writer tests build fixture PDFs with qpdf and use the private fault seam.
  add_executable(pdfbookmark_writer_tests
    "${WRITER_ROOT}/tests/writer/writer_test.cpp")
  target_include_directories(pdfbookmark_writer_tests PRIVATE
    "${WRITER_ROOT}/src/writer")
  target_link_libraries(pdfbookmark_writer_tests PRIVATE
    pdfbookmarkWriter qpdf::libqpdf)
  if(PDFBOOKMARK_TEST_HOOKS)
    target_compile_definitions(pdfbookmark_writer_tests PRIVATE PDFBOOKMARK_TEST_HOOKS)
  endif()
  if(MSVC)
    target_compile_options(pdfbookmark_writer_tests PRIVATE /utf-8)
  endif()
  if(WIN32)
    add_custom_command(TARGET pdfbookmark_writer_tests POST_BUILD
      COMMAND "${CMAKE_COMMAND}"
              "-DSRC=$<TARGET_FILE_DIR:qpdf::libqpdf>"
              "-DDST=$<TARGET_FILE_DIR:pdfbookmark_writer_tests>"
              -P "${_copy_dlls}")
  endif()
  add_test(NAME writer_qpdf COMMAND pdfbookmark_writer_tests
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}")
endif()

install(TARGETS pdfbookmarkWriterPlan pdfbookmarkWriter
  EXPORT pdfbookmarkWriterTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES
  "${WRITER_ROOT}/include/pdfbookmark/core/types.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark/core")
install(FILES
  "${WRITER_ROOT}/include/pdfbookmark/writer/plan.hpp"
  "${WRITER_ROOT}/include/pdfbookmark/writer/writer.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark/writer")
if(WIN32)
  # Ship qpdf's runtime DLLs next to installed binaries for consumers.
  install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\"
    \"-DSRC=$<TARGET_FILE_DIR:qpdf::libqpdf>\"
    \"-DDST=\${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}\"
    -P \"${_copy_dlls}\")")
endif()
configure_package_config_file(
  "${WRITER_ROOT}/cmake/pdfbookmarkWriterConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkWriterConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkWriter")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkWriterConfig.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkWriter")
install(EXPORT pdfbookmarkWriterTargets
  FILE pdfbookmarkWriterTargets.cmake
  NAMESPACE pdfbookmark::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkWriter")
