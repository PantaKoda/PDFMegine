# Qt Quick client of the installed pdfbookmark SDK: open a PDF, review and edit
# the proposed bookmarks, write a new bookmarked copy. Guide: docs/API.md.
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug ^
#         -DCMAKE_PREFIX_PATH="C:/Qt/6.11.2/msvc2022_64;C:/path/to/pdfbookmark-sdk"
#   cmake --build build
#   build\pdfbookmark_qt_example.exe                       (the window)
#   build\pdfbookmark_qt_example.exe --selftest book.pdf   (no window: analyze + write, exit 0)
#
# Needs the Qt "MSVC 64-bit" kit (the C++ API is MSVC-only; see AGENTS.md).
cmake_minimum_required(VERSION 3.21)
project(pdfbookmark_qt_example LANGUAGES CXX)

find_package(Qt6 6.5 REQUIRED COMPONENTS Quick)
find_package(pdfbookmark 0.1 CONFIG REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(pdfbookmark_qt_example main.cpp)
qt_add_qml_module(pdfbookmark_qt_example
  URI BookmarkExample
  VERSION 1.0
  QML_FILES Main.qml
  SOURCES bookmarker.h bookmarker.cpp)

target_link_libraries(pdfbookmark_qt_example PRIVATE Qt6::Quick pdfbookmark::pdfbookmark)
if(MSVC)
  target_compile_options(pdfbookmark_qt_example PRIVATE /utf-8)
endif()

# pdfbookmark.dll, its dependency DLLs and the OCR models next to the executable.
# (For distribution, also run windeployqt on the executable.)
pdfbookmark_deploy_runtime(pdfbookmark_qt_example)
