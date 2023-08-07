cmake_minimum_required (VERSION 3.12)
project (asio-example CXX)

find_package(asio REQUIRED)

add_executable(asio-example src/asio-example.cpp)
target_include_directories(asio-example PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(asio-example asio::asio)
