# End-to-end checks of `pdfbookmark text` exit codes and outputs.
# cmake -DCLI=<exe> -DPDF=<6-page fixture> -DWORK=<dir> -P cli_text_test.cmake
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

# Complete run to a file; CLI page 3 is physical index 2.
expect_exit(0 "${CLI}" text "${PDF}" --pages 3,1 --mode embedded
  --json "${WORK}/text.json")
file(READ "${WORK}/text.json" json)
string(JSON first GET "${json}" pages 0 page_index)
string(JSON second GET "${json}" pages 1 page_index)
string(JSON base GET "${json}" page_index_base)
string(JSON status GET "${json}" status)
string(JSON text GET "${json}" pages 0 content text)
if(NOT first EQUAL 2 OR NOT second EQUAL 0 OR NOT base EQUAL 0 OR
   NOT status STREQUAL "complete" OR NOT text MATCHES "Alpha")
  message(FATAL_ERROR "Unexpected JSON: ${first} ${second} ${base} ${status}")
endif()
if(NOT LAST_OUT STREQUAL "" OR NOT LAST_ERR MATCHES "2 pages \\(ok=2")
  message(FATAL_ERROR "stdout must be empty and stderr must summarize")
endif()

# Existing output is refused without --force and left unchanged.
file(SHA256 "${WORK}/text.json" before)
expect_exit(1 "${CLI}" text "${PDF}" --pages 1 --mode embedded
  --json "${WORK}/text.json")
file(SHA256 "${WORK}/text.json" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "existing JSON was modified")
endif()
expect_exit(0 "${CLI}" text "${PDF}" --pages 1 --mode embedded
  --json "${WORK}/text.json" --force)

# JSON on stdout is clean; progress stays on stderr.
expect_exit(0 "${CLI}" text "${PDF}" --pages 2-3 --mode embedded)
string(JSON count LENGTH "${LAST_OUT}" pages)
if(NOT count EQUAL 2 OR NOT LAST_ERR MATCHES "text: 2/2 pages")
  message(FATAL_ERROR "stdout JSON or stderr progress missing")
endif()

# The input PDF can never be the JSON target.
expect_exit(1 "${CLI}" text "${PDF}" --json "${PDF}" --force --mode embedded)

# Usage and range errors.
expect_exit(2 "${CLI}")
expect_exit(2 "${CLI}" text "${PDF}" --pages 0)
expect_exit(2 "${CLI}" text "${PDF}" --pages 3-1)
expect_exit(2 "${CLI}" text "${PDF}" --mode fast)
expect_exit(2 "${CLI}" frobnicate)
expect_exit(2 "${CLI}" apply "${PDF}")
expect_exit(1 "${CLI}" text "${PDF}" --pages 7 --mode embedded)
expect_exit(1 "${CLI}" text "${WORK}/missing.pdf")
# OCR requested with models explicitly disabled: configuration failure.
expect_exit(1 "${CLI}" text "${PDF}" --mode ocr --no-ocr-models)
# Help: overview and per-command help go to stdout with exit 0.
expect_exit(0 "${CLI}" --help)
if(NOT LAST_OUT MATCHES "Commands:" OR NOT LAST_OUT MATCHES "analyze" OR
   NOT LAST_OUT MATCHES "help <command>")
  message(FATAL_ERROR "--help overview incomplete")
endif()
expect_exit(0 "${CLI}" --version)
if(NOT LAST_OUT MATCHES "^pdfbookmark [0-9]+\\.[0-9]+\\.[0-9]+")
  message(FATAL_ERROR "--version output unexpected: ${LAST_OUT}")
endif()
foreach(cmd add analyze apply text)
  expect_exit(0 "${CLI}" ${cmd} --help)
  set(per_command "${LAST_OUT}")
  expect_exit(0 "${CLI}" help ${cmd})
  if(NOT per_command STREQUAL LAST_OUT OR NOT LAST_OUT MATCHES "Examples?:")
    message(FATAL_ERROR "'${cmd} --help' and 'help ${cmd}' differ or lack an example")
  endif()
endforeach()
expect_exit(0 "${CLI}" analyze --help)
foreach(option --plan --report --allow-partial --flat-outline --candidate
               --max-search-pages --max-evidence-pages --mode --models --dpi --ocr-budget)
  if(NOT LAST_OUT MATCHES "${option}")
    message(FATAL_ERROR "analyze --help does not describe ${option}")
  endif()
endforeach()
expect_exit(2 "${CLI}" help frobnicate)
# A usage error inside a command points to that command's help.
expect_exit(2 "${CLI}" analyze "${PDF}" --bogus)
if(NOT LAST_ERR MATCHES "Unknown option --bogus" OR
   NOT LAST_ERR MATCHES "pdfbookmark analyze --help")
  message(FATAL_ERROR "command usage error lacks a pointer to its help")
endif()
message(STATUS "CLI text end-to-end checks passed")
