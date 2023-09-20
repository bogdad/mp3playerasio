cmake_minimum_required (VERSION 3.15)
project (asio-example CXX)
set(CMAKE_CXX_STANDARD 20)

if (CLANG_TIDY)
	set(CMAKE_CXX_CLANG_TIDY "/opt/homebrew/opt/llvm/bin/clang-tidy;-checks=*")
endif()
if(ASAN)
  message("Using ASAN")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address -fsanitize-address-use-after-scope -fno-omit-frame-pointer")
endif()
if(UBSAN)
  message("enabled UBSAN")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=undefined")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=undefined")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=undefined")
endif()

find_package(asio REQUIRED)
find_package(absl REQUIRED)
find_package(minimp3 REQUIRED)

add_library(protocol src/protocol.cpp)
target_include_directories(protocol 
	PUBLIC include)
target_link_libraries(protocol 
	PRIVATE asio::asio absl::base absl::any_invocable absl::log)

add_library(mp3 src/mp3.cpp)
target_include_directories(mp3 PUBLIC include)
target_link_libraries(mp3 
	PRIVATE asio::asio absl::any_invocable absl::log minimp3::minimp3)

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
