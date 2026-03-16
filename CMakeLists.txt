cmake_minimum_required(VERSION 3.20)

# vcpkg toolchain — must be set before project()
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "" FORCE)
    elseif(EXISTS "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "$ENV{HOME}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "" FORCE)
    endif()
endif()

# auto-detect vcpkg triplet
if(NOT DEFINED VCPKG_TARGET_TRIPLET)
    if(APPLE)
        execute_process(COMMAND uname -m OUTPUT_VARIABLE ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(ARCH STREQUAL "arm64")
            set(VCPKG_TARGET_TRIPLET "arm64-osx" CACHE STRING "vcpkg triplet" FORCE)
        else()
            set(VCPKG_TARGET_TRIPLET "x64-osx" CACHE STRING "vcpkg triplet" FORCE)
        endif()
    elseif(UNIX)
        execute_process(COMMAND uname -m OUTPUT_VARIABLE ARCH OUTPUT_STRIP_TRAILING_WHITESPACE)
        if(ARCH STREQUAL "x86_64")
            set(VCPKG_TARGET_TRIPLET "x64-linux" CACHE STRING "vcpkg triplet" FORCE)
        else()
            set(VCPKG_TARGET_TRIPLET "arm64-linux" CACHE STRING "vcpkg triplet" FORCE)
        endif()
    elseif(WIN32)
        set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "vcpkg triplet" FORCE)
    endif()
endif()

project(DearAPI VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(wxWidgets REQUIRED COMPONENTS core base aui)
include(${wxWidgets_USE_FILE})

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_executable(dearapi
    src/main.cpp
    src/main_frame.cpp
    src/request_tab.cpp
    src/http_client.cpp
    src/db_store.cpp
)

target_include_directories(dearapi PRIVATE src include)

target_link_libraries(dearapi PRIVATE
    ${wxWidgets_LIBRARIES}
    Boost::system
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    unofficial::sqlite3::sqlite3
)

set_target_properties(dearapi PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
)
