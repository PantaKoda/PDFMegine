cmake_minimum_required(VERSION 3.20)

project(ocr_cpp_consumer LANGUAGES CXX)

find_package(ocr CONFIG REQUIRED)

add_executable(ocr_cpp_consumer main.cpp)
target_compile_features(ocr_cpp_consumer PRIVATE cxx_std_17)
target_link_libraries(ocr_cpp_consumer PRIVATE ocr::ocr)

add_custom_command(TARGET ocr_cpp_consumer POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:ocr::ocr>"
          "$<TARGET_FILE_DIR:ocr_cpp_consumer>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:ONNXRuntime::ONNXRuntime>"
          "$<TARGET_FILE_DIR:ocr_cpp_consumer>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:opencv_world>"
          "$<TARGET_FILE_DIR:ocr_cpp_consumer>"
)
