# 旧版简单测试（保留但不强制编译）
# add_executable(ThreadpoolTest1  ThreadpoolTest1.cpp ${SRC_THRPOOL})
# target_link_libraries(ThreadpoolTest1  pthread)

# ========== ChatBench 压测工具 ==========
set(BENCH_SRC
    bench_main.cpp
)

add_executable(ChatBench ${BENCH_SRC})
target_link_libraries(ChatBench pthread)

# 头文件路径（复用项目的 include）
target_include_directories(ChatBench PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${PROJECT_SOURCE_DIR}/include/server
    ${PROJECT_SOURCE_DIR}/include/server/model
    ${PROJECT_SOURCE_DIR}/thirdparty
)

# ========== ConnBench 连接数压测工具 ==========
add_executable(ConnBench conn_bench.cpp)
target_link_libraries(ConnBench pthread)
