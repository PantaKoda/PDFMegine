# Unified pdfbookmark library (S1-S6 + Engine) and its SDK package.
#
#   Target:   pdfbookmark (shared), alias pdfbookmark::pdfbookmark
#   Headers:  <pdfbookmark/pdfbookmark.hpp>  (stable C++ API)
#             <pdfbookmark/pdfbookmark.h>    (stable C API, for any language)
#   Install:  cmake --install <build> --component sdk --prefix <sdk>
#             (install Debug and Release builds into the same prefix)
#   Consume:  find_package(pdfbookmark CONFIG REQUIRED)
#             target_link_libraries(app PRIVATE pdfbookmark::pdfbookmark)
#             pdfbookmark_deploy_runtime(app)
#
# Backends (PDFium, qpdf, ONNX Runtime/OpenCV via the OCR package) are
# private: clients see only project types and link a single target.

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
set(_root "${PROJECT_SOURCE_DIR}")

set(_sources
  # S1 Text Acquisition
  ${_root}/src/text/acquisition.cpp
  ${_root}/src/text/pdf_session.cpp
  ${_root}/src/text/sha256.cpp
  ${_root}/src/text/ocr_adapter_override.cpp
  # S2-S4
  ${_root}/src/detection/detection.cpp
  ${_root}/src/parsing/parsing.cpp
  ${_root}/src/mapping/mapping.cpp
  # S5 Bookmark Writing
  ${_root}/src/writer/plan.cpp
  ${_root}/src/writer/writer.cpp
  # S6 Document Metadata Extraction
  ${_root}/src/metadata/metadata.cpp
  # Engine (orchestration, JSON codecs, library services)
  ${_root}/src/engine/json.cpp
  ${_root}/src/engine/codec.cpp
  ${_root}/src/engine/ledger.cpp
  ${_root}/src/engine/text.cpp
  ${_root}/src/engine/analysis.cpp
  ${_root}/src/engine/analysis_json.cpp
  ${_root}/src/engine/apply.cpp
  ${_root}/src/engine/metadata.cpp
  ${_root}/src/engine/book.cpp
  ${_root}/src/engine/library.cpp
  ${_root}/src/engine/c_api.cpp)
if(PDFBOOKMARK_TEXT_WITH_OCR)
  list(APPEND _sources ${_root}/src/text/ocr_adapter_enabled.cpp)
else()
  list(APPEND _sources ${_root}/src/text/ocr_adapter_disabled.cpp)
endif()

add_library(pdfbookmark SHARED ${_sources})
add_library(pdfbookmark::pdfbookmark ALIAS pdfbookmark)
target_compile_features(pdfbookmark PUBLIC cxx_std_17)
target_include_directories(pdfbookmark
  PUBLIC
    "$<BUILD_INTERFACE:${_root}/include>"
    "$<BUILD_INTERFACE:${PDFBOOKMARK_GENERATED_INCLUDE}>"
    "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
  PRIVATE
    "${_root}/src/text" "${_root}/src/engine" "${_root}/src/writer")
# PDFBOOKMARK_BUILDING_LIBRARY makes PDFB_API export the C functions.
target_compile_definitions(pdfbookmark PRIVATE NOMINMAX PDFBOOKMARK_BUILDING_LIBRARY)
if(PDFBOOKMARK_TEST_HOOKS)
  target_compile_definitions(pdfbookmark PRIVATE PDFBOOKMARK_TEST_HOOKS)
endif()
target_link_libraries(pdfbookmark PRIVATE pdfium qpdf::libqpdf)
if(PDFBOOKMARK_TEXT_WITH_OCR)
  target_link_libraries(pdfbookmark PRIVATE ocr ${OpenCV_LIBS})
  # Only for quieting OpenCV's INFO logging (see ocr_adapter_enabled.cpp).
  target_include_directories(pdfbookmark PRIVATE ${OpenCV_INCLUDE_DIRS})
endif()
set_target_properties(pdfbookmark PROPERTIES
  EXPORT_NAME pdfbookmark
  OUTPUT_NAME pdfbookmark
  DEBUG_POSTFIX d
  VERSION "${PDFBOOKMARK_VERSION}"
  SOVERSION "${PDFBOOKMARK_VERSION_MAJOR}"
  WINDOWS_EXPORT_ALL_SYMBOLS ON
  # Next to the CLI and tests in the build tree, so they run in place.
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
if(MSVC)
  target_compile_options(pdfbookmark PRIVATE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(pdfbookmark PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()

# ---------------------------------------------------------------- API test
# Uses ONLY <pdfbookmark/pdfbookmark.hpp> and the unified target.
if(OCR_BUILD_TESTS)
  add_executable(pdfbookmark_api_tests "${_root}/tests/library/api_test.cpp")
  target_link_libraries(pdfbookmark_api_tests PRIVATE pdfbookmark::pdfbookmark)
  pdfbookmark_use_runtime(pdfbookmark_api_tests)
  set_target_properties(pdfbookmark_api_tests PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
  if(MSVC)
    target_compile_options(pdfbookmark_api_tests PRIVATE /W4 /WX /permissive- /utf-8)
  endif()
  add_test(NAME library_api COMMAND pdfbookmark_api_tests
    "${_root}/tests/engine/fixtures" "${_root}/tests/metadata/fixtures/front_matter.pdf"
    "${PROJECT_BINARY_DIR}/library_api_work")

  # C API, compiled as C (proves the header is plain C).
  enable_language(C)
  add_executable(pdfbookmark_c_api_tests "${_root}/tests/library/c_api_test.c")
  target_link_libraries(pdfbookmark_c_api_tests PRIVATE pdfbookmark::pdfbookmark)
  pdfbookmark_use_runtime(pdfbookmark_c_api_tests)
  set_target_properties(pdfbookmark_c_api_tests PROPERTIES
    C_STANDARD 11 C_STANDARD_REQUIRED ON
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}")
  if(MSVC)
    target_compile_options(pdfbookmark_c_api_tests PRIVATE /W4 /WX /utf-8)
  endif()
  file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/library_c_api_work")
  add_test(NAME library_c_api COMMAND pdfbookmark_c_api_tests
    "${_root}/tests/engine/fixtures" "${_root}/tests/metadata/fixtures/front_matter.pdf"
    "${PROJECT_BINARY_DIR}/library_c_api_work")

  # Python client through the C API (ctypes); skipped without Python 3.
  find_package(Python3 COMPONENTS Interpreter)
  if(Python3_FOUND)
    add_test(NAME library_python COMMAND "${Python3_EXECUTABLE}"
      "${_root}/examples/python/test_pdfbookmark.py"
      "$<TARGET_FILE:pdfbookmark>" "${_root}/tests/engine/fixtures"
      "${PROJECT_BINARY_DIR}/library_python_work")
  endif()
endif()

# ---------------------------------------------------------------- SDK install
set(_sdk sdk)
set(_cmake_dir "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmark")
install(TARGETS pdfbookmark EXPORT pdfbookmarkTargets COMPONENT ${_sdk}
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
# Public headers only (private headers live under src/).
install(DIRECTORY "${_root}/include/pdfbookmark" DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  COMPONENT ${_sdk} FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")
install(FILES "${PDFBOOKMARK_GENERATED_INCLUDE}/pdfbookmark/version.hpp"
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark" COMPONENT ${_sdk})

# Runtime DLLs the library actually loads. Release builds go to bin/, Debug
# builds to bin/debug/ (several dependency DLLs share names across configs).
set(_ocr_runtime_dirs "")
set(_ocr_runtime_files "")
if(PDFBOOKMARK_TEXT_WITH_OCR)
  set(_ocr_runtime_files "\"$<TARGET_FILE:ONNXRuntime::ONNXRuntime>\"")
  set(_ocr_runtime_dirs
    "\"$<TARGET_FILE_DIR:ONNXRuntime::ONNXRuntime>\" \"$<TARGET_FILE_DIR:opencv_world>\"")
endif()
set(_runtime_code [[
  # Normalise paths before the exclude regexes (CMake 4.x policy; the
  # case-insensitive system32 patterns match either way). Silences CMP0207.
  if(POLICY CMP0207)
    cmake_policy(SET CMP0207 NEW)
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME STREQUAL "Debug")
    set(_dest "${CMAKE_INSTALL_PREFIX}/bin/debug")
  else()
    set(_dest "${CMAKE_INSTALL_PREFIX}/bin")
  endif()
  file(GET_RUNTIME_DEPENDENCIES
    LIBRARIES "$<TARGET_FILE:pdfbookmark>"
    RESOLVED_DEPENDENCIES_VAR _deps
    UNRESOLVED_DEPENDENCIES_VAR _missing
    DIRECTORIES "$<TARGET_FILE_DIR:pdfbookmark>" "$<TARGET_FILE_DIR:pdfium>"
                "$<TARGET_FILE_DIR:qpdf::libqpdf>" @_ocr_runtime_dirs@
    PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
    POST_EXCLUDE_REGEXES "[Ss][Yy][Ss][Tt][Ee][Mm]32" "[Ww][Ii][Nn][Ss][Xx][Ss]"
                         "[Ss][Yy][Ss][Ww][Oo][Ww]64")
  foreach(_dep IN LISTS _deps)
    file(INSTALL DESTINATION "${_dest}" TYPE SHARED_LIBRARY FILES "${_dep}")
  endforeach()
  # Windows 11 ships an unrelated System32\onnxruntime.dll, which the scan
  # above resolves first and then excludes; always install the pinned one.
  foreach(_dep @_ocr_runtime_files@)
    file(INSTALL DESTINATION "${_dest}" TYPE SHARED_LIBRARY FILES "${_dep}")
  endforeach()
  # Only optional Windows components may stay unresolved; anything else
  # would be a broken SDK.
  set(_real_missing "")
  foreach(_name IN LISTS _missing)
    if(NOT _name MATCHES "^[Hh][Vv][Ss][Ii]")
      list(APPEND _real_missing "${_name}")
    endif()
  endforeach()
  if(_real_missing)
    message(FATAL_ERROR "Unresolved pdfbookmark runtime dependencies: ${_real_missing}")
  endif()
]])
string(REPLACE "@_ocr_runtime_dirs@" "${_ocr_runtime_dirs}" _runtime_code "${_runtime_code}")
string(REPLACE "@_ocr_runtime_files@" "${_ocr_runtime_files}" _runtime_code "${_runtime_code}")
install(CODE "${_runtime_code}" COMPONENT ${_sdk})

if(PDFBOOKMARK_TEXT_WITH_OCR)
  install(FILES "${_root}/models/det/inference.onnx"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/pdfbookmark/models/det" COMPONENT ${_sdk})
  install(FILES "${_root}/models/rec/inference.onnx" "${_root}/models/rec/charset.txt"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/pdfbookmark/models/rec" COMPONENT ${_sdk})
endif()
# Client documentation: the guide, the JSON reference, and the brief for
# coding agents working on client projects (installed as AGENTS.md).
install(FILES "${_root}/docs/API.md" "${_root}/docs/JSON_FORMATS.md"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/pdfbookmark" COMPONENT ${_sdk})
install(FILES "${_root}/docs/AGENTS_SDK.md" RENAME AGENTS.md
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/pdfbookmark" COMPONENT ${_sdk})
install(DIRECTORY "${_root}/examples/basic" "${_root}/examples/python" "${_root}/examples/qt-quick"
  DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/pdfbookmark/examples" COMPONENT ${_sdk}
  PATTERN "__pycache__" EXCLUDE)
get_filename_component(_vcpkg_share "${qpdf_DIR}/.." ABSOLUTE)
foreach(_notice
    "${PDFium_DIR}/LICENSE|PDFium.txt"
    "${_vcpkg_share}/qpdf/copyright|qpdf.txt"
    "${_vcpkg_share}/zlib/copyright|zlib.txt"
    "${_vcpkg_share}/libjpeg-turbo/copyright|libjpeg-turbo.txt"
    "${ONNXRUNTIME_ROOT}/LICENSE|ONNX-Runtime.txt"
    "${ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt|ONNX-Runtime-third-party-notices.txt"
    "${OpenCV_DIR}/LICENSE|OpenCV.txt"
    "${_root}/PaddleOCR/LICENSE|PaddleOCR-PP-OCR-models.txt")
  string(REPLACE "|" ";" _pair "${_notice}")
  list(GET _pair 0 _from)
  list(GET _pair 1 _to)
  if(EXISTS "${_from}")
    install(FILES "${_from}" DESTINATION "${CMAKE_INSTALL_DATADIR}/doc/pdfbookmark/licenses"
      RENAME "${_to}" COMPONENT ${_sdk})
  endif()
endforeach()

install(EXPORT pdfbookmarkTargets NAMESPACE pdfbookmark::
  FILE pdfbookmarkTargets.cmake DESTINATION "${_cmake_dir}" COMPONENT ${_sdk})
configure_package_config_file("${_root}/cmake/pdfbookmarkConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkConfig.cmake"
  INSTALL_DESTINATION "${_cmake_dir}")
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkConfigVersion.cmake"
  VERSION "${PDFBOOKMARK_VERSION}" COMPATIBILITY SameMajorVersion)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkConfigVersion.cmake"
    "${_root}/cmake/pdfbookmarkDeployRuntime.cmake"
  DESTINATION "${_cmake_dir}" COMPONENT ${_sdk})
