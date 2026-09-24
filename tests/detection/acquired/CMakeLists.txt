cmake_minimum_required(VERSION 3.20)
project(pdfbookmarkDetectionAcquired LANGUAGES CXX)
find_package(pdfbookmarkText CONFIG REQUIRED)
find_package(pdfbookmarkTocDetection CONFIG REQUIRED)
add_executable(detection_acquired main.cpp)
target_compile_features(detection_acquired PRIVATE cxx_std_17)
target_link_libraries(detection_acquired PRIVATE
  pdfbookmark::Text pdfbookmark::TocDetection)
add_custom_command(TARGET detection_acquired POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:pdfium>" "$<TARGET_FILE_DIR:detection_acquired>")
