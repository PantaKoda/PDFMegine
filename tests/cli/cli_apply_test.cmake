# End-to-end `pdfbookmark analyze` -> `apply`.
# cmake -DCLI=<exe> -DFIX=<tests/engine/fixtures> -DWORK=<dir> -P cli_apply_test.cmake
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(expect_exit code)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc
    OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc STREQUAL "${code}")
    message(FATAL_ERROR "Expected exit ${code}, got ${rc}: ${ARGN}\n${err}")
  endif()
  set(LAST_OUT "${out}" PARENT_SCOPE)
  set(LAST_ERR "${err}" PARENT_SCOPE)
endfunction()

set(IN "${FIX}/boundary_outlined.pdf")
file(SHA256 "${IN}" input_before)

expect_exit(0 "${CLI}" analyze "${IN}" --report "${WORK}/report.json"
  --plan "${WORK}/plan.json")
expect_exit(0 "${CLI}" apply "${IN}" --plan "${WORK}/plan.json"
  --output "${WORK}/out.pdf")
if(NOT EXISTS "${WORK}/out.pdf" OR NOT LAST_OUT STREQUAL "" OR
   NOT LAST_ERR MATCHES "committed 8 bookmarks on 70 pages" OR
   NOT LAST_ERR MATCHES "existing outline replaced in the copy")
  message(FATAL_ERROR "apply did not report a verified commit:\n${LAST_ERR}")
endif()
file(SHA256 "${WORK}/out.pdf" out_first)

# Existing output: refused, unchanged; --force replaces it.
expect_exit(1 "${CLI}" apply "${IN}" --plan "${WORK}/plan.json" --output "${WORK}/out.pdf")
file(SHA256 "${WORK}/out.pdf" out_after_refusal)
if(NOT out_first STREQUAL out_after_refusal)
  message(FATAL_ERROR "refused apply modified the existing output")
endif()
expect_exit(0 "${CLI}" apply "${IN}" --plan "${WORK}/plan.json"
  --output "${WORK}/out.pdf" --force)
file(SHA256 "${WORK}/out.pdf" out_second)
if(NOT out_first STREQUAL out_second)
  message(FATAL_ERROR "identical apply runs produced different bytes")
endif()

# Stale plan (another input): exit 1, nothing written.
expect_exit(1 "${CLI}" apply "${FIX}/multi.pdf" --plan "${WORK}/plan.json"
  --output "${WORK}/stale.pdf")
if(EXISTS "${WORK}/stale.pdf" OR NOT LAST_ERR MATCHES "re-run analyze")
  message(FATAL_ERROR "stale plan was not rejected clearly")
endif()

# An analysis report is not a plan; the input can never be the output.
expect_exit(1 "${CLI}" apply "${IN}" --plan "${WORK}/report.json" --output "${WORK}/bad.pdf")
expect_exit(1 "${CLI}" apply "${IN}" --plan "${WORK}/plan.json" --output "${IN}" --force)
if(EXISTS "${WORK}/bad.pdf")
  message(FATAL_ERROR "malformed plan produced output")
endif()

# Usage errors.
expect_exit(2 "${CLI}" apply "${IN}" --plan "${WORK}/plan.json")
expect_exit(2 "${CLI}" apply "${IN}" --output "${WORK}/x.pdf")
expect_exit(2 "${CLI}" apply "${IN}" --plan)
expect_exit(2 "${CLI}" apply "${IN}" --plan "${WORK}/plan.json" --output "${WORK}/x.pdf" --merge)

# One step: add = analyze + apply, default output next to the input.
file(COPY_FILE "${IN}" "${WORK}/Book (copy).pdf")
file(SHA256 "${WORK}/Book (copy).pdf" book_before)
expect_exit(0 "${CLI}" add "${WORK}/Book (copy).pdf")
if(NOT EXISTS "${WORK}/Book (copy) (bookmarked).pdf" OR NOT LAST_OUT STREQUAL "" OR
   NOT LAST_ERR MATCHES "add: wrote .* with 8 bookmarks")
  message(FATAL_ERROR "add did not write the default output:\n${LAST_ERR}")
endif()
expect_exit(1 "${CLI}" add "${WORK}/Book (copy).pdf")
expect_exit(0 "${CLI}" add "${WORK}/Book (copy).pdf" --force
  --report "${WORK}/add.report.json" --plan "${WORK}/add.plan.json")
if(NOT EXISTS "${WORK}/add.report.json" OR NOT EXISTS "${WORK}/add.plan.json")
  message(FATAL_ERROR "add --report/--plan files missing")
endif()
file(SHA256 "${WORK}/Book (copy).pdf" book_after)
if(NOT book_before STREQUAL book_after)
  message(FATAL_ERROR "add modified its input")
endif()
file(COPY_FILE "${FIX}/unresolved.pdf" "${WORK}/Notes.pdf")
expect_exit(3 "${CLI}" add "${WORK}/Notes.pdf")
if(EXISTS "${WORK}/Notes (bookmarked).pdf" OR NOT LAST_ERR MATCHES "no PDF written" OR
   NOT LAST_ERR MATCHES "tip: --allow-partial")
  message(FATAL_ERROR "add wrote output for an unready plan or gave no tip")
endif()
expect_exit(0 "${CLI}" add "${WORK}/Notes.pdf" --allow-partial)
expect_exit(2 "${CLI}" add "${WORK}/Notes.pdf" --report -)
# apply without a plan points to the one-step command.
expect_exit(2 "${CLI}" apply "${WORK}/Notes.pdf")
if(NOT LAST_ERR MATCHES "pdfbookmark add")
  message(FATAL_ERROR "apply without --plan does not suggest 'add'")
endif()

file(SHA256 "${IN}" input_after)
if(NOT input_before STREQUAL input_after)
  message(FATAL_ERROR "input PDF bytes changed")
endif()
message(STATUS "CLI apply end-to-end checks passed")
