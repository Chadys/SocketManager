cmake_minimum_required(VERSION 3.12)
project(SocketClient)

set(CMAKE_CXX_STANDARD 17)

add_executable(SocketClient main.cpp SocketClient.cpp SocketClient.h SocketHelperClasses.cpp SocketHelperClasses.h)

target_link_libraries(SocketClient ws2_32)