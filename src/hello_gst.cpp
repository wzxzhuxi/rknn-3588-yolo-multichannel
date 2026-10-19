// 工具链 + 板端 GStreamer 冒烟测试: 打印版本, 检查 mppvideodec 工厂与 dmabuf allocator 可用性
#include <cstdio>
#include <gst/gst.h>
#include <gst/allocators/gstdmabuf.h>

int main() {
    gst_init(nullptr, nullptr);
    guint major = 0, minor = 0, micro = 0, nano = 0;
    gst_version(&major, &minor, &micro, &nano);
    std::printf("[hello_gst] GStreamer %u.%u.%u\n", major, minor, micro);

    GstElement* dec = gst_element_factory_make("mppvideodec", nullptr);
    std::printf("[hello_gst] mppvideodec factory: %s\n", dec ? "OK" : "MISSING");
    if (dec) gst_object_unref(dec);

    GstAllocator* alloc = gst_dmabuf_allocator_new();
    std::printf("[hello_gst] dmabuf allocator: %s\n", alloc ? "OK" : "MISSING");
    if (alloc) gst_object_unref(alloc);

    return (dec && alloc) ? 0 : 1;
}
