# wal/CMakeLists.txt — WAL infrastructure library

file(GLOB WAL_SRC "*.h" "*.cpp")

add_library(blinkhash_wal STATIC ${WAL_SRC})

# WAL headers live in this directory
target_include_directories(blinkhash_wal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

INSTALL(TARGETS blinkhash_wal
    ARCHIVE DESTINATION ${CMAKE_SOURCE_DIR})