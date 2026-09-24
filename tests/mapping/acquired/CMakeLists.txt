cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkMappingAcquired LANGUAGES CXX)
find_package(pdfbookmarkText CONFIG REQUIRED)
find_package(pdfbookmarkTocDetection CONFIG REQUIRED)
find_package(pdfbookmarkTocParsing CONFIG REQUIRED)
find_package(pdfbookmarkMapping CONFIG REQUIRED)
add_executable(mapping_acquired main.cpp)
target_compile_features(mapping_acquired PRIVATE cxx_std_17)
target_link_libraries(mapping_acquired PRIVATE
  pdfbookmark::Text pdfbookmark::TocDetection
  pdfbookmark::TocParsing pdfbookmark::Mapping
  pdfium)  # Test-only outline probe for the paired fixture.
add_custom_command(TARGET mapping_acquired POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:mapping_acquired>")
