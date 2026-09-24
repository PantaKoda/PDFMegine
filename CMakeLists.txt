cmake_minimum_required(VERSION 3.20)

project(ocr VERSION 0.1.0 LANGUAGES CXX)

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Pinned third-party packages and OCR models (downloaded once, checksummed).
include(PdfbookmarkDependencies)
pdfbookmark_fetch_dependencies(pdfium onnxruntime opencv)
pdfbookmark_ensure_models()

option(BUILD_SHARED_LIBS "Build ocr as a shared library" OFF)
option(OCR_BUILD_TOOLS "Build OCR validation tools" ON)
option(OCR_BUILD_TESTS "Build OCR tests" ON)
option(PDFBOOKMARK_BUILD_TEXT "Build S1 Text Acquisition" OFF)
option(PDFBOOKMARK_TEXT_WITH_OCR "Enable existing OCR package in S1" ON)
option(PDFBOOKMARK_BUILD_DETECTION "Build S2 TOC Detection" OFF)
option(PDFBOOKMARK_BUILD_PARSING "Build S3 TOC Parsing" OFF)
option(PDFBOOKMARK_BUILD_MAPPING "Build S4 Page Mapping" OFF)
option(PDFBOOKMARK_BUILD_WRITER "Build S5 Bookmark Writing (requires qpdf)" OFF)
option(PDFBOOKMARK_BUILD_ENGINE "Build Engine facade and pdfbookmark CLI (requires S1)" OFF)
option(PDFBOOKMARK_BUILD_METADATA "Build S6 Document Metadata Extraction" OFF)
option(PDFBOOKMARK_TEST_HOOKS "Compile test-only seams (fake OCR, write fault injection); OFF for release packages" ON)
option(PDFBOOKMARK_BUILD_LIBRARY
  "Build the unified pdfbookmark shared library and SDK package (enables all subsystems)" OFF)
if(PDFBOOKMARK_BUILD_LIBRARY)
  foreach(_part TEXT DETECTION PARSING MAPPING WRITER ENGINE METADATA)
    set(PDFBOOKMARK_BUILD_${_part} ON CACHE BOOL "" FORCE)
  endforeach()
endif()

# Single source of the pdfbookmark version (library, CLI, packages).
set(PDFBOOKMARK_VERSION "0.1.0" CACHE STRING "pdfbookmark version (MAJOR.MINOR.PATCH)")
if(NOT PDFBOOKMARK_VERSION MATCHES "^([0-9]+)\\.([0-9]+)\\.([0-9]+)$")
  message(FATAL_ERROR "PDFBOOKMARK_VERSION must be MAJOR.MINOR.PATCH")
endif()
set(PDFBOOKMARK_VERSION_MAJOR ${CMAKE_MATCH_1})
set(PDFBOOKMARK_VERSION_MINOR ${CMAKE_MATCH_2})
set(PDFBOOKMARK_VERSION_PATCH ${CMAKE_MATCH_3})
set(PDFBOOKMARK_GENERATED_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
configure_file(cmake/pdfbookmark_version.hpp.in
  "${PDFBOOKMARK_GENERATED_INCLUDE}/pdfbookmark/version.hpp" @ONLY)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs)
find_package(ONNXRuntime REQUIRED)

add_library(clipper2 OBJECT
  third_party/clipper2/src/clipper.engine.cpp
  third_party/clipper2/src/clipper.offset.cpp
)
target_compile_features(clipper2 PUBLIC cxx_std_17)
target_include_directories(clipper2 SYSTEM PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clipper2/include"
)
set_target_properties(clipper2 PROPERTIES POSITION_INDEPENDENT_CODE ON)

add_library(ocr
  src/c_api.cpp
  src/detector.cpp
  src/engine.cpp
  src/geometry.cpp
  src/image_io.cpp
  src/ort_util.cpp
  src/recognizer.cpp
  $<TARGET_OBJECTS:clipper2>
)
add_library(ocr::ocr ALIAS ocr)

target_compile_features(ocr PUBLIC cxx_std_17)
target_include_directories(ocr
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<INSTALL_INTERFACE:include>"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party/clipper2/include"
    "${OpenCV_INCLUDE_DIRS}"
)
target_link_libraries(ocr PRIVATE
  ONNXRuntime::ONNXRuntime
  ${OpenCV_LIBS}
)
set_target_properties(ocr PROPERTIES
  WINDOWS_EXPORT_ALL_SYMBOLS ON
  POSITION_INDEPENDENT_CODE ON
)

# Backend runtime DLLs (PDFium, ONNX Runtime, OpenCV, qpdf) are copied into
# the build root by ONE target, pdfbookmark_runtime_dlls (defined at the end
# of this file). Executables that run from the build root depend on it.
# Why: separate POST_BUILD copies of the same DLL by several executables
# raced in parallel clean builds ("permission denied", E-21).
function(pdfbookmark_use_runtime target)
  add_dependencies(${target} pdfbookmark_runtime_dlls)
endfunction()

function(ocr_enable_strict_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4 /WX /permissive-)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
  endif()
endfunction()

ocr_enable_strict_warnings(ocr)

if(PDFBOOKMARK_BUILD_TEXT)
  find_package(PDFium CONFIG REQUIRED)
  set(pdfbookmark_text_sources
    src/text/acquisition.cpp
    src/text/pdf_session.cpp
    src/text/sha256.cpp
    src/text/ocr_adapter_override.cpp
  )
  if(PDFBOOKMARK_TEXT_WITH_OCR)
    list(APPEND pdfbookmark_text_sources src/text/ocr_adapter_enabled.cpp)
  else()
    list(APPEND pdfbookmark_text_sources src/text/ocr_adapter_disabled.cpp)
  endif()
  add_library(pdfbookmarkText ${pdfbookmark_text_sources})
  add_library(pdfbookmark::Text ALIAS pdfbookmarkText)
  target_compile_features(pdfbookmarkText PUBLIC cxx_std_17)
  target_compile_definitions(pdfbookmarkText PRIVATE NOMINMAX)
  if(PDFBOOKMARK_TEST_HOOKS)
    target_compile_definitions(pdfbookmarkText PRIVATE PDFBOOKMARK_TEST_HOOKS)
  endif()
  target_include_directories(pdfbookmarkText
    PUBLIC
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
      "$<INSTALL_INTERFACE:include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/text"
  )
  target_link_libraries(pdfbookmarkText PRIVATE pdfium)
  if(PDFBOOKMARK_TEXT_WITH_OCR)
    target_link_libraries(pdfbookmarkText PRIVATE ocr)
    # Only for quieting OpenCV's INFO logging (see ocr_adapter_enabled.cpp).
    target_include_directories(pdfbookmarkText PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(pdfbookmarkText PRIVATE ${OpenCV_LIBS})
  endif()
  set_target_properties(pdfbookmarkText PROPERTIES
    EXPORT_NAME Text
    WINDOWS_EXPORT_ALL_SYMBOLS ON
    POSITION_INDEPENDENT_CODE ON
  )
  ocr_enable_strict_warnings(pdfbookmarkText)
  install(TARGETS pdfbookmarkText EXPORT pdfbookmarkTextTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Text
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}" COMPONENT Text
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Text
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
  )
  install(FILES "$<TARGET_FILE:pdfium>"
    DESTINATION "${CMAKE_INSTALL_BINDIR}" COMPONENT Text)
  install(DIRECTORY include/pdfbookmark/
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/pdfbookmark"
    COMPONENT Text)
  configure_package_config_file(
    cmake/pdfbookmarkTextConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkTextConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkText"
  )
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/pdfbookmarkTextConfig.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkText"
    COMPONENT Text
  )
  install(EXPORT pdfbookmarkTextTargets
    FILE pdfbookmarkTextTargets.cmake
    NAMESPACE pdfbookmark::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/pdfbookmarkText"
    COMPONENT Text
  )
endif()

if(OCR_BUILD_TOOLS)
  add_executable(ocr_demo tools/ocr_demo.cpp)
  target_compile_features(ocr_demo PRIVATE cxx_std_17)
  target_link_libraries(ocr_demo PRIVATE ocr)
  ocr_enable_strict_warnings(ocr_demo)
  add_custom_command(TARGET ocr_demo POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E make_directory
            "$<TARGET_FILE_DIR:ocr_demo>/assets/models/det"
            "$<TARGET_FILE_DIR:ocr_demo>/assets/models/rec"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/det/inference.onnx"
            "$<TARGET_FILE_DIR:ocr_demo>/assets/models/det/inference.onnx"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/rec/inference.onnx"
            "$<TARGET_FILE_DIR:ocr_demo>/assets/models/rec/inference.onnx"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/models/rec/charset.txt"
            "$<TARGET_FILE_DIR:ocr_demo>/assets/models/rec/charset.txt"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/corpus/00_plain_screenshot.png"
            "$<TARGET_FILE_DIR:ocr_demo>/assets/sample.png"
  )
  pdfbookmark_use_runtime(ocr_demo)

  add_executable(ocr_model_info tools/model_info.cpp)
  target_compile_features(ocr_model_info PRIVATE cxx_std_17)
  target_include_directories(ocr_model_info PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
  target_link_libraries(ocr_model_info PRIVATE ocr ONNXRuntime::ONNXRuntime)
  ocr_enable_strict_warnings(ocr_model_info)
  pdfbookmark_use_runtime(ocr_model_info)

  add_executable(ocr_compare_golden tools/compare_golden.cpp)
  target_compile_features(ocr_compare_golden PRIVATE cxx_std_17)
  target_include_directories(ocr_compare_golden PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/tools"
    "${OpenCV_INCLUDE_DIRS}"
  )
  target_link_libraries(ocr_compare_golden PRIVATE
    ocr
    ONNXRuntime::ONNXRuntime
    ${OpenCV_LIBS}
  )
  ocr_enable_strict_warnings(ocr_compare_golden)
  pdfbookmark_use_runtime(ocr_compare_golden)
endif()

if(OCR_BUILD_TESTS)
  enable_testing()
  if(PDFBOOKMARK_BUILD_TEXT)
    add_executable(pdfbookmark_text_smoke tests/text/smoke.cpp)
    target_compile_features(pdfbookmark_text_smoke PRIVATE cxx_std_17)
    target_include_directories(pdfbookmark_text_smoke PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src/text")
    target_link_libraries(pdfbookmark_text_smoke PRIVATE pdfbookmarkText)
    ocr_enable_strict_warnings(pdfbookmark_text_smoke)
    pdfbookmark_use_runtime(pdfbookmark_text_smoke)
    if(PDFBOOKMARK_TEXT_WITH_OCR)
      add_executable(pdfbookmark_text_ocr_integration
        tests/text/ocr_integration.cpp)
      target_compile_features(pdfbookmark_text_ocr_integration PRIVATE cxx_std_17)
      target_compile_definitions(pdfbookmark_text_ocr_integration PRIVATE NOMINMAX)
      target_link_libraries(pdfbookmark_text_ocr_integration PRIVATE
        pdfbookmarkText ocr pdfium)
      ocr_enable_strict_warnings(pdfbookmark_text_ocr_integration)
      pdfbookmark_use_runtime(pdfbookmark_text_ocr_integration)
      add_test(NAME text_ocr_integration COMMAND pdfbookmark_text_ocr_integration
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/text/fixtures/image_only.pdf"
        "${CMAKE_CURRENT_SOURCE_DIR}/models/det/inference.onnx"
        "${CMAKE_CURRENT_SOURCE_DIR}/models/rec/inference.onnx"
        "${CMAKE_CURRENT_SOURCE_DIR}/models/rec/charset.txt")
    endif()
    add_test(NAME text_smoke COMMAND pdfbookmark_text_smoke
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/text/fixtures/basic.pdf")
    if(PDFBOOKMARK_TEST_HOOKS)
      add_executable(pdfbookmark_text_policy tests/text/policy.cpp)
      target_compile_features(pdfbookmark_text_policy PRIVATE cxx_std_17)
      target_include_directories(pdfbookmark_text_policy PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src/text")
      target_link_libraries(pdfbookmark_text_policy PRIVATE pdfbookmarkText)
      ocr_enable_strict_warnings(pdfbookmark_text_policy)
      pdfbookmark_use_runtime(pdfbookmark_text_policy)
      add_test(NAME text_policy COMMAND pdfbookmark_text_policy
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/text/fixtures/basic.pdf")
      target_compile_definitions(pdfbookmark_text_policy PRIVATE
        PDFBOOKMARK_TEST_HOOKS)
    endif()
  endif()
  if(OCR_BUILD_TOOLS)
    add_test(
      NAME model_info
      COMMAND ocr_model_info
              "${CMAKE_CURRENT_SOURCE_DIR}/models/det/inference.onnx"
              "${CMAKE_CURRENT_SOURCE_DIR}/models/rec/inference.onnx"
    )
    # golden/ (2.3 GB of reference outputs) is local-only; regenerate it
    # with tools/generate_golden.py (docs/ocr/README.md).
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/golden")
    add_test(
      NAME golden_parity
      COMMAND ocr_compare_golden
              "${CMAKE_CURRENT_SOURCE_DIR}/golden"
              "${CMAKE_CURRENT_SOURCE_DIR}/models/det/inference.onnx"
              "${CMAKE_CURRENT_SOURCE_DIR}/models/rec/inference.onnx"
              "${CMAKE_CURRENT_SOURCE_DIR}/models/rec/charset.txt"
    )
    else()
      message(STATUS "golden/ not present: OCR parity test (golden_parity) not registered")
    endif()
  endif()
endif()

install(
  TARGETS ocr
  EXPORT ocrTargets
  ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
  INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
  DIRECTORY include/
  DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
)
install(
  FILES cmake/FindONNXRuntime.cmake
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ocr"
)
install(
  FILES
    "$<TARGET_FILE:ONNXRuntime::ONNXRuntime>"
    "$<TARGET_FILE:opencv_world>"
  DESTINATION "${CMAKE_INSTALL_BINDIR}"
)
configure_package_config_file(
  cmake/ocrConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/ocrConfig.cmake"
  INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ocr"
)
write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/ocrConfigVersion.cmake"
  VERSION "${PROJECT_VERSION}"
  COMPATIBILITY SameMajorVersion
)
install(
  FILES
    "${CMAKE_CURRENT_BINARY_DIR}/ocrConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/ocrConfigVersion.cmake"
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ocr"
)
install(
  EXPORT ocrTargets
  FILE ocrTargets.cmake
  NAMESPACE ocr::
  DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/ocr"
)

if(PDFBOOKMARK_BUILD_DETECTION)
  add_subdirectory(subsystems/toc_detection)
endif()

if(PDFBOOKMARK_BUILD_PARSING)
  add_subdirectory(subsystems/toc_parsing)
endif()

if(PDFBOOKMARK_BUILD_MAPPING)
  add_subdirectory(subsystems/page_mapping)
endif()

if(PDFBOOKMARK_BUILD_WRITER)
  add_subdirectory(subsystems/bookmark_writing)
endif()

if(PDFBOOKMARK_BUILD_METADATA)
  add_subdirectory(subsystems/document_metadata)
endif()

# Engine (orchestration) and CLI (client). Not domain subsystems.
if(PDFBOOKMARK_BUILD_ENGINE)
  foreach(_required TEXT DETECTION PARSING MAPPING WRITER)
    if(NOT PDFBOOKMARK_BUILD_${_required})
      message(FATAL_ERROR "PDFBOOKMARK_BUILD_ENGINE requires PDFBOOKMARK_BUILD_${_required}")
    endif()
  endforeach()
  # qpdf is private to S5; imported targets are directory scoped, so the
  # Engine block finds it again only to stage runtime DLLs and for tests.
  find_package(qpdf CONFIG REQUIRED)
  add_library(pdfbookmarkEngine
    src/engine/json.cpp src/engine/codec.cpp src/engine/ledger.cpp
    src/engine/text.cpp src/engine/analysis.cpp src/engine/analysis_json.cpp
    src/engine/apply.cpp src/engine/library.cpp)
  add_library(pdfbookmark::Engine ALIAS pdfbookmarkEngine)
  target_compile_features(pdfbookmarkEngine PUBLIC cxx_std_17)
  target_include_directories(pdfbookmarkEngine
    PUBLIC
      "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
      "$<BUILD_INTERFACE:${PDFBOOKMARK_GENERATED_INCLUDE}>"
      "$<INSTALL_INTERFACE:include>"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/engine")
  target_link_libraries(pdfbookmarkEngine PUBLIC pdfbookmarkText
    pdfbookmarkTocDetection pdfbookmarkTocParsing pdfbookmarkMapping
    pdfbookmarkWriterPlan pdfbookmarkWriter)
  # S6 is optional: Engine gains extract_metadata() and the CLI its
  # 'metadata' command only when the subsystem is built.
  if(PDFBOOKMARK_BUILD_METADATA)
    target_sources(pdfbookmarkEngine PRIVATE src/engine/metadata.cpp)
    target_link_libraries(pdfbookmarkEngine PUBLIC pdfbookmarkMetadata)
  endif()
  set_target_properties(pdfbookmarkEngine PROPERTIES
    WINDOWS_EXPORT_ALL_SYMBOLS ON POSITION_INDEPENDENT_CODE ON)
  ocr_enable_strict_warnings(pdfbookmarkEngine)

  add_library(pdfbookmark_cli_selection OBJECT apps/cli/page_selection.cpp)
  target_include_directories(pdfbookmark_cli_selection PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/apps/cli"
    "${CMAKE_CURRENT_SOURCE_DIR}/include")
  target_compile_features(pdfbookmark_cli_selection PUBLIC cxx_std_17)
  ocr_enable_strict_warnings(pdfbookmark_cli_selection)
  add_executable(pdfbookmarkCli apps/cli/main.cpp
    $<TARGET_OBJECTS:pdfbookmark_cli_selection>)
  target_include_directories(pdfbookmarkCli PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/apps/cli")
  if(PDFBOOKMARK_BUILD_LIBRARY)
    target_link_libraries(pdfbookmarkCli PRIVATE pdfbookmark::pdfbookmark)
  else()
    target_link_libraries(pdfbookmarkCli PRIVATE pdfbookmarkEngine)
  endif()
  set_target_properties(pdfbookmarkCli PROPERTIES OUTPUT_NAME pdfbookmark)
  if(PDFBOOKMARK_BUILD_METADATA)
    target_compile_definitions(pdfbookmarkCli PRIVATE PDFBOOKMARK_WITH_METADATA)
  endif()
  ocr_enable_strict_warnings(pdfbookmarkCli)
  set(pdfbookmark_engine_runtime "$<TARGET_FILE:pdfium>")
  if(PDFBOOKMARK_TEXT_WITH_OCR)
    list(APPEND pdfbookmark_engine_runtime
      "$<TARGET_FILE:ONNXRuntime::ONNXRuntime>" "$<TARGET_FILE:opencv_world>")
  endif()
  if(BUILD_SHARED_LIBS)
    list(APPEND pdfbookmark_engine_runtime
      "$<TARGET_FILE:pdfbookmarkEngine>" "$<TARGET_FILE:pdfbookmarkText>")
    if(PDFBOOKMARK_TEXT_WITH_OCR)
      list(APPEND pdfbookmark_engine_runtime "$<TARGET_FILE:ocr>")
    endif()
  endif()
  if(BUILD_SHARED_LIBS)
    list(APPEND pdfbookmark_engine_runtime
      "$<TARGET_FILE:pdfbookmarkTocDetection>" "$<TARGET_FILE:pdfbookmarkTocParsing>"
      "$<TARGET_FILE:pdfbookmarkMapping>" "$<TARGET_FILE:pdfbookmarkWriterPlan>"
      "$<TARGET_FILE:pdfbookmarkWriter>")
  endif()
  # Engine executables run from the build root with the shared runtime DLLs
  # (pdfbookmark_engine_runtime is copied by pdfbookmark_runtime_dlls).
  function(pdfbookmark_engine_runtime_for target)
    pdfbookmark_use_runtime(${target})
  endfunction()
  pdfbookmark_engine_runtime_for(pdfbookmarkCli)

  # End-user package (allow-list): cmake --install <build> --component app
  #   --prefix <folder>. Build it from a Release configuration with
  #   PDFBOOKMARK_TEST_HOOKS=OFF. Nothing else (tests, tools, headers, CMake
  #   files, reference data) is part of this component.
  set(_app app)
  install(TARGETS pdfbookmarkCli RUNTIME DESTINATION .
          COMPONENT ${_app} EXCLUDE_FROM_ALL)
  # Only DLLs the executable actually loads; Windows system DLLs excluded.
  install(CODE [[
    file(GET_RUNTIME_DEPENDENCIES
      EXECUTABLES "$<TARGET_FILE:pdfbookmarkCli>"
      RESOLVED_DEPENDENCIES_VAR _deps
      UNRESOLVED_DEPENDENCIES_VAR _missing
      DIRECTORIES "$<TARGET_FILE_DIR:pdfbookmarkCli>"
                  "$<TARGET_FILE_DIR:pdfium>"
                  "$<TARGET_FILE_DIR:qpdf::libqpdf>"
      PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
      POST_EXCLUDE_REGEXES "[Ss][Yy][Ss][Tt][Ee][Mm]32" "[Ww][Ii][Nn][Ss][Xx][Ss]"
                           "[Ss][Yy][Ss][Ww][Oo][Ww]64")
    foreach(_dep IN LISTS _deps)
      file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}" TYPE SHARED_LIBRARY
           FILES "${_dep}")
    endforeach()
    # Unresolved names are optional Windows components referenced by system
    # DLLs; the package is verified by running it in a clean environment.
    if(_missing)
      message(STATUS "Not packaged (not found; expected to be OS-provided): ${_missing}")
    endif()
  ]] COMPONENT ${_app} EXCLUDE_FROM_ALL)
  # Microsoft C++ runtime, so no separate redistributable install is needed.
  set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
  set(CMAKE_INSTALL_UCRT_LIBRARIES FALSE)
  include(InstallRequiredSystemLibraries)
  # Only the runtime DLLs the packaged binaries import (checked with
  # dumpbin /dependents): msvcp140, msvcp140_1, vcruntime140(_1), concrt140.
  set(_vc_runtime ${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS})
  list(FILTER _vc_runtime INCLUDE REGEX
       "[/\](msvcp140|msvcp140_1|vcruntime140|vcruntime140_1|concrt140)\.dll$")
  if(_vc_runtime)
    install(PROGRAMS ${_vc_runtime} DESTINATION .
            COMPONENT ${_app} EXCLUDE_FROM_ALL)
  endif()
  if(PDFBOOKMARK_TEXT_WITH_OCR)
    install(FILES models/det/inference.onnx DESTINATION models/det
            COMPONENT ${_app} EXCLUDE_FROM_ALL)
    install(FILES models/rec/inference.onnx models/rec/charset.txt
            DESTINATION models/rec COMPONENT ${_app} EXCLUDE_FROM_ALL)
  endif()
  install(FILES packaging/README.txt DESTINATION .
          COMPONENT ${_app} EXCLUDE_FROM_ALL)
  install(PROGRAMS "packaging/Add bookmarks.cmd"
                   "packaging/Add bookmarks (allow partial).cmd"
                   packaging/Install.cmd packaging/Uninstall.cmd
                   packaging/install.ps1 packaging/uninstall.ps1
          DESTINATION . COMPONENT ${_app} EXCLUDE_FROM_ALL)
  get_filename_component(_vcpkg_share "${qpdf_DIR}/.." ABSOLUTE)
  foreach(_notice
      "${PDFium_DIR}/LICENSE|PDFium.txt"
      "${_vcpkg_share}/qpdf/copyright|qpdf.txt"
      "${_vcpkg_share}/zlib/copyright|zlib.txt"
      "${_vcpkg_share}/libjpeg-turbo/copyright|libjpeg-turbo.txt"
      "${ONNXRUNTIME_ROOT}/LICENSE|ONNX-Runtime.txt"
      "${ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt|ONNX-Runtime-third-party-notices.txt"
      "${OpenCV_DIR}/LICENSE|OpenCV.txt"
      "${CMAKE_CURRENT_SOURCE_DIR}/PaddleOCR/LICENSE|PaddleOCR-PP-OCR-models.txt")
    string(REPLACE "|" ";" _pair "${_notice}")
    list(GET _pair 0 _from)
    list(GET _pair 1 _to)
    if(EXISTS "${_from}")
      install(FILES "${_from}" DESTINATION licenses RENAME "${_to}"
              COMPONENT ${_app} EXCLUDE_FROM_ALL)
    else()
      message(WARNING "Licence notice not found for the app package: ${_from}")
    endif()
  endforeach()

  if(OCR_BUILD_TESTS)
    add_executable(pdfbookmark_engine_text_tests tests/engine/engine_text_test.cpp)
    target_include_directories(pdfbookmark_engine_text_tests PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src/engine")
    target_link_libraries(pdfbookmark_engine_text_tests PRIVATE pdfbookmarkEngine)
    ocr_enable_strict_warnings(pdfbookmark_engine_text_tests)
    pdfbookmark_engine_runtime_for(pdfbookmark_engine_text_tests)
    set(pdfbookmark_engine_fixture
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/mapping/fixtures/acquired_mapping.pdf")
    if(PDFBOOKMARK_TEXT_WITH_OCR)
      add_test(NAME engine_text COMMAND pdfbookmark_engine_text_tests
        "${pdfbookmark_engine_fixture}" "${CMAKE_CURRENT_SOURCE_DIR}/models")
    else()
      add_test(NAME engine_text COMMAND pdfbookmark_engine_text_tests
        "${pdfbookmark_engine_fixture}")
    endif()

    add_executable(pdfbookmark_engine_analysis_tests
      tests/engine/engine_analysis_test.cpp)
    target_include_directories(pdfbookmark_engine_analysis_tests PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/src/engine")
    target_link_libraries(pdfbookmark_engine_analysis_tests PRIVATE pdfbookmarkEngine)
    ocr_enable_strict_warnings(pdfbookmark_engine_analysis_tests)
    pdfbookmark_engine_runtime_for(pdfbookmark_engine_analysis_tests)
    add_test(NAME engine_analysis COMMAND pdfbookmark_engine_analysis_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/engine/fixtures")

    add_executable(pdfbookmark_engine_apply_tests tests/engine/engine_apply_test.cpp)
    target_link_libraries(pdfbookmark_engine_apply_tests PRIVATE
      pdfbookmarkEngine qpdf::libqpdf)
    ocr_enable_strict_warnings(pdfbookmark_engine_apply_tests)
    if(MSVC)
      target_compile_options(pdfbookmark_engine_apply_tests PRIVATE /utf-8)
    endif()
    pdfbookmark_engine_runtime_for(pdfbookmark_engine_apply_tests)
    add_test(NAME engine_apply COMMAND pdfbookmark_engine_apply_tests
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/engine/fixtures"
      "${CMAKE_CURRENT_BINARY_DIR}/engine_apply_work")

    if(PDFBOOKMARK_BUILD_METADATA)
      add_executable(pdfbookmark_engine_metadata_tests tests/engine/engine_metadata_test.cpp)
      target_link_libraries(pdfbookmark_engine_metadata_tests PRIVATE pdfbookmarkEngine)
      ocr_enable_strict_warnings(pdfbookmark_engine_metadata_tests)
      pdfbookmark_engine_runtime_for(pdfbookmark_engine_metadata_tests)
      add_test(NAME engine_metadata COMMAND pdfbookmark_engine_metadata_tests
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/metadata/fixtures/front_matter.pdf")
      add_test(NAME cli_metadata COMMAND "${CMAKE_COMMAND}"
        "-DCLI=$<TARGET_FILE:pdfbookmarkCli>"
        "-DPDF=${CMAKE_CURRENT_SOURCE_DIR}/tests/metadata/fixtures/front_matter.pdf"
        "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_metadata_work"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/cli_metadata_test.cmake")
    endif()

    add_executable(pdfbookmark_cli_selection_tests
      tests/cli/page_selection_test.cpp
      $<TARGET_OBJECTS:pdfbookmark_cli_selection>)
    target_include_directories(pdfbookmark_cli_selection_tests PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/apps/cli" "${CMAKE_CURRENT_SOURCE_DIR}/include")
    target_compile_features(pdfbookmark_cli_selection_tests PRIVATE cxx_std_17)
    ocr_enable_strict_warnings(pdfbookmark_cli_selection_tests)
    add_test(NAME cli_page_selection COMMAND pdfbookmark_cli_selection_tests)
    add_test(NAME cli_text COMMAND "${CMAKE_COMMAND}"
      "-DCLI=$<TARGET_FILE:pdfbookmarkCli>"
      "-DPDF=${pdfbookmark_engine_fixture}"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_text_work"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/cli_text_test.cmake")
    add_test(NAME cli_analyze COMMAND "${CMAKE_COMMAND}"
      "-DCLI=$<TARGET_FILE:pdfbookmarkCli>"
      "-DFIX=${CMAKE_CURRENT_SOURCE_DIR}/tests/engine/fixtures"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_analyze_work"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/cli_analyze_test.cmake")
    add_test(NAME cli_apply COMMAND "${CMAKE_COMMAND}"
      "-DCLI=$<TARGET_FILE:pdfbookmarkCli>"
      "-DFIX=${CMAKE_CURRENT_SOURCE_DIR}/tests/engine/fixtures"
      "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_apply_work"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli/cli_apply_test.cmake")
  endif()
endif()

# Unified pdfbookmark library and SDK package (library/CMakeLists.txt).
if(PDFBOOKMARK_BUILD_LIBRARY)
  add_subdirectory(library)
endif()

# Distributable ZIP of the end-user package: cpack --config <build>/CPackConfig.cmake
if(PDFBOOKMARK_BUILD_ENGINE)
  set(CPACK_PACKAGE_NAME "pdfbookmark")
  set(CPACK_PACKAGE_VENDOR "PDF Bookmark")
  set(CPACK_PACKAGE_VERSION "${PDFBOOKMARK_VERSION}")
  set(CPACK_PACKAGE_FILE_NAME "pdfbookmark-${PDFBOOKMARK_VERSION}-win64")
  set(CPACK_GENERATOR "ZIP")
  set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
  set(CPACK_COMPONENTS_ALL app)
  set(CPACK_COMPONENTS_GROUPING ALL_COMPONENTS_IN_ONE)
  set(CPACK_ARCHIVE_FILE_NAME "pdfbookmark-${PDFBOOKMARK_VERSION}-win64")
  set(CPACK_COMPONENT_INCLUDE_TOPLEVEL_DIRECTORY ON)  # One folder in the ZIP.
  set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package")
  set(CPACK_VERBATIM_VARIABLES ON)
  include(CPack)
endif()

# ---------------------------------------------------------------- runtime DLLs
# One copy step for every backend DLL the build-root executables load (see
# pdfbookmark_use_runtime). It runs on each build; unchanged files are skipped.
set(_runtime "$<TARGET_FILE:ONNXRuntime::ONNXRuntime>" "$<TARGET_FILE:opencv_world>")
if(TARGET pdfium)
  list(APPEND _runtime "$<TARGET_FILE:pdfium>")
endif()
if(DEFINED pdfbookmark_engine_runtime)
  list(APPEND _runtime ${pdfbookmark_engine_runtime})
endif()
list(REMOVE_DUPLICATES _runtime)
set(_runtime_commands
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different ${_runtime} "${PROJECT_BINARY_DIR}")
if(TARGET qpdf::libqpdf)
  list(APPEND _runtime_commands
    COMMAND "${CMAKE_COMMAND}" "-DSRC=$<TARGET_FILE_DIR:qpdf::libqpdf>"
            "-DDST=${PROJECT_BINARY_DIR}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/pdfbookmarkCopyDlls.cmake")
endif()
add_custom_target(pdfbookmark_runtime_dlls ${_runtime_commands}
  COMMENT "Copying backend runtime DLLs into the build root" VERBATIM)
