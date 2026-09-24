# End-to-end checks of `pdfbookmark metadata`.
# cmake -DCLI=<exe> -DPDF=<front_matter.pdf> -DWORK=<dir> -P cli_metadata_test.cmake
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")

function(expect_exit code)
  execute_process(COMMAND ${ARGN} RESULT_VARIABLE rc
    OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT rc STREQUAL "${code}")
    message(FATAL_ERROR "Expected exit ${code}, got ${rc}: ${ARGN}\n${out}\n${err}")
  endif()
  set(LAST_OUT "${out}" PARENT_SCOPE)
endfunction()

file(SHA256 "${PDF}" before)
expect_exit(0 "${CLI}" metadata "${PDF}" --mode embedded)
if(NOT LAST_OUT MATCHES "Title: +Parallel Worlds" OR
   NOT LAST_OUT MATCHES "Subtitle: +A Practical Guide" OR
   NOT LAST_OUT MATCHES "Jane Q. Doe \\(author\\), John Smith \\(author\\)" OR
   NOT LAST_OUT MATCHES "Edition: +Second Edition" OR
   NOT LAST_OUT MATCHES "Publication year: 2012" OR
   NOT LAST_OUT MATCHES "Copyright year: +ambiguous")
  message(FATAL_ERROR "Unexpected metadata summary:\n${LAST_OUT}")
endif()
expect_exit(0 "${CLI}" metadata "${PDF}" --mode embedded --json "${WORK}/meta.json")
file(READ "${WORK}/meta.json" json)
string(JSON title GET "${json}" fields title value title)
string(JSON year GET "${json}" fields publication_year value year)
string(JSON first_page GET "${json}" pages 0 role)
if(NOT title STREQUAL "Parallel Worlds" OR NOT year EQUAL 2012 OR
   NOT first_page STREQUAL "title")  # Text-only first page: title, not cover.
  message(FATAL_ERROR "Unexpected metadata JSON: ${title} ${year} ${first_page}")
endif()
expect_exit(1 "${CLI}" metadata "${PDF}" --mode embedded --json "${WORK}/meta.json")
expect_exit(3 "${CLI}" metadata "${PDF}" --mode embedded --max-pages 2)
expect_exit(0 "${CLI}" metadata --help)
expect_exit(2 "${CLI}" metadata "${PDF}" --max-pages 0)
file(SHA256 "${PDF}" after)
if(NOT before STREQUAL after)
  message(FATAL_ERROR "metadata modified its input")
endif()
message(STATUS "CLI metadata checks passed")
