cmake_minimum_required(VERSION 3.10)
project(MiniSearchEngine VERSION 1.0.0 LANGUAGES CXX)

# 設定 C++ 標準
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 編譯選項
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

# 尋找必要的套件
find_package(PkgConfig REQUIRED)
pkg_check_modules(CURL REQUIRED libcurl)
pkg_check_modules(GUMBO REQUIRED gumbo)

# 尋找 SQLite3
find_package(SQLite3 REQUIRED)

# 尋找 Redis
find_package(PkgConfig REQUIRED)
pkg_check_modules(HIREDIS REQUIRED hiredis)

# 尋找 httplib
find_path(HTTPLIB_INCLUDE_DIR httplib.h)
if(NOT HTTPLIB_INCLUDE_DIR)
    message(FATAL_ERROR "httplib not found. Please install cpp-httplib")
endif()

# 包含目錄
include_directories(${CURL_INCLUDE_DIRS})
include_directories(${GUMBO_INCLUDE_DIRS})
include_directories(${HIREDIS_INCLUDE_DIRS})
include_directories(${HTTPLIB_INCLUDE_DIR})
include_directories(include)

# 源文件
set(SOURCES
    src/main.cpp
    src/crawler.cpp
    src/parser.cpp
    src/task_queue.cpp
    src/indexer.cpp
    src/storage.cpp
    src/server.cpp
    src/logger.cpp
)

# 頭文件
set(HEADERS
    include/crawler.h
    include/parser.h
    include/task_queue.h
    include/indexer.h
    include/storage.h
    include/server.h
    include/logger.h
)

# 建立可執行文件
add_executable(mini_search_engine ${SOURCES} ${HEADERS})

# 連結庫
target_link_libraries(mini_search_engine 
    ${CURL_LIBRARIES}
    ${GUMBO_LIBRARIES}
    SQLite::SQLite3
    ${HIREDIS_LIBRARIES}
    pthread
)

# 設置庫搜尋路徑
target_link_directories(mini_search_engine PRIVATE
    /opt/homebrew/lib
)

# 編譯定義
target_compile_definitions(mini_search_engine PRIVATE
    ${CURL_CFLAGS_OTHER}
    ${GUMBO_CFLAGS_OTHER}
    ${HIREDIS_CFLAGS_OTHER}
)

# 安裝目標
install(TARGETS mini_search_engine DESTINATION bin)
