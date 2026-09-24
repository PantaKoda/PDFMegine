# End-to-end checks of `pdfbookmark analyze`.
# cmake -DCLI=<exe> -DFIX=<tests/engine/fixtures> -DWORK=<dir> -P cli_analyze_test.cmake
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

# Ready plan: report and plan files written; stdout stays empty.
expect_exit(0 "${CLI}" analyze "${FIX}/boundary.pdf"
  --report "${WORK}/boundary.json" --plan "${WORK}/boundary.plan.json")
file(READ "${WORK}/boundary.json" report)
file(READ "${WORK}/boundary.plan.json" plan)
string(JSON outcome GET "${report}" outcome)
string(JSON nodes LENGTH "${plan}" nodes)
string(JSON policy GET "${plan}" existing_outline_policy)
string(JSON last_title GET "${plan}" nodes 7 title)
string(JSON last_page GET "${plan}" nodes 7 destination pdf_page_index)
if(NOT outcome STREQUAL "plan_ready" OR NOT nodes EQUAL 8 OR
   NOT policy STREQUAL "replace_in_copy" OR NOT last_title STREQUAL "Appendix Notes" OR
   NOT last_page EQUAL 60 OR NOT LAST_OUT STREQUAL "" OR
   NOT LAST_ERR MATCHES "plan ready and written")
  message(FATAL_ERROR "Unexpected ready analysis: ${outcome} ${nodes} ${policy}")
endif()

# Existing outputs are refused without --force.
expect_exit(1 "${CLI}" analyze "${FIX}/boundary.pdf" --plan "${WORK}/boundary.plan.json")
expect_exit(0 "${CLI}" analyze "${FIX}/boundary.pdf" --report "${WORK}/boundary.json"
  --plan "${WORK}/boundary.plan.json" --force)

# Not ready: report written, plan file NOT written, exit 3, blockers shown.
expect_exit(3 "${CLI}" analyze "${FIX}/unresolved.pdf"
  --report "${WORK}/unresolved.json" --plan "${WORK}/unresolved.plan.json")
if(EXISTS "${WORK}/unresolved.plan.json" OR NOT LAST_ERR MATCHES "blocked: Entry 'Beta'")
  message(FATAL_ERROR "A plan was written for an unready analysis")
endif()
file(READ "${WORK}/unresolved.json" report)
string(JSON ready GET "${report}" plan ready)
if(ready)
  message(FATAL_ERROR "Unready analysis reported ready")
endif()

# Explicit partial policy: omission recorded in the plan.
expect_exit(0 "${CLI}" analyze "${FIX}/unresolved.pdf" --allow-partial
  --report "${WORK}/partial.json" --plan "${WORK}/partial.plan.json")
file(READ "${WORK}/partial.plan.json" plan)
string(JSON omitted LENGTH "${plan}" omitted_entries)
string(JSON promoted LENGTH "${plan}" promotions)
if(NOT omitted EQUAL 1 OR NOT promoted EQUAL 1 OR NOT LAST_ERR MATCHES "partial: see omissions")
  message(FATAL_ERROR "Partial plan must record omission and promotion")
endif()

# Tied candidates: exit 3 listing IDs; selecting one by ID succeeds.
expect_exit(3 "${CLI}" analyze "${FIX}/multi.pdf")
string(JSON second_id GET "${LAST_OUT}" detection candidates 1 id)
if(NOT LAST_ERR MATCHES "choose with --candidate")
  message(FATAL_ERROR "Candidate choice was not explained")
endif()
expect_exit(0 "${CLI}" analyze "${FIX}/multi.pdf" --candidate "${second_id}"
  --plan "${WORK}/multi.plan.json" --report "${WORK}/multi.json")

# No TOC in the searched pages.
expect_exit(3 "${CLI}" analyze "${FIX}/no_toc.pdf" --report "${WORK}/none.json")
file(READ "${WORK}/none.json" report)
string(JSON outcome GET "${report}" outcome)
if(NOT outcome STREQUAL "no_toc_found_in_search")
  message(FATAL_ERROR "Expected no_toc_found_in_search, got ${outcome}")
endif()

# Usage and safety errors.
expect_exit(2 "${CLI}" analyze "${FIX}/boundary.pdf" --plan -)
expect_exit(2 "${CLI}" analyze "${FIX}/boundary.pdf" --max-search-pages 0)
expect_exit(1 "${CLI}" analyze "${FIX}/boundary.pdf" --plan "${FIX}/boundary.pdf" --force)
expect_exit(1 "${CLI}" analyze "${WORK}/missing.pdf")
message(STATUS "CLI analyze end-to-end checks passed")
