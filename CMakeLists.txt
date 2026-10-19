cmake_minimum_required(VERSION 3.11 FATAL_ERROR)
project(rknn-3588-yolo-multichannel VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/cross_deps.cmake)
include_directories(${CALC_DEPS_INCLUDE_DIRS})

# 静态链接 C++ 运行时: 板端 libstdc++ 6.0.28 太老; 工具链 6.0.33 的 .so 又带
# GLIBC_2.38 Verneed (加载器按文件级版本检查, 无法用 shim 满足). 静态链接后
# 其 __isoc23 UND 由 zc_glibc_compat 在 exe 内部满足. 与 rga 的 .so 各自
# 持有 libstdc++ 实例无碍 — 跨边界只过 C API.
add_link_options(-static-libstdc++ -static-libgcc)

# ---- glibc23 兼容层: 必须编进可执行文件本体 (静态库形式会被链接器丢弃, 见
#      src/compat/glibc23_compat.c 头注释). OBJECT 库 + $<TARGET_OBJECTS> 直链.
add_library(zc_glibc_compat OBJECT src/compat/glibc23_compat.c)
set_target_properties(zc_glibc_compat PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)

# ---- 工具/冒烟测试目标 ----
add_executable(hello_gst src/hello_gst.cpp $<TARGET_OBJECTS:zc_glibc_compat>)
target_link_libraries(hello_gst ${GST_LIBS})

# ---- 组件库 ----
add_library(zc_common_config STATIC
    components/common_config/src/app_config.cpp
    components/common_config/src/project_adapters.cpp
)
target_include_directories(zc_common_config PUBLIC
    components/common_config/include
    components/infer/include
    components/post_infer/include
    components/pre_infer/include
)

add_executable(config_dump src/config_dump.cpp $<TARGET_OBJECTS:zc_glibc_compat>)
target_link_libraries(config_dump zc_common_config)

add_library(zc_pre STATIC
    components/pre_infer/src/gst_rtsp_dmabuf_source.cpp
    components/pre_infer/src/types.cpp
    components/pre_infer/src/preprocessor.cpp
    components/pre_infer/src/pre_infer_pipeline.cpp
)
target_include_directories(zc_pre PUBLIC components/pre_infer/include)
target_link_libraries(zc_pre ${GST_LIBS} ${OpenCV_LIBS} ${RGA_LIB})

add_executable(rtsp_source_dump src/rtsp_source_dump.cpp $<TARGET_OBJECTS:zc_glibc_compat>)
target_link_libraries(rtsp_source_dump zc_pre ${GST_LIBS})

add_library(zc_infer STATIC
    components/infer/src/yolov5s_detector.cpp
    components/infer/src/yolov5s_sub/yolov5_nms.cpp
    components/infer/src/infer_engine.cpp
    components/infer/src/pre_infer_bridge.cpp
    components/infer/src/rknn_perf.cpp
)
target_include_directories(zc_infer PUBLIC
    components/infer/include
    components/infer/src
    components/pre_infer/include
)
# RGA 路径: VDEC dma fd --im2d--> RKNN io_mem (uint8 RGB888), 零拷贝。
target_compile_definitions(zc_infer PUBLIC CALC_INFER_WITH_RKNN=1)
target_link_libraries(zc_infer ${RKNN_API_LIB} ${RGA_LIB} ${OpenCV_LIBS})

add_executable(detect_dump src/detect_dump.cpp $<TARGET_OBJECTS:zc_glibc_compat>)
target_link_libraries(detect_dump zc_pre zc_infer ${GST_LIBS})

add_library(zc_post STATIC
    components/post_infer/src/person_tracker.cpp
    components/post_infer/src/roi_mapper.cpp
    components/post_infer/src/snapshot_manager.cpp
    components/post_infer/src/post_infer_engine.cpp
)
target_include_directories(zc_post PUBLIC components/post_infer/include)
target_link_libraries(zc_post zc_infer ${OpenCV_LIBS})

add_executable(rknn3588_yolo_app
    src/main.cpp
    src/npu_worker_pool.cpp
    $<TARGET_OBJECTS:zc_glibc_compat>
)
target_link_libraries(rknn3588_yolo_app
    zc_pre zc_infer zc_post zc_common_config
    ${CALC_LINK_LIBS}
)
