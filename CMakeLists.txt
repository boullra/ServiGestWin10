cmake_minimum_required(VERSION 3.16)
project(ServiGestWin10 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(ServiGestWin10 WIN32
    src/main.cpp src/scm.cpp src/core.cpp src/knowledge.cpp src/i18n.cpp src/lang_win.cpp res/app.rc)
target_compile_definitions(ServiGestWin10 PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0A00 WINVER=0x0A00)
target_include_directories(ServiGestWin10 PRIVATE src)
target_link_libraries(ServiGestWin10 PRIVATE comctl32 comdlg32 shlwapi shell32 uxtheme advapi32 gdi32 user32)

if(MSVC)
    # El manifiesto va embebido desde app.rc; evitar que el enlazador genere otro.
    target_link_options(ServiGestWin10 PRIVATE /MANIFEST:NO)
    target_compile_options(ServiGestWin10 PRIVATE /utf-8 /W4)
    set_property(TARGET ServiGestWin10 PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(ServiGestWin10 PRIVATE -Wall -Wextra -municode)
    target_link_options(ServiGestWin10 PRIVATE -municode -static)
endif()

enable_testing()
add_executable(test_core tests/test_core.cpp src/core.cpp src/knowledge.cpp src/i18n.cpp)
target_include_directories(test_core PRIVATE src)
if(MSVC)
    target_compile_options(test_core PRIVATE /utf-8)
endif()
add_test(NAME test_core COMMAND test_core ${CMAKE_SOURCE_DIR}/lang)
# Recompilar el recurso cuando cambie un archivo de idioma
set_property(SOURCE res/app.rc APPEND PROPERTY OBJECT_DEPENDS
    ${CMAKE_SOURCE_DIR}/lang/en.lang ${CMAKE_SOURCE_DIR}/lang/es.lang
    ${CMAKE_SOURCE_DIR}/lang/fr.lang ${CMAKE_SOURCE_DIR}/lang/zh-CN.lang)
