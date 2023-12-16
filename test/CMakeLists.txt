find_package(Catch2 3 REQUIRED)

add_executable(protocol_test protocol_test.cpp)
target_link_libraries(protocol_test PRIVATE protocol Catch2::Catch2WithMain)
target_include_directories(protocol_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/include)

add_test(NAME protocol_test
         COMMAND protocol_test -r junit)
