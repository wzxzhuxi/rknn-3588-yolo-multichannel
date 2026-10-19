# 依赖路径聚合 — vendored (librknn_api/3rdparty) + buildroot sysroot (GStreamer/glib)
# 变量: CALC_DEPS_INCLUDE_DIRS / CALC_LINK_LIBS

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# ---- vendored ----
set(RKNN_API_PATH ${REPO_ROOT}/librknn_api)
set(RKNN_API_LIB  ${RKNN_API_PATH}/aarch64/librknnrt.so)
set(RGA_DIR       ${REPO_ROOT}/3rdparty/rga/RK3588)
set(RGA_LIB       ${RGA_DIR}/lib/Linux/aarch64/librga.so)
set(OpenCV_DIR    ${REPO_ROOT}/3rdparty/opencv/opencv-linux-aarch64/share/OpenCV)
find_package(OpenCV REQUIRED)

# ---- buildroot sysroot (GStreamer + glib) ----
set(SR ${CMAKE_SYSROOT})
set(GST_INCLUDE_DIRS
    ${SR}/usr/include/gstreamer-1.0
    ${SR}/usr/include/glib-2.0
    ${SR}/usr/lib/glib-2.0/include
)
set(GST_LIBS
    ${SR}/usr/lib/libgstapp-1.0.so
    ${SR}/usr/lib/libgstvideo-1.0.so
    ${SR}/usr/lib/libgstallocators-1.0.so
    ${SR}/usr/lib/libgstreamer-1.0.so
    ${SR}/usr/lib/libgobject-2.0.so
    ${SR}/usr/lib/libglib-2.0.so
)

set(CALC_DEPS_INCLUDE_DIRS
    ${OpenCV_INCLUDE_DIRS}
    ${RKNN_API_PATH}/include
    ${RGA_DIR}/include
    ${REPO_ROOT}/3rdparty
    ${GST_INCLUDE_DIRS}
)

set(CALC_LINK_LIBS
    ${OpenCV_LIBS}
    ${RKNN_API_LIB}
    ${RGA_LIB}
    ${GST_LIBS}
    pthread
)
