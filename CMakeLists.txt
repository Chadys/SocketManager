cmake_minimum_required(VERSION 3.12)
project(SocketManager)

set(CMAKE_CXX_STANDARD 17)

add_executable(SocketManager main.cpp SocketManager.cpp SocketManager.h SocketHelperClasses.cpp SocketHelperClasses.h Misc.cpp Misc.h socket_headers.h)

target_link_libraries(SocketManager ws2_32 rpcrt4)

include(CheckSymbolExists)
CHECK_SYMBOL_EXISTS(idealsendbacklogquery "ws2tcpip.h" HAVE_DECL_IDEAL_SEND_BACKLOG_IOCTLS)
if(HAVE_DECL_IDEAL_SEND_BACKLOG_IOCTLS)
    target_compile_definitions(SocketManager PRIVATE -DHAVE_DECL_IDEAL_SEND_BACKLOG_IOCTLS)
endif()