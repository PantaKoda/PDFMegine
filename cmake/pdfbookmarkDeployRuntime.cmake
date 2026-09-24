# Copies what a pdfbookmark client needs at run time into DST: the
# pdfbookmark DLL (LIB), its dependency DLLs (from SRC) and, when MODELS is
# given, the OCR models into DST/models. Unchanged files are skipped, so it
# is cheap to run on every build and picks up an upgraded SDK.
# Usage: cmake -DLIB=<dll> -DSRC=<sdk>/bin[/debug] -DDST=<dir> [-DMODELS=<dir>]
#              -P pdfbookmarkDeployRuntime.cmake
if(NOT LIB OR NOT SRC OR NOT DST)
  message(FATAL_ERROR "LIB, SRC and DST are required")
endif()
file(MAKE_DIRECTORY "${DST}")
file(GLOB _dlls "${SRC}/*.dll")
list(FILTER _dlls EXCLUDE REGEX "/pdfbookmarkd?\.dll$")
# file(COPY) keeps timestamps and skips files that are already up to date.
file(COPY "${LIB}" ${_dlls} DESTINATION "${DST}")
if(MODELS AND EXISTS "${MODELS}")
  file(COPY "${MODELS}/" DESTINATION "${DST}/models")
endif()
