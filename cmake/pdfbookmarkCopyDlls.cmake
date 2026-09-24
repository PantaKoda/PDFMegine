# Copy every DLL next to an imported backend library (e.g. qpdf and its
# vcpkg runtime dependencies) into a destination directory.
# Usage: cmake -DSRC=<dir> -DDST=<dir> -P pdfbookmarkCopyDlls.cmake
if(NOT SRC OR NOT DST)
  message(FATAL_ERROR "SRC and DST are required")
endif()
file(GLOB _dlls "${SRC}/*.dll")
file(MAKE_DIRECTORY "${DST}")
foreach(_dll IN LISTS _dlls)
  file(COPY "${_dll}" DESTINATION "${DST}")
endforeach()
