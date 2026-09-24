cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkParsingAcquired LANGUAGES CXX)
find_package(pdfbookmarkText CONFIG REQUIRED)
find_package(pdfbookmarkTocDetection CONFIG REQUIRED)
find_package(pdfbookmarkTocParsing CONFIG REQUIRED)
add_executable(parsing_acquired main.cpp)
target_compile_features(parsing_acquired PRIVATE cxx_std_17)
target_link_libraries(parsing_acquired PRIVATE
  pdfbookmark::Text pdfbookmark::TocDetection pdfbookmark::TocParsing)
add_custom_command(TARGET parsing_acquired POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:parsing_acquired>")
