// rknn3588_yolo_app — RK3588 多路 RTSP 零拷贝行人检测 (多通道 + 跟踪 + 可选抓拍)
#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <dirent.h>
#include <exception>
#include <execinfo.h>
#include <fcntl.h>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <typeinfo>
#include <utility>
#include <vector>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <gst/gst.h>

#include "app/infer_engine.hpp"
#include "app/pre_infer_bridge.hpp"
#include "app/rknn_perf.hpp"  // GetRknnPerfSnapshots (心跳打印)
#include "calc_common_config/app_config.hpp"
#include "calc_common_config/project_adapters.hpp"
#include "calc_post_infer/post_infer_engine.hpp"
#include "calc_pre_infer/pre_infer_pipeline.hpp"
#include "npu_worker_pool.h"

namespace {

std::atomic_bool g_stop{false};

void HandleSignal(int) {
    if (g_stop.load()) std::_Exit(130);
    g_stop.store(true);
}

// std::terminate 处理器 —— 必须在 abort() *之前* 运行: 那时异常对象还活着,
// 能拿到类型名和 what()。只靠下面的 SIGABRT 处理器拿不到(abort 时异常已析构)。
//
// 为什么要这个: 板上出现过一次 SIGABRT → 进程带着 MPP 解码器一起拆 → 内核在
// shmem dma-buf 的 inode 回收路径上报 "Bad page cache ... still mapped when deleted"
// (283 页) → 96 秒后内核 panic 重启。abort.log 只能给出二进制偏移, 且旧二进制
// 一被覆盖就对不上号 —— 异常类型名才是稳定的线索。
void HandleTerminate() {
    const int fd =
        ::open("/userdata/rknn-3588-yolo-multichannel/terminate.log", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) {
        std::string msg = "std::terminate\n";
        if (std::current_exception()) {
            try {
                std::rethrow_exception(std::current_exception());
            } catch (const std::exception& e) {
                msg += std::string("  type: ") + typeid(e).name() + "\n";
                msg += std::string("  what: ") + e.what() + "\n";
            } catch (...) {
                msg += "  non-std exception (no typeid)\n";
            }
        } else {
            msg += "  no active exception (terminate called directly)\n";
        }
        ::write(fd, msg.data(), msg.size());
        void* frames[32];
        const int n = ::backtrace(frames, 32);
        ::backtrace_symbols_fd(frames, n, fd);
        ::close(fd);
    }
    ::_exit(134);
}

// SIGABRT (未捕获异常 → std::terminate) 同样落盘, 便于定位模型加载期崩溃
void HandleAbort(int) {
    const int fd =
        ::open("/userdata/rknn-3588-yolo-multichannel/abort.log", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) {
        void* frames[32];
        const int n = ::backtrace(frames, 32);
        char buf[96];
        int len = ::snprintf(buf, sizeof(buf), "SIGABRT backtrace (%d frames):\n", n);
        if (len > 0) ::write(fd, buf, len);
        ::backtrace_symbols_fd(frames, n, fd);
        ::close(fd);
    }
    ::_exit(134);
}

void HandleSegv(int) {
    // 写到持久分区 (/userdata), 不是 tmpfs — 板子重启后证据仍在.
    // backtrace_symbols_fd 直接给符号名, 宿主机可再 addr2line 精确定位.
    const int fd =
        ::open("/userdata/rknn-3588-yolo-multichannel/segv.log", O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd >= 0) {
        void* frames[32];
        const int n = ::backtrace(frames, 32);
        char buf[96];
        int len = ::snprintf(buf, sizeof(buf), "SIGSEGV backtrace (%d frames):\n", n);
        if (len > 0) ::write(fd, buf, len);
        ::backtrace_symbols_fd(frames, n, fd);
        ::close(fd);
    }
    ::_exit(139);
}

// ── 系统采样 (心跳用) ───────────────────────────────────────────────
double ProcRssMB() {
    std::ifstream f("/proc/self/statm");
    if (!f) return 0.0;
    long size_pages = 0, resident_pages = 0;
    f >> size_pages >> resident_pages;
    return resident_pages * (::sysconf(_SC_PAGESIZE) / 1024) / 1024.0;
}

struct SysMemSnap {
    double pct = 0.0;
    long total_mb = 0, avail_mb = 0;
};
SysMemSnap ReadSysMem() {
    std::ifstream f("/proc/meminfo");
    SysMemSnap s;
    if (!f) return s;
    std::string key, unit;
    long val_kb = 0, total_kb = 0, avail_kb = 0;
    while (f >> key >> val_kb >> unit) {
        if (key == "MemTotal:")
            total_kb = val_kb;
        else if (key == "MemAvailable:") {
            avail_kb = val_kb;
            break;
        }
    }
    s.total_mb = total_kb / 1024;
    s.avail_mb = avail_kb / 1024;
    if (total_kb > 0) s.pct = 1.0 - static_cast<double>(avail_kb) / total_kb;
    return s;
}

double CpuTempCelsius() {
    std::ifstream f("/sys/class/thermal/thermal_zone0/temp");
    if (!f) return 0.0;
    long milli = 0;
    f >> milli;
    return milli / 1000.0;
}

struct CpuTimes {
    long proc_jiffies = 0, sys_total = 0, sys_idle = 0;
    bool ok = false;
};
CpuTimes ReadCpuTimes() {
    CpuTimes c;
    bool proc_ok = false;
    {
        std::ifstream f("/proc/self/stat");
        std::string line;
        if (f && std::getline(f, line)) {
            size_t rp = line.rfind(')');
            if (rp != std::string::npos) {
                std::istringstream ss(line.substr(rp + 1));
                std::string tok;
                bool skipped = true;
                for (int i = 0; i < 11; ++i)
                    if (!(ss >> tok)) {
                        skipped = false;
                        break;
                    }
                long utime = 0, stime = 0;
                if (skipped && (ss >> utime >> stime)) {
                    c.proc_jiffies = utime + stime;
                    proc_ok = true;
                }
            }
        }
    }
    {
        std::ifstream f("/proc/stat");
        std::string line;
        if (f && std::getline(f, line) && line.rfind("cpu ", 0) == 0) {
            std::istringstream ss(line.substr(3));
            long v = 0;
            int idx = 0;
            while (ss >> v) {
                c.sys_total += v;
                if (idx == 3 || idx == 4) c.sys_idle += v;
                ++idx;
            }
        }
    }
    c.ok = proc_ok && c.sys_total > 0;
    return c;
}

struct CpuSampler {
    CpuTimes prev_;
    bool primed_ = false;
    long ncpu_ = std::max(1L, ::sysconf(_SC_NPROCESSORS_ONLN));
    std::pair<double, double> sample() {  // {proc_pct, sys_pct}
        CpuTimes cur = ReadCpuTimes();
        double proc = 0.0, sys = 0.0;
        if (primed_ && cur.ok && prev_.ok) {
            const long d_total = cur.sys_total - prev_.sys_total;
            if (d_total > 0) {
                sys = 100.0 * (d_total - (cur.sys_idle - prev_.sys_idle)) / d_total;
                proc = 100.0 * (cur.proc_jiffies - prev_.proc_jiffies) / d_total * ncpu_;
            }
        }
        if (cur.ok) {
            prev_ = cur;
            primed_ = true;
        }
        return {proc, sys};
    }
};

// ── 日志 (LogTee) ──────────────────────────────────────────────
class LogTee {
  public:
    LogTee(std::string dir, size_t max_size, bool to_terminal)
        : log_dir_(std::move(dir)), max_size_(max_size), to_terminal_(to_terminal) {}

    bool start() {
        ::mkdir(log_dir_.c_str(), 0755);
        cleanup_old_logs();
        if (!open_new_file()) return false;
        saved_stdout_ = ::dup(STDOUT_FILENO);
        saved_stderr_ = ::dup(STDERR_FILENO);
        if (saved_stdout_ < 0 || saved_stderr_ < 0) return false;
        if (::pipe(pipe_fd_) < 0) return false;
        ::dup2(pipe_fd_[1], STDOUT_FILENO);
        ::dup2(pipe_fd_[1], STDERR_FILENO);
        ::close(pipe_fd_[1]);
        pipe_fd_[1] = -1;
        std::setvbuf(stdout, nullptr, _IOLBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        // 日志泵线程: 挂了不该拖垮整个进程, 兜住异常只降级(日志不再刷屏)。
        reader_ = std::thread([this] {
            try {
                reader_loop();
            } catch (const std::exception& e) {
                std::cerr << "[fatal] log reader threw " << typeid(e).name() << ": " << e.what()
                          << " -- logging degraded\n";
            } catch (...) {
                std::cerr << "[fatal] log reader threw a non-std exception"
                          << " -- logging degraded\n";
            }
        });
        return true;
    }

    void stop() {
        if (saved_stdout_ >= 0) {
            ::fflush(stdout);
            ::fflush(stderr);
            ::dup2(saved_stdout_, STDOUT_FILENO);
            ::dup2(saved_stdout_, STDERR_FILENO);
            ::close(saved_stdout_);
            saved_stdout_ = -1;
            ::close(saved_stderr_);
            saved_stderr_ = -1;
        }
        if (reader_.joinable()) reader_.join();
        if (file_fd_ >= 0) {
            ::close(file_fd_);
            file_fd_ = -1;
        }
        if (pipe_fd_[0] >= 0) {
            ::close(pipe_fd_[0]);
            pipe_fd_[0] = -1;
        }
    }

    const std::string& current_path() const { return current_path_; }

  private:
    void write_blob(const char* data, size_t len) {
        if (to_terminal_ && saved_stdout_ >= 0) {
            size_t off = 0;
            while (off < len) {
                ssize_t w = ::write(saved_stdout_, data + off, len - off);
                if (w <= 0) break;
                off += static_cast<size_t>(w);
            }
        }
        if (file_fd_ >= 0) {
            size_t off = 0;
            while (off < len) {
                ssize_t w = ::write(file_fd_, data + off, len - off);
                if (w <= 0) break;
                off += static_cast<size_t>(w);
            }
            file_size_ += len;
        }
    }

    void reader_loop() {
        char buf[4096];
        std::string line_buf;
        line_buf.reserve(256);
        for (;;) {
            ssize_t n = ::read(pipe_fd_[0], buf, sizeof(buf));
            if (n <= 0) break;
            for (ssize_t i = 0; i < n; ++i) {
                line_buf.push_back(buf[i]);
                if (buf[i] == '\n') {
                    write_blob(line_buf.data(), line_buf.size());
                    line_buf.clear();
                }
            }
        }
        if (!line_buf.empty()) {
            line_buf.push_back('\n');
            write_blob(line_buf.data(), line_buf.size());
        }
    }

    bool open_new_file() {
        std::time_t t = std::time(nullptr);
        struct tm tm_buf;
        ::localtime_r(&t, &tm_buf);
        char fname[64];
        std::strftime(fname, sizeof(fname), "app_%Y%m%d_%H%M%S.log", &tm_buf);
        current_path_ = log_dir_ + "/" + fname;
        file_fd_ = ::open(current_path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
        if (file_fd_ < 0) return false;
        file_size_ = 0;
        return true;
    }

    void cleanup_old_logs() {
        DIR* d = ::opendir(log_dir_.c_str());
        if (!d) return;
        std::time_t now = std::time(nullptr);
        struct dirent* ent;
        while ((ent = ::readdir(d)) != nullptr) {
            std::string name = ent->d_name;
            if (name.rfind("app_", 0) != 0) continue;
            if (name.size() < 8 || name.substr(name.size() - 4) != ".log") continue;
            std::string full = log_dir_ + "/" + name;
            struct stat st;
            if (::stat(full.c_str(), &st) != 0) continue;
            if (now - st.st_mtime > 24 * 3600) ::unlink(full.c_str());
        }
        ::closedir(d);
        if (file_size_ >= max_size_) {
            if (file_fd_ >= 0) {
                ::close(file_fd_);
                file_fd_ = -1;
            }
            open_new_file();
        }
    }

    std::string log_dir_;
    size_t max_size_;
    bool to_terminal_;
    int saved_stdout_ = -1;
    int saved_stderr_ = -1;
    int pipe_fd_[2] = {-1, -1};
    int file_fd_ = -1;
    size_t file_size_ = 0;
    std::thread reader_;
    std::string current_path_;
};

LogTee* g_log_tee = nullptr;

bool CheckZeroCopyConfig(const calc_config::AppConfig& cfg, std::string* err) {
    if (cfg.video.input_mode != "rtsp") {
        if (err) *err = "3588 pipeline requires input_mode=rtsp";
        return false;
    }
    if (!cfg.video.prefer_rga || !cfg.video.require_rga) {
        if (err) *err = "zero-copy video infer requires prefer_rga=true and require_rga=true";
        return false;
    }
    if (!cfg.video.keep_source_bgr && cfg.post.enable_snapshot) {
        if (err) *err = "production snapshots require keep_source_bgr=true";
        return false;
    }
    return true;
}

// ── 通道运行时 ──────────────────────────────────────────────────────
struct ChannelRuntime {
    int id = -1;
    int cfg_index = -1;  // 对应 cfg.channels 的下标 (含禁用通道)
    std::string name;

    calc_pre::PreInferPipeline pre;
    std::unique_ptr<calc_post::PostInferEngine> post;

    std::thread capture_th;
    std::thread engine_th;
    std::atomic_bool open_failed{false};

    // 心跳统计
    std::atomic<uint64_t> frames_read{0};
    std::atomic<uint64_t> frames_submitted{0};
    std::atomic<uint64_t> frames_dropped{0};
    std::atomic<uint64_t> results_done{0};
    std::atomic<uint64_t> detections_last{0};

    explicit ChannelRuntime(calc_pre::PreInferConfig pre_cfg) : pre(std::move(pre_cfg)) {}
};

// EngineLoop 分段耗时: post->process 每次调用的墙钟与线程 CPU
// (clock_gettime CLOCK_THREAD_CPUTIME_ID), 心跳以 [prof] 行输出 ——
// 用于区分"CPU 忙"与"等 NPU"。
std::atomic<uint64_t> g_prof_post_calls{0}, g_prof_post_ns{0}, g_prof_post_cpu_ns{0};

inline uint64_t ThreadCpuNs() {
    struct timespec ts;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts);
    return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + static_cast<uint64_t>(ts.tv_nsec);
}

void CaptureLoop(ChannelRuntime& ch, NpuWorkerPool& pool, const calc_pre::PreInferConfig& pre_cfg) {
    std::string err;
    if (!ch.pre.open(&err)) {
        std::cerr << "[ch:" << ch.name << "] pre-infer open failed: " << err << "\n";
        ch.open_failed.store(true);
        return;
    }
    {
        const auto info = ch.pre.sourceInfo();
        std::cout << "[ch:" << ch.name << "] source=" << info.description << " " << info.width << "x" << info.height
                  << " fps=" << info.fps << "\n";
    }

    uint64_t submit_seq = 0;
    int read_failures = 0;
    constexpr int kMaxReadFailures = 60;  // 服务器索引/网络抖动容忍 ~30s

    while (!g_stop.load()) {
        calc_pre::PreInferFrame f;
        // 抽帧已前移到 GStreamer (videorate max-rate=target_fps): 读到即送检.
        // 队列满预判: 判丢的帧免做 source_bgr RGA 转换 (高负载下丢帧大头).
        const bool want = pool.HasRoom();
        if (!ch.pre.read(&f, &err, want)) {
            if (g_stop.load()) break;
            if (++read_failures >= kMaxReadFailures) {
                std::cerr << "[ch:" << ch.name << "] read failed x" << read_failures << ", giving up: " << err << "\n";
                break;
            }
            if (read_failures == 1 || read_failures % 10 == 0) {
                std::cerr << "[ch:" << ch.name << "] read retry " << read_failures << "/" << kMaxReadFailures << " ("
                          << err << ")\n";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            continue;
        }
        read_failures = 0;
        ch.frames_read.fetch_add(1);
        const int buf_idx = f.source_dma.buffer_index;

        if (!want) {  // 队列满预判丢帧 (此帧未做 source_bgr 转换)
            ch.frames_dropped.fetch_add(1);
            ch.pre.release_frame(buf_idx);
            continue;
        }

        NpuWorkerPool::InferJob job;
        job.channel_id = ch.id;
        job.seq = submit_seq;
        job.infer_input = app::FromPreInferFrame(f);
        job.release = [&ch, buf_idx] { ch.pre.release_frame(buf_idx); };
        if (!pool.Submit(std::move(job))) {
            ch.frames_dropped.fetch_add(1);
            ch.pre.release_frame(buf_idx);  // 队列满 → 丢帧归还
            continue;
        }
        ++submit_seq;
        ch.frames_submitted.fetch_add(1);
    }
}

void EngineLoop(ChannelRuntime& ch, NpuWorkerPool& pool) {
    uint64_t next_seq = 0;

    while (!g_stop.load()) {
        NpuWorkerPool::InferOutcome oc;
        if (!pool.PopOrdered(ch.id, &next_seq, &oc, 100)) continue;
        next_seq = oc.seq + 1;
        ch.results_done.fetch_add(1);

        calc_post::PostInferInput in;
        in.infer = std::move(oc.result);
        in.source_bgr = oc.source_bgr;
        calc_post::PostInferResult out;
        std::string err;
        const auto prof_t0 = std::chrono::steady_clock::now();
        const uint64_t prof_c0 = ThreadCpuNs();
        if (!ch.post->process(in, &out, &err)) {
            std::cerr << "[ch:" << ch.name << "] post-infer error: " << err << "\n";
        }
        g_prof_post_ns.fetch_add(
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - prof_t0).count(),
            std::memory_order_relaxed);
        g_prof_post_cpu_ns.fetch_add(ThreadCpuNs() - prof_c0, std::memory_order_relaxed);
        g_prof_post_calls.fetch_add(1, std::memory_order_relaxed);
        ch.detections_last.store(out.detections.size());
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::signal(SIGINT, HandleSignal);
    std::signal(SIGTERM, HandleSignal);
    std::signal(SIGSEGV, HandleSegv);
    std::signal(SIGABRT, HandleAbort);
    std::set_terminate(HandleTerminate);

    const std::string config_path = argc > 1 ? argv[1] : "config/yolov5_only.conf";

    calc_config::AppConfig cfg;
    std::string err;
    if (!calc_config::LoadAppConfig(config_path, &cfg, &err)) {
        std::cerr << "[app] config error: " << err << "\n";
        return 1;
    }
    std::vector<std::string> config_errors;
    if (!calc_config::ValidateAppConfig(cfg, &config_errors)) {
        for (const auto& item : config_errors) {
            std::cerr << "[app] config invalid: " << item << "\n";
        }
        return 1;
    }
    if (!CheckZeroCopyConfig(cfg, &err)) {
        std::cerr << "[app] config invalid: " << err << "\n";
        return 1;
    }

    LogTee log_tee("logs", static_cast<size_t>(100) * 1024 * 1024, cfg.runtime.foreground_log);
    if (cfg.runtime.save_log) {
        if (!log_tee.start()) {
            std::cerr << "[app] save_log enabled but log tee start failed; continuing\n";
        } else {
            g_log_tee = &log_tee;
            std::cout << "[app] log file: " << log_tee.current_path() << "\n";
        }
    }

    // GStreamer 初始化早于业务线程
    gst_init(nullptr, nullptr);

    std::cout << "[app] loaded config: " << config_path << "\n";
    std::cout << calc_config::SummarizeAppConfig(cfg);

    // ── Phase 1: NPU 池 (先加载全部模型) ──
    app::AppConfig infer_cfg = calc_config::ToInferConfig(cfg);
    NpuWorkerPool pool;
    if (!pool.Start(cfg.runtime.npu_workers, infer_cfg, &err)) {
        std::cerr << "[app] npu pool start failed: " << err << "\n";
        return 3;
    }
    std::cout << "[app] npu pool started: workers=" << cfg.runtime.npu_workers << "\n";

    // ── Phase 2: 通道 (模型就绪后再开视频源) ──
    std::vector<std::unique_ptr<ChannelRuntime>> channels;
    int ch_id = 0;
    for (size_t ci = 0; ci < cfg.channels.size(); ++ci) {
        const auto& ch_cfg = cfg.channels[ci];
        if (!ch_cfg.enable) continue;
        auto ch = std::make_unique<ChannelRuntime>(calc_config::ToPreInferConfig(cfg, ch_cfg));
        ch->id = ch_id++;
        ch->cfg_index = static_cast<int>(ci);
        ch->name = ch_cfg.name;

        ch->post = std::make_unique<calc_post::PostInferEngine>(calc_config::ToPostInferConfig(cfg));
        if (!ch->post->init(&err)) {
            std::cerr << "[app] ch=" << ch_cfg.name << " post-infer init failed: " << err << "\n";
            return 4;
        }

        channels.push_back(std::move(ch));
    }
    if (channels.empty()) {
        std::cerr << "[app] no enabled channel\n";
        return 1;
    }

    for (auto& ch : channels) {
        // 线程入口一律包一层: 未捕获异常会走 std::terminate → abort, 而 abort 会让进程
        // 带着 MPP 解码器一起拆, 触发内核在 shmem dma-buf 回收路径上的
        // "Bad page cache ... still mapped when deleted" bug。
        // 兜住后改为请求正常停机 —— 析构正常跑, 解码器有序关闭。
        ChannelRuntime* chp = ch.get();
        const auto pre_cfg = calc_config::ToPreInferConfig(cfg, cfg.channels[ch->cfg_index]);
        ch->capture_th = std::thread([chp, &pool, pre_cfg] {
            try {
                CaptureLoop(*chp, pool, pre_cfg);
            } catch (const std::exception& e) {
                std::cerr << "[fatal] ch=" << chp->name << " capture thread threw " << typeid(e).name() << ": "
                          << e.what() << " -- requesting shutdown\n";
                g_stop.store(true);
            } catch (...) {
                std::cerr << "[fatal] ch=" << chp->name
                          << " capture thread threw a non-std exception -- requesting shutdown\n";
                g_stop.store(true);
            }
        });
        ch->engine_th = std::thread([chp, &pool] {
            try {
                EngineLoop(*chp, pool);
            } catch (const std::exception& e) {
                std::cerr << "[fatal] ch=" << chp->name << " engine thread threw " << typeid(e).name() << ": "
                          << e.what() << " -- requesting shutdown\n";
                g_stop.store(true);
            } catch (...) {
                std::cerr << "[fatal] ch=" << chp->name
                          << " engine thread threw a non-std exception -- requesting shutdown\n";
                g_stop.store(true);
            }
        });
    }
    std::cout << "[app] " << channels.size() << " channel(s) started. Press Ctrl+C to stop.\n";

    // ── 心跳 (5s) ──
    CpuSampler cpu_sampler;
    cpu_sampler.sample();
    const int heartbeat_sec = 5;
    while (!g_stop.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        static int tick = 0;
        if (++tick % heartbeat_sec != 0) continue;

        std::ostringstream block;
        const auto pool_stats = pool.GetStats();
        const double avg_infer = pool_stats.completed > 0 ? pool_stats.infer_ms_total / pool_stats.completed : 0.0;
        static uint64_t last_read_total = 0;
        uint64_t read_total = 0;
        for (const auto& ch : channels)
            read_total += ch->frames_read.load();
        const double read_fps = (read_total - last_read_total) / static_cast<double>(heartbeat_sec);
        last_read_total = read_total;

        for (const auto& ch : channels) {
            char line[192];
            std::snprintf(line, sizeof(line), "[perf] ch=%-6s read=%llu sub=%llu done=%llu dets=%llu drop=%llu%s\n",
                          ch->name.c_str(), static_cast<unsigned long long>(ch->frames_read.load()),
                          static_cast<unsigned long long>(ch->frames_submitted.load()),
                          static_cast<unsigned long long>(ch->results_done.load()),
                          static_cast<unsigned long long>(ch->detections_last.load()),
                          static_cast<unsigned long long>(ch->frames_dropped.load()),
                          ch->open_failed.load() ? " OPEN_FAILED" : "");
            block << line;
        }
        {
            char line[224];
            std::snprintf(
                line, sizeof(line),
                "[perf] pool sub=%llu done=%llu fail=%llu drop_sub=%llu drop_mb=%llu infer avg=%.1fms max=%.1fms\n",
                static_cast<unsigned long long>(pool_stats.submitted),
                static_cast<unsigned long long>(pool_stats.completed),
                static_cast<unsigned long long>(pool_stats.failed),
                static_cast<unsigned long long>(pool_stats.dropped_submit),
                static_cast<unsigned long long>(pool_stats.dropped_mailbox), avg_infer, pool_stats.infer_ms_max);
            block << line;
        }
        // 每模型 NPU 纯推理耗时 (rknn_query 直读, 不含排队/CPU)。
        for (const auto& p : app::GetRknnPerfSnapshots()) {
            char line[224];
            std::snprintf(line, sizeof(line), "[npu] %-22s n=%-8llu avg=%.2fms min=%.2fms max=%.2fms\n",
                          p.model.c_str(), static_cast<unsigned long long>(p.count), p.avg_us / 1000.0,
                          p.min_us / 1000.0, p.max_us / 1000.0);
            block << line;
        }
        // post 处理耗时
        {
            const uint64_t pc = g_prof_post_calls.load();
            char line[160];
            std::snprintf(line, sizeof(line), "[prof] post wall=%.3f cpu=%.3f n=%llu\n",
                          pc ? static_cast<double>(g_prof_post_ns.load()) / 1e6 / static_cast<double>(pc) : 0.0,
                          pc ? static_cast<double>(g_prof_post_cpu_ns.load()) / 1e6 / static_cast<double>(pc) : 0.0,
                          static_cast<unsigned long long>(pc));
            block << line;
        }
        if (pool_stats.frames > 0) {
            const double fr = static_cast<double>(pool_stats.frames);
            char line[192];
            std::snprintf(line, sizeof(line),
                          "[model] frames=%llu dets=%llu | detect %.2fms/frame | sum %.2fms/frame\n",
                          static_cast<unsigned long long>(pool_stats.frames),
                          static_cast<unsigned long long>(pool_stats.detections), pool_stats.detect_ms_total / fr,
                          pool_stats.detect_ms_total / fr);
            block << line;
        }
        {
            auto sm = ReadSysMem();
            auto cpu = cpu_sampler.sample();
            char line[192];
            std::snprintf(line, sizeof(line),
                          "[perf] read_fps=%.1f mem proc=%.1fMB sys=%.1f%% cpu proc=%.1f%% sys=%.1f%% temp=%.1f°C\n",
                          read_fps, ProcRssMB(), sm.pct * 100.0, cpu.first, cpu.second, CpuTempCelsius());
            block << line;
        }
        std::cout << block.str();
    }

    // ── 优雅停机 ──
    std::cout << "[app] stopping...\n";
    for (auto& ch : channels) {
        if (ch->capture_th.joinable()) ch->capture_th.join();
    }
    pool.Stop();
    for (auto& ch : channels) {
        if (ch->engine_th.joinable()) ch->engine_th.join();
        std::string flush_err;
        if (!ch->post->flush(&flush_err) && !flush_err.empty()) {
            std::cerr << "[ch:" << ch->name << "] snapshot flush error: " << flush_err << "\n";
        }
        ch->pre.close();
    }
    std::cout << "[app] stopped.\n";
    if (g_log_tee) {
        g_log_tee->stop();
        g_log_tee = nullptr;
    }
    return 0;
}
