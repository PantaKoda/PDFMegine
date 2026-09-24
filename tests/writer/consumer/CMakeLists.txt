cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkWriterConsumer LANGUAGES CXX)
find_package(pdfbookmarkWriter CONFIG REQUIRED)
add_executable(writer_consumer main.cpp)
target_compile_features(writer_consumer PRIVATE cxx_std_17)
target_link_libraries(writer_consumer PRIVATE pdfbookmark::Writer)
# Runtime DLLs installed with the package (Writer if shared, qpdf and deps).
if(WIN32)
  file(GLOB _package_dlls "${pdfbookmarkWriter_DIR}/../../../bin/*.dll")
  foreach(_dll IN LISTS _package_dlls)
    add_custom_command(TARGET writer_consumer POST_BUILD
      COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${_dll}"
              "$<TARGET_FILE_DIR:writer_consumer>")
  endforeach()
endif()
