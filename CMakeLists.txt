cmake_minimum_required(VERSION 2.8.11)
project(cvim)

if ( NOT EXISTS ${NEOVIM_EXEC} )
    set(NEOVIM_EXEC nvim)
endif ( NOT EXISTS ${NEOVIM_EXEC} )

add_definitions(-I/usr/local/include -Wall -Wextra -pedantic -Wno-unused-parameter -lpthread -std=c++1y)

set(CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Qt
# set(CMAKE_AUTOMOC ON)
# find_package(Qt5Widgets REQUIRED)
# find_package(Qt5Network REQUIRED)
# find_package(Qt5Test REQUIRED)
#
# msgpack
find_package(Msgpack REQUIRED)
include_directories(${MSGPACK_INCLUDE_DIRS})

if(CMAKE_BUILD_TYPE STREQUAL "Release")
endif()

add_subdirectory(src)
# add_subdirectory(examples)
# add_subdirectory(test)
