# Minimal client of the installed pdfbookmark SDK.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
#         -DCMAKE_PREFIX_PATH=C:/path/to/pdfbookmark-sdk
#   cmake --build build
#   build\pdfbookmark_example.exe "C:\Books\My Book.pdf"
cmake_minimum_required(VERSION 3.20)
project(pdfbookmark_example LANGUAGES CXX)

find_package(pdfbookmark 0.1 CONFIG REQUIRED)

add_executable(pdfbookmark_example main.cpp)
target_compile_features(pdfbookmark_example PRIVATE cxx_std_17)
target_link_libraries(pdfbookmark_example PRIVATE pdfbookmark::pdfbookmark)
if(MSVC)
  target_compile_options(pdfbookmark_example PRIVATE /utf-8)
endif()
# Copies pdfbookmark.dll, its dependencies and the OCR models next to the exe.
pdfbookmark_deploy_runtime(pdfbookmark_example)
