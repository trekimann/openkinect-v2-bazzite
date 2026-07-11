cmake_minimum_required(VERSION 3.16)
project(openkinect-v2 VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_path(LIBFREENECT2_INCLUDE_DIR
    NAMES libfreenect2/libfreenect2.hpp
    HINTS /usr/include /usr/local/include
)

find_library(LIBFREENECT2_LIBRARY
    NAMES freenect2 libfreenect2
    HINTS /usr/lib /usr/local/lib /usr/lib64 /usr/local/lib64
)

if(NOT LIBFREENECT2_INCLUDE_DIR OR NOT LIBFREENECT2_LIBRARY)
    message(FATAL_ERROR "libfreenect2 headers and library are required to build openkinect-v2d")
endif()

add_executable(openkinect-v2d camera/kinect2v4l2.cpp)
target_include_directories(openkinect-v2d PRIVATE ${LIBFREENECT2_INCLUDE_DIR})
target_link_libraries(openkinect-v2d PRIVATE ${LIBFREENECT2_LIBRARY})

target_compile_options(openkinect-v2d PRIVATE -Wall -Wextra -Wpedantic)

install(TARGETS openkinect-v2d RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(PROGRAMS
    camera/openkinect-v2-runner.sh
    scripts/openkinect-v2-hostctl.sh
    audio/openkinect-audio-status.sh
    audio/kinect-audio-setup.sh
    audio/kinect-audio-config.sh
    audio/kinect-audio-monitor.sh
    audio/kinect-audio-pipewire.sh
    audio/kinect-record.sh
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/openkinect-v2
)
install(FILES services/openkinect-v2.service DESTINATION lib/systemd/system)
install(FILES services/openkinect-v2.conf DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/openkinect-v2)
