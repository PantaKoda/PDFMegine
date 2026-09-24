cmake_minimum_required(VERSION 3.20)

project(ocr_c_consumer LANGUAGES C CXX)

find_package(ocr CONFIG REQUIRED)

add_executable(ocr_c_consumer main.c)
target_link_libraries(ocr_c_consumer PRIVATE ocr::ocr)
set_target_properties(ocr_c_consumer PROPERTIES LINKER_LANGUAGE CXX)

add_custom_command(TARGET ocr_c_consumer POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:ocr::ocr>"
          "$<TARGET_FILE_DIR:ocr_c_consumer>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:ONNXRuntime::ONNXRuntime>"
          "$<TARGET_FILE_DIR:ocr_c_consumer>"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "$<TARGET_FILE:opencv_world>"
          "$<TARGET_FILE_DIR:ocr_c_consumer>"
)
