cmake_minimum_required(VERSION 3.16)
project(jack-panel LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

add_executable(jack-panel main.cpp)
target_link_libraries(jack-panel PRIVATE Qt6::Widgets)

install(TARGETS jack-panel RUNTIME DESTINATION bin)
