cmake_minimum_required(VERSION 3.16)
project(inventory CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_library(tinyjson STATIC third_party/tinyjson/tinyjson.cpp)
target_include_directories(tinyjson PUBLIC third_party/tinyjson)

add_library(inventory STATIC
  src/lib/item.cpp
  src/lib/stock.cpp)
target_include_directories(inventory PUBLIC include)
target_compile_definitions(inventory PRIVATE INVENTORY_VERSION=2)
target_link_libraries(inventory PUBLIC tinyjson)

add_executable(inventory-cli src/app/main.cpp)
target_link_libraries(inventory-cli PRIVATE inventory)
