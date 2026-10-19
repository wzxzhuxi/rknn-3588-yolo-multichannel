#ifndef CALC_PRE_INFER_DMA_HEAP_ALLOCATOR_HPP
#define CALC_PRE_INFER_DMA_HEAP_ALLOCATOR_HPP

#include <cerrno>
#include <cstring>
#include <iostream>
#include <mutex>
#include <unordered_map>

#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

#include <opencv2/opencv.hpp>

namespace calc_pre {

// dma_heap 自分配。system-dma32 优先（落在 <4G，RGA2 也能映射；RGA3 无此限），
// cma/system 回退。字段类型须与内核 struct dma_heap_allocation_data 严格一致
// （__u64/__u32/__u32/__u64），否则 ioctl 结构大小不符 → EINVAL。
struct DmaHeapAllocData {
    unsigned long long len;
    unsigned int fd;
    unsigned int fd_flags;
    unsigned long long heap_flags;
};
inline constexpr unsigned long kDmaHeapIoctlAllocCmd = 0xc0184800;

inline int AllocDmaHeapBuffer(size_t size) {
    const char* heap_paths[] = {"/dev/dma_heap/system-dma32", "/dev/dma_heap/cma", "/dev/dma_heap/system", nullptr};
    int last_errno = 0;
    const char* last_path = "";
    for (int i = 0; heap_paths[i]; ++i) {
        int heap = ::open(heap_paths[i], O_RDWR | O_CLOEXEC);
        if (heap < 0) {
            last_errno = errno;
            last_path = heap_paths[i];
            continue;
        }
        DmaHeapAllocData alloc{size, 0, 2, 0};  // fd_flags=2: O_RDWR
        if (::ioctl(heap, kDmaHeapIoctlAllocCmd, &alloc) == 0) {
            ::close(heap);
            return static_cast<int>(alloc.fd);
        }
        last_errno = errno;
        last_path = heap_paths[i];
        ::close(heap);
    }
    std::cerr << "[dma_heap] alloc " << size << "B failed: " << last_path << " errno=" << last_errno << " ("
              << std::strerror(last_errno) << ")\n";
    return -1;
}

// ── dma_heap 内存的 cv::Mat 分配器 ──────────────────────────────────────
// 数据流形状固定: RGA 直接写进 cv::Mat 自己的内存,
// 没有中间缓冲、没有 memcpy；内存来源是 dma_heap。
// malloc, 本工程换成 dma_heap(system-dma32)。
//
// 为什么必须换: 本板 16GB, malloc 拿到的页可能落在 4G 以上, 而 RGA2 无 4G+
// 寻址能力(RGA3 有)。多通道压测时低段页耗尽 → 新分配落到 >4G → 内核报
//   rga_mm: RGA_MMU unsupported memory larger than 4G!
//
// 6.12 系统注意: 新 RGA 驱动解析 VA 时手工遍历页表, 不认 dma_heap 大块内存的
// PMD 块映射 —— RGA 侧必须用 dma-buf fd 引用同一缓冲 (与检测器 io_mem 同法)。
// 因此 fd 保留到 deallocate, 供 FdFor() 查询。
class DmaHeapMatAllocator : public cv::MatAllocator {
  public:
    static DmaHeapMatAllocator* Instance() {
        static DmaHeapMatAllocator inst;
        return &inst;
    }

    int FdFor(const void* va) const {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = fd_by_va_.find(va);
        return it == fd_by_va_.end() ? -1 : it->second;
    }

    cv::UMatData* allocate(int dims, const int* sizes, int type, void* data0, size_t* step, int /*flags*/,
                           cv::UMatUsageFlags /*usageFlags*/) const override {
        if (dims != 2 || data0 != nullptr || !step) return nullptr;

        const size_t esz = CV_ELEM_SIZE(type);
        size_t total = esz;
        for (int i = dims - 1; i >= 0; --i) {
            step[i] = total;
            total *= static_cast<size_t>(sizes[i]);
        }

        const int fd = AllocDmaHeapBuffer(total);
        if (fd < 0) return nullptr;
        void* va = ::mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (va == MAP_FAILED) {
            std::cerr << "[dma_heap] mmap failed: " << std::strerror(errno) << "\n";
            ::close(fd);
            return nullptr;
        }
        {
            std::lock_guard<std::mutex> lk(mu_);
            fd_by_va_[va] = fd;  // fd 在 deallocate 时关闭
        }

        auto* u = new cv::UMatData(this);
        u->data = u->origdata = static_cast<uchar*>(va);
        u->size = total;
        return u;
    }

    bool allocate(cv::UMatData* /*data*/, int /*accessflags*/, cv::UMatUsageFlags /*usageFlags*/) const override {
        return false;  // 没有 UMat 的 device 侧副本
    }

    void deallocate(cv::UMatData* data) const override {
        if (!data) return;
        if (data->data) {
            {
                std::lock_guard<std::mutex> lk(mu_);
                auto it = fd_by_va_.find(data->data);
                if (it != fd_by_va_.end()) {
                    ::close(it->second);
                    fd_by_va_.erase(it);
                }
            }
            ::munmap(data->data, data->size);
        }
        data->data = nullptr;
        delete data;
    }

  private:
    mutable std::mutex mu_;
    mutable std::unordered_map<const void*, int> fd_by_va_;
};

}  // namespace calc_pre

#endif  // CALC_PRE_INFER_DMA_HEAP_ALLOCATOR_HPP
