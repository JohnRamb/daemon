cmake_minimum_required(VERSION 3.10)
project(network_daemon)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Включить более строгие предупреждения
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBNL REQUIRED libnl-3.0 libnl-route-3.0)
pkg_check_modules(LIBEVENT REQUIRED libevent)

set(SOURCES
    main.cpp
    event_loop.cpp
    netlink_manager.cpp
    unix_socket_server.cpp
    network_manager.cpp
    network_daemon.cpp
    command_processor.cpp
    s_expression_parser.cpp
)

# Проверяем существование файлов перед добавлением
foreach(SOURCE_FILE ${SOURCES})
    if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE_FILE})
        message(WARNING "Source file ${SOURCE_FILE} does not exist!")
    endif()
endforeach()

add_executable(network_daemon ${SOURCES})

target_include_directories(network_daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBNL_INCLUDE_DIRS}
    ${LIBEVENT_INCLUDE_DIRS}
)

target_link_libraries(network_daemon PRIVATE
    ${LIBNL_LIBRARIES}
    ${LIBEVENT_LIBRARIES}
)

# Установка целевых каталогов для библиотек
link_directories(${LIBNL_LIBRARY_DIRS} ${LIBEVENT_LIBRARY_DIRS})

# Добавляем каталоги для поиска заголовков
include_directories(${LIBNL_INCLUDE_DIRS} ${LIBEVENT_INCLUDE_DIRS})

# Вывод информации о найденных библиотеках
message(STATUS "libnl include directories: ${LIBNL_INCLUDE_DIRS}")
message(STATUS "libnl libraries: ${LIBNL_LIBRARIES}")
message(STATUS "libevent include directories: ${LIBEVENT_INCLUDE_DIRS}")
message(STATUS "libevent libraries: ${LIBEVENT_LIBRARIES}")

#add_subdirectory(tests)