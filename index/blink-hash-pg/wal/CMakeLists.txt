# wal/CMakeLists.txt — WAL infrastructure library

file(GLOB WAL_SRC "*.h" "*.cpp")

add_library(blinkhash_wal STATIC ${WAL_SRC})

# WAL headers live in this directory
target_include_directories(blinkhash_wal PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# Linux: link io_uring if available
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_library(URING_LIB uring)
    if(URING_LIB)
        target_link_libraries(blinkhash_wal ${URING_LIB})
        target_compile_definitions(blinkhash_wal PUBLIC HAS_IO_URING)
    endif()
endif()

INSTALL(TARGETS blinkhash_wal
    ARCHIVE DESTINATION ${CMAKE_SOURCE_DIR})