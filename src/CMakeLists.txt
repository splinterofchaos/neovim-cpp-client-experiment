# add_executable(neovimgen neovimgen.cpp)
add_executable(vsh vim-shell.cpp)
add_executable(cvim cursed.cpp)

find_package (Threads)
find_package (Curses)

add_library(Socket Socket.cpp)
add_library(NeoServer NeoServer.cpp)

target_link_libraries(NeoServer ${CMAKE_THREAD_LIBS_INIT} ${MSGPACK_LIBRARIES})

target_link_libraries(vsh  Socket NeoServer)
target_link_libraries(cvim Socket NeoServer ${CURSES_CURSES_LIBRARY})
