set(CLIENT_SOURCES
    src/client_app.cpp
)

add_library(cloud_drive_client_lib ${CLIENT_SOURCES})

target_include_directories(cloud_drive_client_lib
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/common/include
)

target_link_libraries(cloud_drive_client_lib
    PUBLIC
        pthread
        crypto
)

add_executable(cloud_drive_client src/main.cpp)
target_link_libraries(cloud_drive_client PRIVATE cloud_drive_client_lib)
