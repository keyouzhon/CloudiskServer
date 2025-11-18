set(SERVER_SOURCES
    src/auth_service.cpp
    src/cloud_server.cpp
    src/config_loader.cpp
    src/file_index.cpp
    src/jwt_service.cpp
    src/logger.cpp
    src/password_hasher.cpp
    src/storage_manager.cpp
    src/task_executor.cpp
)

add_library(cloud_drive_server_lib ${SERVER_SOURCES})

target_include_directories(cloud_drive_server_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/common/include
)

target_link_libraries(cloud_drive_server_lib
    PUBLIC
        sqlite3
        ssl
        crypto
        pthread
)

add_executable(cloud_drive_server src/main.cpp)
target_link_libraries(cloud_drive_server PRIVATE cloud_drive_server_lib)
