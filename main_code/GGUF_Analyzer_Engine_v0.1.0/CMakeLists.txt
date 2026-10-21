cmake_minimum_required(VERSION 3.20)
project(GGUFAnalyzerEngine VERSION 0.1.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(gguf_analyzer
    src/main.cpp
    src/gguf_engine.cpp
)
target_include_directories(gguf_analyzer PRIVATE src)
if(MSVC)
  target_compile_options(gguf_analyzer PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(gguf_analyzer PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
endif()
