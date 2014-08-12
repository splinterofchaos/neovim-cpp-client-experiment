# add_executable(neovimgen neovimgen.cpp)
add_executable(vsh vim-shell.cpp)
# set_target_properties(neovimgen PROPERTIES COMPILE_FLAGS -DNEOVIM_NO_AUTO=1)
# target_link_libraries(neovimgen ${MSGPACK_LIBRARIES})

# add_custom_target(
#     neovim_auto
#     ${NEOVIM_EXEC} --api-msgpack-metadata | ${CMAKE_BINARY_DIR}/src/neovimgen ${CMAKE_CURRENT_SOURCE_DIR}/auto/ 
#     DEPENDS neovimgen
# )
find_package (Threads)

set(NEOVIM_SHELL_SOURCES Socket.cpp)

add_library(Socket Socket.cpp)
add_library(NeoServer NeoServer.cpp)

target_link_libraries( vsh
                       ${MSGPACK_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT}
                       Socket NeoServer
                     )
