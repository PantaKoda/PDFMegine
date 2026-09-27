# CLI analyze/add --metadata at the process boundary (PR #4 review):
# conflicting output destinations are rejected before any work or output,
# and one run-wide --ocr-budget caps OCR across both stages.
# Usage: cmake -DCLI=<pdfbookmark> -DFIXTURES=<tests/engine/fixtures>
#              -DMODELS=<models dir> -DWORK=<dir> -P cli_book_test.cmake
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(BOOK "${FIXTURES}/boundary.pdf")
set(SCAN "${FIXTURES}/scan4.pdf")

function(expect_exit code)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc STREQUAL "${code}")
    message(FATAL_ERROR "Expected exit ${code}, got ${rc}: ${ARGN}\n${out}\n${err}")
  endif()
  set(LAST_ERR "${err}" PARENT_SCOPE)
endfunction()

function(expect_absent)
  foreach(path IN LISTS ARGN)
    if(EXISTS "${path}")
      message(FATAL_ERROR "Rejected run still wrote ${path}")
    endif()
  endforeach()
endfunction()

# 1. Collisions: exit 2 (usage) before analysis, nothing written.
expect_exit(2 "${CLI}" analyze "${BOOK}" --mode embedded
            --report "${WORK}/same.json" --metadata "${WORK}/same.json")
if(NOT LAST_ERR MATCHES "--report and --metadata name the same file")
  message(FATAL_ERROR "Unexpected collision message: ${LAST_ERR}")
endif()
expect_exit(2 "${CLI}" analyze "${BOOK}" --mode embedded
            --report "${WORK}/same.json" --metadata "${WORK}/./same.json")
expect_exit(2 "${CLI}" analyze "${BOOK}" --mode embedded
            --plan "${WORK}/p.json" --metadata "${WORK}/p.json")
expect_exit(2 "${CLI}" add "${BOOK}" --mode embedded
            --output "${WORK}/o.pdf" --report "${WORK}/o.pdf")
expect_absent("${WORK}/same.json" "${WORK}/p.json" "${WORK}/o.pdf")
if(WIN32)
  expect_exit(2 "${CLI}" analyze "${BOOK}" --mode embedded
              --report "${WORK}/Case.json" --metadata "${WORK}/case.json")
  expect_absent("${WORK}/Case.json")
endif()
# An existing file named twice, even with --force, is rejected and unchanged.
file(WRITE "${WORK}/existing.json" "keep")
expect_exit(2 "${CLI}" analyze "${BOOK}" --mode embedded --force
            --report "${WORK}/existing.json" --metadata "${WORK}/existing.json")
file(READ "${WORK}/existing.json" kept)
if(NOT kept STREQUAL "keep")
  message(FATAL_ERROR "Rejected run changed an existing file")
endif()

# 2. One run-wide OCR budget: 4 scanned pages, --ocr-budget 1 -> 1 attempt in
#    total across analysis and metadata (no TOC here: exit 3).
if(MODELS)
  expect_exit(3 "${CLI}" analyze "${SCAN}" --models "${MODELS}" --ocr-budget 1
              --report "${WORK}/scan-report.json" --metadata "${WORK}/scan-meta.json")
  file(READ "${WORK}/scan-report.json" report)
  file(READ "${WORK}/scan-meta.json" meta)
  string(JSON a GET "${report}" acquisition ocr_attempts_used)
  string(JSON m GET "${meta}" acquisition ocr_attempts_used)
  math(EXPR total "${a} + ${m}")
  if(NOT total EQUAL 1)
    message(FATAL_ERROR "--ocr-budget 1 allowed ${total} OCR attempts (analysis ${a}, metadata ${m})")
  endif()
endif()
message(STATUS "CLI book checks passed")
