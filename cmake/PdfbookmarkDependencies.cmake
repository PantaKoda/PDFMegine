# Pinned third-party packages and OCR models: one place that says what the
# project depends on, where each piece comes from, and its SHA-256.
#
# A fresh clone needs only the tool chain (Visual Studio with its vcpkg);
# configuring downloads the rest once into <repo>/.deps (shared by every
# build tree) and verifies each file's checksum. See docs/BUILDING.md.
#
#   Package        Version      Source                                    How
#   PDFium         155.0.8057   bblanchon/pdfium-binaries chromium/8057   download
#   ONNX Runtime   1.30.0       microsoft/onnxruntime v1.30.0             download
#   OpenCV         5.0.0        opencv/opencv 5.0.0 Windows package       download
#   qpdf           12.3.2       vcpkg (vcpkg.json, pinned baseline)       vcpkg
#   OCR models     v1           this repository's release "models-v1"     download
#
# To use your own copies, pass PDFium_DIR, ONNXRUNTIME_ROOT or OpenCV_DIR
# (they are then not downloaded), or set PDFBOOKMARK_FETCH_DEPENDENCIES=OFF.
# Offline model install: -DPDFBOOKMARK_MODELS_ARCHIVE=<path to the models zip>.
#
# Why prebuilt packages: they are byte-identical to the binaries the OCR
# parity and all tests were verified with (IMPLEMENTATION_DECISIONS.md E-21).

include_guard(GLOBAL)

get_filename_component(PDFBOOKMARK_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
option(PDFBOOKMARK_FETCH_DEPENDENCIES "Download pinned PDFium, ONNX Runtime and OpenCV" ON)
set(PDFBOOKMARK_DEPS_DIR "${PDFBOOKMARK_SOURCE_ROOT}/.deps" CACHE PATH
  "Download cache for pinned dependencies (shared by build trees)")

# Downloads `url` (checked against `sha256`) and extracts it once into
# <deps>/<name>-<first 12 hash digits>. Sets <out_var> to that directory.
function(pdfbookmark_fetch name url sha256 out_var)
  string(SUBSTRING "${sha256}" 0 12 _short)
  set(_dir "${PDFBOOKMARK_DEPS_DIR}/${name}-${_short}")
  if(NOT EXISTS "${_dir}/.complete")
    set(_archive "${PDFBOOKMARK_DEPS_DIR}/downloads/${name}-${_short}")
    if(NOT EXISTS "${_archive}")
      message(STATUS "Downloading ${name}: ${url}")
      file(DOWNLOAD "${url}" "${_archive}.part" EXPECTED_HASH SHA256=${sha256}
        TLS_VERIFY ON STATUS _status)
      list(GET _status 0 _code)
      if(NOT _code EQUAL 0)
        file(REMOVE "${_archive}.part")
        message(FATAL_ERROR "Download of ${name} failed: ${_status}")
      endif()
      file(RENAME "${_archive}.part" "${_archive}")
    endif()
    file(REMOVE_RECURSE "${_dir}")
    file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_dir}")
    file(TOUCH "${_dir}/.complete")
  endif()
  set(${out_var} "${_dir}" PARENT_SCOPE)
endfunction()

# Makes the named packages (pdfium, onnxruntime, opencv) available, unless
# the caller already pointed CMake at its own copy.
function(pdfbookmark_fetch_dependencies)
  if(NOT PDFBOOKMARK_FETCH_DEPENDENCIES)
    return()
  endif()
  set(_windows_x64 FALSE)
  if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(_windows_x64 TRUE)
  endif()
  foreach(_package IN LISTS ARGN)
    if(_package STREQUAL "pdfium")
      set(_var PDFium_DIR)
    elseif(_package STREQUAL "onnxruntime")
      set(_var ONNXRUNTIME_ROOT)
    elseif(_package STREQUAL "opencv")
      set(_var OpenCV_DIR)
    else()
      message(FATAL_ERROR "Unknown dependency ${_package}")
    endif()
    if(${_var})
      continue()  # Caller-provided copy.
    endif()
    if(NOT _windows_x64)
      message(FATAL_ERROR "Pinned downloads are defined for Windows x64 only so far. "
        "On this platform pass ${_var} (docs/BUILDING.md).")
    endif()
    if(_package STREQUAL "pdfium")
      pdfbookmark_fetch(pdfium
        "https://github.com/bblanchon/pdfium-binaries/releases/download/chromium%2F8057/pdfium-win-x64.tgz"
        e307d519e42f2e69b1b531f0c2a32dffcdf3891ec0eba60328ba51a57cec01ed _dir)
      set(PDFium_DIR "${_dir}" CACHE PATH "PDFium package (pinned download)" FORCE)
    elseif(_package STREQUAL "onnxruntime")
      pdfbookmark_fetch(onnxruntime
        "https://github.com/microsoft/onnxruntime/releases/download/v1.30.0/onnxruntime-win-x64-1.30.0.zip"
        c6ba983baf5681af108599675d2a89c2d145512d02de28aed0bff177cd0ba949 _dir)
      set(ONNXRUNTIME_ROOT "${_dir}/onnxruntime-win-x64-1.30.0" CACHE PATH
        "ONNX Runtime root (pinned download)" FORCE)
    else()
      # The official Windows package is a self-extracting 7-Zip archive.
      pdfbookmark_fetch(opencv
        "https://github.com/opencv/opencv/releases/download/5.0.0/opencv-5.0.0-windows.exe"
        9c6c1fcea58acdf06edba13148b2246e00c2658143fa51e61ecd370db8c39f63 _dir)
      set(OpenCV_DIR "${_dir}/opencv/build" CACHE PATH "OpenCV package (pinned download)" FORCE)
    endif()
  endforeach()
endfunction()

# ------------------------------------------------------------ OCR models
# Kept out of Git (139 MB). Release asset of this (private) repository; the
# download authenticates with GH_TOKEN/GITHUB_TOKEN or the GitHub CLI login.
set(PDFBOOKMARK_MODELS_REPO "PantaKoda/PDFMegine")
set(PDFBOOKMARK_MODELS_TAG "models-v1")
set(PDFBOOKMARK_MODELS_ASSET "pdfbookmark-models-v1.zip")
set(PDFBOOKMARK_MODELS_SHA256 "1cff12a6f1537f081f75758196e9b1b46b9f7e30af66914f2acb89b05ca0c822")
set(PDFBOOKMARK_MODELS_ARCHIVE "" CACHE FILEPATH
  "Local copy of ${PDFBOOKMARK_MODELS_ASSET} (offline alternative to the download)")

function(pdfbookmark_github_token out_var)
  foreach(_name GH_TOKEN GITHUB_TOKEN)
    if(DEFINED ENV{${_name}} AND NOT "$ENV{${_name}}" STREQUAL "")
      set(${out_var} "$ENV{${_name}}" PARENT_SCOPE)
      return()
    endif()
  endforeach()
  find_program(_gh gh)
  if(_gh)
    execute_process(COMMAND "${_gh}" auth token OUTPUT_VARIABLE _token
      RESULT_VARIABLE _result OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(_result EQUAL 0 AND _token)
      set(${out_var} "${_token}" PARENT_SCOPE)
      return()
    endif()
  endif()
  set(${out_var} "" PARENT_SCOPE)
endfunction()

function(pdfbookmark_ensure_models)
  set(_models "${PDFBOOKMARK_SOURCE_ROOT}/models")
  set(_files det/inference.onnx rec/inference.onnx rec/charset.txt)
  set(_missing FALSE)
  foreach(_file IN LISTS _files)
    if(NOT EXISTS "${_models}/${_file}")
      set(_missing TRUE)
    endif()
  endforeach()
  if(NOT _missing)
    return()
  endif()

  set(_archive "${PDFBOOKMARK_MODELS_ARCHIVE}")
  if(NOT _archive)
    set(_archive "${PDFBOOKMARK_DEPS_DIR}/downloads/${PDFBOOKMARK_MODELS_ASSET}")
    if(NOT EXISTS "${_archive}")
      pdfbookmark_github_token(_token)
      set(_how "Log in with the GitHub CLI (gh auth login), set GH_TOKEN, or pass -DPDFBOOKMARK_MODELS_ARCHIVE=<${PDFBOOKMARK_MODELS_ASSET}>.")
      if(NOT _token)
        message(FATAL_ERROR "OCR models are missing from ${_models} and no GitHub "
          "credentials were found to download them. ${_how}")
      endif()
      # Private repository: resolve the asset's API URL, then download it.
      set(_api "https://api.github.com/repos/${PDFBOOKMARK_MODELS_REPO}")
      set(_release "${PDFBOOKMARK_DEPS_DIR}/downloads/release-${PDFBOOKMARK_MODELS_TAG}.json")
      file(DOWNLOAD "${_api}/releases/tags/${PDFBOOKMARK_MODELS_TAG}" "${_release}"
        HTTPHEADER "Authorization: Bearer ${_token}"
        HTTPHEADER "Accept: application/vnd.github+json" STATUS _status)
      list(GET _status 0 _code)
      if(NOT _code EQUAL 0)
        message(FATAL_ERROR "Cannot read release ${PDFBOOKMARK_MODELS_TAG}: ${_status}. ${_how}")
      endif()
      file(READ "${_release}" _json)
      string(JSON _count LENGTH "${_json}" assets)
      set(_url "")
      if(_count GREATER 0)
        math(EXPR _last "${_count} - 1")
        foreach(_i RANGE ${_last})
          string(JSON _name GET "${_json}" assets ${_i} name)
          if(_name STREQUAL PDFBOOKMARK_MODELS_ASSET)
            string(JSON _url GET "${_json}" assets ${_i} url)
          endif()
        endforeach()
      endif()
      if(NOT _url)
        message(FATAL_ERROR "Release ${PDFBOOKMARK_MODELS_TAG} has no ${PDFBOOKMARK_MODELS_ASSET}")
      endif()
      message(STATUS "Downloading OCR models (${PDFBOOKMARK_MODELS_ASSET}, 100 MB)")
      file(DOWNLOAD "${_url}" "${_archive}.part"
        HTTPHEADER "Authorization: Bearer ${_token}"
        HTTPHEADER "Accept: application/octet-stream"
        EXPECTED_HASH SHA256=${PDFBOOKMARK_MODELS_SHA256} STATUS _status)
      list(GET _status 0 _code)
      if(NOT _code EQUAL 0)
        file(REMOVE "${_archive}.part")
        message(FATAL_ERROR "OCR model download failed: ${_status}")
      endif()
      file(RENAME "${_archive}.part" "${_archive}")
    endif()
  endif()
  file(SHA256 "${_archive}" _actual)
  if(NOT _actual STREQUAL PDFBOOKMARK_MODELS_SHA256)
    message(FATAL_ERROR "${_archive} is not ${PDFBOOKMARK_MODELS_ASSET} (SHA-256 mismatch)")
  endif()
  file(ARCHIVE_EXTRACT INPUT "${_archive}" DESTINATION "${_models}")
  message(STATUS "OCR models installed in ${_models}")
endfunction()

# ------------------------------------------------------- licence notices
# Installs the licence notice of every third-party piece the packages ship
# into `destination`; further arguments go to install() (COMPONENT ...).
# A missing notice stops the configure: a package must never lose one
# silently (issue #6). The PP-OCR models' licence is kept in this
# repository, because the models archive holds only the model files.
function(pdfbookmark_install_notices destination)
  get_filename_component(_vcpkg_share "${qpdf_DIR}/.." ABSOLUTE)
  foreach(_notice
      "${PDFium_DIR}/LICENSE|PDFium.txt"
      "${_vcpkg_share}/qpdf/copyright|qpdf.txt"
      "${_vcpkg_share}/zlib/copyright|zlib.txt"
      "${_vcpkg_share}/libjpeg-turbo/copyright|libjpeg-turbo.txt"
      "${ONNXRUNTIME_ROOT}/LICENSE|ONNX-Runtime.txt"
      "${ONNXRUNTIME_ROOT}/ThirdPartyNotices.txt|ONNX-Runtime-third-party-notices.txt"
      "${OpenCV_DIR}/LICENSE|OpenCV.txt"
      "${PDFBOOKMARK_SOURCE_ROOT}/packaging/licenses/PaddleOCR-PP-OCR-models.txt|PaddleOCR-PP-OCR-models.txt")
    string(REPLACE "|" ";" _pair "${_notice}")
    list(GET _pair 0 _from)
    list(GET _pair 1 _to)
    if(NOT EXISTS "${_from}")
      message(FATAL_ERROR "Licence notice ${_to} not found at ${_from}. Packages "
        "must ship every third-party notice (docs/BUILDING.md).")
    endif()
    install(FILES "${_from}" DESTINATION "${destination}" RENAME "${_to}" ${ARGN})
  endforeach()
endfunction()
