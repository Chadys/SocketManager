cmake_minimum_required(VERSION 3.12)
project(SocketManager)

set(CMAKE_CXX_STANDARD 17)

add_executable(SocketManager main.cpp SocketManager.cpp SocketManager.h SocketHelperClasses.cpp SocketHelperClasses.h Misc.cpp Misc.h)

target_link_libraries(SocketManager ws2_32 rpcrt4)