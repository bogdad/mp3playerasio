cmake_minimum_required (VERSION 3.15)
project (asio-example CXX)
set(CMAKE_CXX_STANDARD 20)

find_package(asio 1.28.1 REQUIRED)
find_package(absl REQUIRED)

add_library(protocol src/protocol.cpp)
target_include_directories(protocol 
	PUBLIC include)
target_link_libraries(protocol 
	PRIVATE asio::asio absl::any_invocable absl::log)

add_library(mp3 src/mp3.cpp)
target_include_directories(mp3 PUBLIC include)
target_link_libraries(mp3 
	PRIVATE asio::asio absl::any_invocable absl::log)

add_executable(asio-server 
	src/asio-server.cpp src/server-protocol.cpp)
target_include_directories(asio-server PUBLIC include)
target_link_libraries(asio-server 
	PRIVATE asio::asio absl::strings absl::log mp3 protocol)

add_executable(asio-client 
	src/asio-client.cpp src/client-protocol.cpp)
target_include_directories(asio-client 
	PUBLIC include)
target_link_libraries(asio-client 
	PRIVATE protocol asio::asio absl::strings)
