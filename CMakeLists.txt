cmake_minimum_required(VERSION 3.12)
project(network_daemon)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Включить более строгие предупреждения
add_compile_options(-Wall -Wextra -Wpedantic)

# Опция для включения/отключения демонизации
option(ENABLE_DAEMON "Enable daemonization of the network daemon" OFF)

# Отладочный вывод
message(STATUS "ENABLE_DAEMON option value: ${ENABLE_DAEMON}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBNL REQUIRED libnl-3.0 libnl-route-3.0)

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

# Добавляем определения макросов для конкретного target
if(ENABLE_DAEMON)
    message(STATUS "Daemonization: ENABLED")
    target_compile_definitions(network_daemon PRIVATE ENABLE_DAEMON)
else()
    message(STATUS "Daemonization: DISABLED")
endif()

# Можно добавить общие определения для всех файлов
target_compile_definitions(network_daemon PRIVATE
    VERSION="1.0.0"
    BUILD_TIMESTAMP="${CMAKE_BUILD_TYPE}"
)

target_include_directories(network_daemon PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${LIBNL_INCLUDE_DIRS}
)

target_link_libraries(network_daemon PRIVATE
    ${LIBNL_LIBRARIES}
)

# Вывод информации о найденных библиотеках
message(STATUS "libnl include directories: ${LIBNL_INCLUDE_DIRS}")
message(STATUS "libnl libraries: ${LIBNL_LIBRARIES}")