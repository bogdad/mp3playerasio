cmake_minimum_required (VERSION 3.15)
project (asio-example CXX)

find_package(ASIO 1.28.1 REQUIRED)

add_executable(asio-server src/asio-example.cpp)
target_link_libraries(asio-server PRIVATE asio::asio)

add_executable(asio-client src/asio-client.cpp)
target_link_libraries(asio-client PRIVATE asio::asio)
