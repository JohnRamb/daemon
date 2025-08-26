cmake_minimum_required(VERSION 3.10)
project(network_daemon)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBNL-3 REQUIRED libnl-3.0)
pkg_check_modules(LIBNL-ROUTE-3 REQUIRED libnl-route-3.0)

add_executable(network_daemon
    main.cpp
    event_loop.cpp
    netlink_manager.cpp
    unix_socket_server.cpp
    network_manager.cpp
    network_daemon.cpp
)

target_include_directories(network_daemon PRIVATE ${LIBNL-3_INCLUDE_DIRS} ${LIBNL-ROUTE-3_INCLUDE_DIRS})
target_link_libraries(network_daemon ${LIBNL-3_LIBRARIES} ${LIBNL-ROUTE-3_LIBRARIES})