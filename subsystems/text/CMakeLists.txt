cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkText VERSION 0.1.0 LANGUAGES CXX)

# Standalone EmbeddedOnly build. This entry point intentionally does not find
# OpenCV, ONNX Runtime, or the existing OCR package.
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)
get_filename_component(S1_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(PDFBOOKMARK_TEXT_WITH_OCR OFF)
include("${S1_ROOT}/cmake/PdfbookmarkDependencies.cmake")
pdfbookmark_fetch_dependencies(pdfium)
find_package(PDFium CONFIG REQUIRED)

add_library(pdfbookmarkText
  "${S1_ROOT}/src/text/acquisition.cpp"
  "${S1_ROOT}/src/text/pdf_session.cpp"
  "${S1_ROOT}/src/text/sha256.cpp"
  "${S1_ROOT}/src/text/ocr_adapter_override.cpp"
  "${S1_ROOT}/src/text/ocr_adapter_disabled.cpp"
)
add_library(pdfbookmark::Text ALIAS pdfbookmarkText)
target_compile_features(pdfbookmarkText PUBLIC cxx_std_17)
target_compile_definitions(pdfbookmarkText PRIVATE NOMINMAX)
target_include_directories(pdfbookmarkText PUBLIC
  "$<BUILD_INTERFACE:${S1_ROOT}/include>"
  "$<INSTALL_INTERFACE:include>"
)
target_link_libraries(pdfbookmarkText PRIVATE pdfium)
set_target_properties(pdfbookmarkText PROPERTIES
  EXPORT_NAME Text
  WINDOWS_EXPORT_ALL_SYMBOLS ON
  POSITION_INDEPENDENT_CODE ON
)
if(MSVC)
  target_compile_options(pdfbookmarkText PRIVATE /W4 /WX /permissive-)
else()
  target_compile_options(pdfbookmarkText PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

option(PDFBOOKMARK_TEXT_BUILD_TESTS "Build standalone S1 native test" ON)
if(PDFBOOKMARK_TEXT_BUILD_TESTS)
  enable_testing()
  add_executable(pdfbookmark_text_smoke "${S1_ROOT}/tests/text/smoke.cpp")
  target_compile_features(pdfbookmark_text_smoke PRIVATE cxx_std_17)
  target_include_directories(pdfbookmark_text_smoke PRIVATE
    "${S1_ROOT}/src/text")
  target_link_libraries(pdfbookmark_text_smoke PRIVATE pdfbookmarkText)
  add_custom_command(TARGET pdfbookmark_text_smoke POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "$<TARGET_FILE:pdfium>"
            "$<TARGET_FILE_DIR:pdfbookmark_text_smoke>"
  )
  add_test(NAME text_smoke COMMAND pdfbookmark_text_smoke
    "${S1_ROOT}/tests/text/fixtures/basic.pdf")
endif()

install(TARGETS pdfbookmarkText EXPORT pdfbookmarkTextTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(FILES "$<TARGET_FILE:pdfium>" DESTINATION "${CMAKE_INSTALL_BINDIR}")
install(DIRECTORY "${S1_ROOT}/include/pdfbookmark/"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark")
configure_package_config_file(
  "${S1_ROOT}/cmake/pdfbookmarkTextConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkTextConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkText"
)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkTextConfig.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkText")
install(EXPORT pdfbookmarkTextTargets
  FILE pdfbookmarkTextTargets.cmake
  NAMESPACE pdfbookmark::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkText")

