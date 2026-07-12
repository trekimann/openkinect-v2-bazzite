cmake_minimum_required(VERSION 3.16)
project(openkinect-v2 VERSION 0.1.0 LANGUAGES CXX)

include(GNUInstallDirs)

set(OPENKINECT_CONFIG_DIR "/etc/openkinect-v2")
set(OPENKINECT_CONFIG_FILE "${OPENKINECT_CONFIG_DIR}/openkinect-v2.conf")
set(OPENKINECT_RUNNER_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/openkinect-v2/openkinect-v2-runner.sh")
set(OPENKINECT_HOSTCTL_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/openkinect-v2/openkinect-v2-hostctl.sh")
set(OPENKINECT_AUDIO_STATUS_PATH "${CMAKE_INSTALL_FULL_LIBEXECDIR}/openkinect-v2/openkinect-audio-status.sh")
set(OPENKINECT_BIN_PATH "${CMAKE_INSTALL_FULL_BINDIR}/openkinect-v2d")
set(OPENKINECT_AUDIO_BIN_PATH "${CMAKE_INSTALL_FULL_BINDIR}/openkinect-audiod")
set(OPENKINECT_RUNTIME_RPATH
    "${CMAKE_INSTALL_PREFIX}/lib"
    "${CMAKE_INSTALL_FULL_LIBDIR}"
)
list(REMOVE_DUPLICATES OPENKINECT_RUNTIME_RPATH)
string(JOIN ":" OPENKINECT_RUNTIME_LIBDIRS ${OPENKINECT_RUNTIME_RPATH})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(OPENKINECT_BUILD_CAMERA "Build the Kinect camera daemon" ON)
option(OPENKINECT_BUILD_AUDIO "Build the Kinect audio daemon" ON)

if(OPENKINECT_BUILD_CAMERA)
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
    set_target_properties(openkinect-v2d PROPERTIES INSTALL_RPATH "${OPENKINECT_RUNTIME_RPATH}")
    target_compile_options(openkinect-v2d PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(OPENKINECT_BUILD_AUDIO)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3 libspa-0.2)

    add_executable(openkinect-audiod audio/openkinect-audiod.cpp)
    target_link_libraries(openkinect-audiod PRIVATE PkgConfig::PIPEWIRE)
    set_target_properties(openkinect-audiod PROPERTIES INSTALL_RPATH "${OPENKINECT_RUNTIME_RPATH}")
    target_compile_options(openkinect-audiod PRIVATE -Wall -Wextra -Wpedantic)
endif()

configure_file(camera/openkinect-v2-runner.sh.in ${CMAKE_CURRENT_BINARY_DIR}/openkinect-v2-runner.sh @ONLY)
configure_file(scripts/openkinect-v2-hostctl.sh.in ${CMAKE_CURRENT_BINARY_DIR}/openkinect-v2-hostctl.sh @ONLY)
configure_file(services/openkinect-v2.service.in ${CMAKE_CURRENT_BINARY_DIR}/openkinect-v2.service @ONLY)
configure_file(services/kinect-webcam.service.in ${CMAKE_CURRENT_BINARY_DIR}/kinect-webcam.service @ONLY)
configure_file(services/openkinect-audio.service.in ${CMAKE_CURRENT_BINARY_DIR}/openkinect-audio.service @ONLY)

if(OPENKINECT_BUILD_CAMERA)
    install(TARGETS openkinect-v2d RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
if(OPENKINECT_BUILD_AUDIO)
    install(TARGETS openkinect-audiod RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
endif()
install(PROGRAMS
    ${CMAKE_CURRENT_BINARY_DIR}/openkinect-v2-runner.sh
    ${CMAKE_CURRENT_BINARY_DIR}/openkinect-v2-hostctl.sh
    audio/openkinect-audio-status.sh
    audio/kinect-audio-setup.sh
    audio/kinect-audio-config.sh
    audio/kinect-audio-monitor.sh
    audio/kinect-audio-pipewire.sh
    audio/kinect-record.sh
    DESTINATION ${CMAKE_INSTALL_LIBEXECDIR}/openkinect-v2
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/openkinect-v2.service
    ${CMAKE_CURRENT_BINARY_DIR}/kinect-webcam.service
    DESTINATION lib/systemd/system
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/openkinect-audio.service
    DESTINATION lib/systemd/user
)
install(FILES services/openkinect-v2.conf DESTINATION /etc/openkinect-v2)
