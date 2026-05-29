cmake_minimum_required(VERSION 3.13)
project(pokemonCppServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Embed simulator as an in-process library.
set(BUILD_TESTING OFF CACHE BOOL "Disable tests when embedding simulator" FORCE)

# Find required packages
find_package(Protobuf CONFIG REQUIRED)
find_package(gRPC CONFIG REQUIRED)
find_package(CURL REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Bring in the simulator core without its standalone executable.
add_subdirectory(src/simulator EXCLUDE_FROM_ALL)

target_compile_definitions(battle_core PUBLIC BATTLE_SIMULATOR_DISABLE_CACHE_IO)

# Find hiredis without depending on pkg-config (works on Homebrew and standard Linux paths)
find_path(HIREDIS_INCLUDE_DIR
    NAMES hiredis/hiredis.h
    HINTS /opt/homebrew/opt/hiredis/include /usr/local/opt/hiredis/include
)

find_library(HIREDIS_LIBRARY
    NAMES hiredis
    HINTS /opt/homebrew/opt/hiredis/lib /usr/local/opt/hiredis/lib
)

if(NOT HIREDIS_INCLUDE_DIR OR NOT HIREDIS_LIBRARY)
    message(FATAL_ERROR "Could not find hiredis. Install it with Homebrew or make sure the development package is available.")
endif()


# Proto file  
set(proto_files
    "${CMAKE_CURRENT_SOURCE_DIR}/proto/room_service.proto"
)

# Generate proto files
set(PROTO_GEN_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(PROTO_SRCS
    "${PROTO_GEN_DIR}/room_service.pb.cc"
    "${PROTO_GEN_DIR}/room_service.grpc.pb.cc"
)
set(PROTO_HDRS
    "${PROTO_GEN_DIR}/room_service.pb.h"
    "${PROTO_GEN_DIR}/room_service.grpc.pb.h"
)

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS}
    COMMAND protobuf::protoc
    ARGS
        --proto_path=${CMAKE_CURRENT_SOURCE_DIR}/proto
        --cpp_out=${PROTO_GEN_DIR}
        --grpc_out=${PROTO_GEN_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        ${proto_files}
    DEPENDS ${proto_files}
    COMMENT "Generating gRPC and Protobuf sources"
    VERBATIM
)

# Source files
set(SERVER_SOURCES
    src/server/main.cpp
    src/server/game_server.cpp
    src/server/battle_engine.cpp
    src/server/room_manager.cpp
    src/server/signal_handler.cpp
    src/server/redis_client.cpp
    ${PROTO_SRCS}
)

# Create executable
add_executable(pokemon_server ${SERVER_SOURCES})

target_include_directories(pokemon_server PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/simulator/include"
    ${HIREDIS_INCLUDE_DIR}
    /usr/include/nlohmann
)

# Link libraries
target_link_libraries(pokemon_server
    gRPC::grpc++_reflection
    protobuf::libprotobuf
    battle_core
    nlohmann_json::nlohmann_json
    CURL::libcurl
    ${HIREDIS_LIBRARY}
    pthread
)

# Set output directory
set_target_properties(pokemon_server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
