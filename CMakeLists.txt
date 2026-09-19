cmake_minimum_required(VERSION 3.16)
project(SpecTer VERSION 1.0.0 LANGUAGES CXX)

# C++ Standard Configuration
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for VS Code / IDE intellisense
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Source files list
set(SOURCES
    src/main.cpp
    src/ShellCore.cpp
    src/JobManager.cpp
    src/Profiler.cpp
)

# Headers list (for IDE grouping)
set(HEADERS
    src/ShellCore.h
    src/JobManager.h
    src/Profiler.h
    src/UIEngine.h
)

# Executable target definition
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Compiler flags for MSVC and GCC/Clang
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /utf-8 /EHsc)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Windows System Libraries required for Win32 API calls
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        kernel32
        user32
        psapi
        advapi32
    )
endif()

message(STATUS "Configured ${PROJECT_NAME} for C++17 on Windows platform.")
