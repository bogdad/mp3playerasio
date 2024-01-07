cmake_minimum_required (VERSION 3.19)
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

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")


find_package(asio REQUIRED)
find_package(absl REQUIRED)
find_package(minimp3 REQUIRED)
find_package(SDL2 REQUIRED)

add_library(protocol src/protocol.cpp src/protocol-system.cpp)
target_include_directories(protocol
	PUBLIC include)
target_link_libraries(protocol
	PRIVATE absl::any_invocable
	PUBLIC absl::base absl::log absl::core_headers asio::asio
)

add_library(mp3 src/mp3.cpp src/mp3-system.cpp)
target_include_directories(mp3 PUBLIC include)
target_link_libraries(mp3 
	PRIVATE asio::asio absl::any_invocable absl::log)

add_library(audio-player src/audio-player.cpp)
target_include_directories(audio-player PUBLIC include)
target_link_libraries(audio-player
	PRIVATE asio::asio absl::log minimp3::minimp3 SDL2::SDL2)

add_library(asio-client 
	src/asio-client.cpp src/client-protocol.cpp)
target_include_directories(asio-client
	PUBLIC include)
target_link_libraries(asio-client 
	PRIVATE protocol audio-player asio::asio absl::strings)

add_executable(asio-server 
	src/asio-server.cpp src/server-protocol.cpp)
target_include_directories(asio-server PUBLIC include)
target_link_libraries(asio-server
	PRIVATE asio::asio absl::strings absl::log mp3 protocol)

add_executable(driver src/driver.cpp)
target_include_directories(driver PUBLIC include)
target_link_libraries(driver 
	PRIVATE asio::asio absl::any_invocable absl::log protocol audio-player asio-client)

enable_testing()
add_subdirectory(test)
