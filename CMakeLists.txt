
cmake_minimum_required(VERSION 3.15)
project(scatter LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SCATTER_USE_STANDALONE_ASIO "Use standalone Asio instead of Boost.Asio" ON)

if(SCATTER_USE_STANDALONE_ASIO)
    if(NOT DEFINED ASIO_INCLUDE_DIR)
        message(STATUS "ASIO_INCLUDE_DIR not set. Assuming system include paths contain <asio.hpp>.")
        set(ASIO_INCLUDE_DIR "")
    endif()
    add_library(asio_headers INTERFACE)
    if(ASIO_INCLUDE_DIR)
        target_include_directories(asio_headers INTERFACE ${ASIO_INCLUDE_DIR})
    endif()
    target_compile_definitions(asio_headers INTERFACE ASIO_STANDALONE)
else()
    find_package(Boost 1.70 REQUIRED COMPONENTS system)
endif()

set(SCATTER_COMMON_SOURCES
    src/common/logging.cpp
    src/common/util.cpp
    src/common/crypto.cpp
    src/common/fec.cpp
    src/common/protocol.cpp
)

add_library(scatter_common ${SCATTER_COMMON_SOURCES})

target_include_directories(scatter_common PUBLIC ${PROJECT_SOURCE_DIR}/include)

if(SCATTER_USE_STANDALONE_ASIO)
    target_link_libraries(scatter_common PUBLIC asio_headers)
else()
    target_link_libraries(scatter_common PUBLIC Boost::system)
endif()

# libsodium (vcpkg): find_package(unofficial-sodium CONFIG)
find_package(unofficial-sodium CONFIG QUIET)
if(unofficial-sodium_FOUND)
  target_link_libraries(scatter_common PUBLIC unofficial-sodium::sodium)
  target_compile_definitions(scatter_common PUBLIC SCATTER_HAVE_SODIUM)
endif()

add_executable(scatter_client
    src/client/main_client.cpp
    src/client/socks5_server.cpp
    src/client/transport_pool.cpp
    src/client/session.cpp
)

target_link_libraries(scatter_client PRIVATE scatter_common)

add_executable(scatter_server
    src/server/main_server.cpp
    src/server/server_pool.cpp
    src/server/remote_connector.cpp
)

target_link_libraries(scatter_server PRIVATE scatter_common)

if(MSVC)
    target_compile_options(scatter_common PRIVATE /W4)
    target_compile_options(scatter_client PRIVATE /W4)
    target_compile_options(scatter_server PRIVATE /W4)
else()
    target_compile_options(scatter_common PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(scatter_client PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(scatter_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
