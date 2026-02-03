# SPDX-FileCopyrightText: 2025 LichtFeld Studio Authors
#
# SPDX-License-Identifier: GPL-3.0-or-later

add_library(lfs_mcp STATIC
    mcp_protocol.cpp
    mcp_tools.cpp
    mcp_server.cpp
    mcp_training_context.cpp
    llm_client.cpp
    selection_client.cpp
)

target_include_directories(lfs_mcp
    PUBLIC
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
)

find_package(OpenSSL REQUIRED)

target_link_libraries(lfs_mcp
    PUBLIC
    nlohmann_json::nlohmann_json
    PRIVATE
    lfs_core
    lfs_event_bridge
    lfs_training
    lfs_visualizer
    lfs_rendering
    $<$<BOOL:${BUILD_PYTHON_BINDINGS}>:lfs_python_utils>
    OpenSSL::SSL
    OpenSSL::Crypto
)

target_compile_features(lfs_mcp PUBLIC cxx_std_23)

if(BUILD_PYTHON_BINDINGS)
    target_compile_definitions(lfs_mcp PRIVATE LFS_BUILD_PYTHON_BINDINGS)
endif()

# Standalone MCP server executable
add_executable(lichtfeld-mcp
    main.cpp
)

target_link_libraries(lichtfeld-mcp
    PRIVATE
    lfs_mcp
    lfs_core
    lfs_event_bridge
    lfs_training
    OpenImageIO::OpenImageIO
)

target_compile_features(lichtfeld-mcp PRIVATE cxx_std_23)
