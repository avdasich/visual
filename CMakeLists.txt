cmake_minimum_required(VERSION 3.16)

project(PhoneMonitor CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(THIRD_PARTY ${CMAKE_SOURCE_DIR}/third_party)

add_library(imgui STATIC
    ${THIRD_PARTY}/imgui/imgui.cpp
    ${THIRD_PARTY}/imgui/imgui_draw.cpp
    ${THIRD_PARTY}/imgui/imgui_tables.cpp
    ${THIRD_PARTY}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY}/imgui/backends/imgui_impl_opengl3.cpp
)

target_include_directories(imgui PUBLIC
    ${THIRD_PARTY}/imgui
    ${THIRD_PARTY}/imgui/backends
)

find_package(SDL2 REQUIRED)

target_include_directories(imgui PUBLIC
    ${SDL2_INCLUDE_DIRS}
)

add_library(implot STATIC
    ${THIRD_PARTY}/implot/implot.cpp
    ${THIRD_PARTY}/implot/implot_items.cpp
)

target_include_directories(implot PUBLIC
    ${THIRD_PARTY}/implot
    ${THIRD_PARTY}/imgui
)

find_package(PostgreSQL REQUIRED)

find_package(CURL REQUIRED)

add_executable(main

    src/main.cpp

    src/json_parser.cpp
    src/curl_func.cpp
    src/heatmap.cpp
    src/gui.cpp
    src/server.cpp
    src/database.cpp

    src/osm_map.cpp
)

target_include_directories(main PRIVATE

    src

    ${THIRD_PARTY}/imgui
    ${THIRD_PARTY}/imgui/backends
    ${THIRD_PARTY}/implot
    ${THIRD_PARTY}/stb

    ${PostgreSQL_INCLUDE_DIRS}
    ${CURL_INCLUDE_DIRS}
)

find_library(ZMQ_LIB
    NAMES zmq libzmq
    HINTS
        /opt/homebrew/lib
        /usr/local/lib
        /usr/lib
)

if(NOT ZMQ_LIB)
    message(FATAL_ERROR "libzmq not found. Run: brew install zeromq")
endif()

message(STATUS "Found libzmq: ${ZMQ_LIB}")

find_file(ZMQ_HPP zmq.hpp
    HINTS
        /opt/homebrew/include
        /usr/local/include
        /usr/include
)

if(NOT ZMQ_HPP)
    message(FATAL_ERROR "zmq.hpp not found. Run: brew install cppzmq")
endif()

get_filename_component(ZMQ_HPP_DIR ${ZMQ_HPP} DIRECTORY)

target_include_directories(main PRIVATE
    ${ZMQ_HPP_DIR}
)

find_path(ZMQ_INCLUDE_DIR zmq.h
    HINTS
        /opt/homebrew/include
        /usr/local/include
        /usr/include
)

target_include_directories(main PRIVATE
    ${ZMQ_INCLUDE_DIR}
)

find_package(OpenGL REQUIRED)

target_include_directories(main PRIVATE
    ${SDL2_INCLUDE_DIRS}
)

if(APPLE)

    target_compile_definitions(main PRIVATE
        GL_SILENCE_DEPRECATION
    )

else()

    find_package(GLEW REQUIRED)

    target_link_libraries(main PRIVATE
        GLEW::GLEW
    )

endif()

target_link_libraries(main PRIVATE

    imgui
    implot

    ${SDL2_LIBRARIES}

    OpenGL::GL

    ${ZMQ_LIB}

    ${PostgreSQL_LIBRARIES}

    CURL::libcurl
)



if(APPLE)

    target_link_libraries(main PRIVATE

        "-framework OpenGL"
        "-framework Cocoa"
        "-framework IOKit"
        "-framework CoreFoundation"
    )

endif()
