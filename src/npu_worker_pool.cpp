#include "npu_worker_pool.h"

#include <chrono>
#include <iostream>
#include <utility>

#include "app/infer_engine.hpp"

bool NpuWorkerPool::Start(int num_workers, const app::AppConfig& infer_cfg, std::string* err) {
    if (num_workers <= 0) {
        if (err) *err = "num_workers must be positive";
        return false;
    }
    infer_cfg_ = infer_cfg;

    // 先初始化全部 engine (模型加载是重操作, 失败要趁早), 再起线程.
    std::vector<std::unique_ptr<app::InferEngine>> engines;
    for (int w = 0; w < num_workers; ++w) {
        app::AppConfig wc = infer_cfg_;
        // 绑定策略完全由配置决定 (yolo/pfld/resnet_core_mask, 默认 auto)。
        // 历史: 旧 0.9.2 驱动下 worker 按 w%3 独占绑核会刷 "invalid run task counter"\n        // 提交失败, 当时强制
        // AUTO。 2026-10-14 换 6.12 系统 (驱动 0.9.8 + runtime 2.3.2) 后重新开放配置绑核; 若在旧系统上回退运行,
        // 配置保持默认 auto 即可。 2026-10-16: yolo_core_mask=rotate → 每 worker 显式绑到 w%3 号核\n        // (每核 4
        // context 顺序队列, 单模型多 worker 比 AUTO 少跨核调度开销).
        if (wc.yolo_core_mask == "rotate") {
            wc.yolo_core_mask = std::to_string(w % 3);
        }
        auto engine = std::make_unique<app::InferEngine>(wc);
        std::string init_err;
        if (!engine->init(&init_err)) {
            if (err) *err = "worker " + std::to_string(w) + " infer init failed: " + init_err;
            return false;
        }
        std::cout << "[pool] worker " << w << " ready (npu core " << (w % 3) << ")\n";
        engines.push_back(std::move(engine));
    }

    stop_.store(false);
    for (auto& e : engines) {
        threads_.emplace_back([this, engine = std::move(e)]() mutable {
            // 兜住 worker 里的异常: 逃出去就是 std::terminate → abort, 而 abort 会让
            // 进程带 MPP 解码器一起拆会触发内核 shmem dma-buf 回收问题。
            // 这里改为停池 —— 上层会发现提交失败并走正常停机。
            try {
                WorkerLoop(std::move(engine));
            } catch (const std::exception& ex) {
                std::cerr << "[pool] worker threw " << typeid(ex).name() << ": " << ex.what() << " -- stopping pool\n";
                stop_.store(true);
            } catch (...) {
                std::cerr << "[pool] worker threw a non-std exception -- stopping pool\n";
                stop_.store(true);
            }
        });
    }
    return true;
}

bool NpuWorkerPool::Submit(InferJob job) {
    {
        std::lock_guard<std::mutex> lock(task_mu_);
        if (stop_.load() || tasks_.size() >= kMaxTasks) {
            std::lock_guard<std::mutex> slock(stats_mu_);
            ++stats_.dropped_submit;
            return false;
        }
        {
            std::lock_guard<std::mutex> slock(stats_mu_);
            ++stats_.submitted;
        }
        tasks_.push_back(std::move(job));
    }
    task_cv_.notify_one();
    return true;
}

bool NpuWorkerPool::HasRoom() {
    std::lock_guard<std::mutex> lock(task_mu_);
    return !stop_.load() && tasks_.size() < kMaxTasks;
}

NpuWorkerPool::Mailbox& NpuWorkerPool::MailboxFor(int channel_id) {
    std::lock_guard<std::mutex> lock(mb_mu_);
    auto& slot = mailboxes_[channel_id];
    if (!slot) slot = std::make_unique<Mailbox>();
    return *slot;
}

bool NpuWorkerPool::PopOrdered(int channel_id, uint64_t* next_seq, InferOutcome* out, int timeout_ms) {
    if (!next_seq || !out) return false;
    auto& mb = MailboxFor(channel_id);
    std::unique_lock<std::mutex> lock(mb.mu);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (!stop_.load()) {
        auto it = mb.by_seq.find(*next_seq);
        if (it != mb.by_seq.end()) {
            *out = std::move(it->second);
            mb.by_seq.erase(it);
            return true;
        }
        if (mb.cv.wait_until(lock, deadline) == std::cv_status::timeout) {
            // 超时: 存在更新 seq → 跳空缺 (容忍 submit 丢帧)
            auto newer = mb.by_seq.lower_bound(*next_seq);
            if (newer != mb.by_seq.end()) {
                *next_seq = newer->first;
                *out = std::move(newer->second);
                mb.by_seq.erase(newer);
                return true;
            }
            return false;
        }
    }
    return false;
}

void NpuWorkerPool::WorkerLoop(std::unique_ptr<app::InferEngine> engine) {
    while (true) {
        InferJob job;
        {
            std::unique_lock<std::mutex> lock(task_mu_);
            task_cv_.wait(lock, [this] { return stop_.load() || !tasks_.empty(); });
            if (tasks_.empty()) {  // stop_ 且队列已排空
                break;
            }
            job = std::move(tasks_.front());
            tasks_.pop_front();
        }

        const auto t0 = std::chrono::steady_clock::now();
        InferOutcome oc;
        oc.channel_id = job.channel_id;
        oc.seq = job.seq;
        oc.source_bgr = job.infer_input.source_bgr;
        std::string err;
        oc.ok = engine->process(job.infer_input, &oc.result, &err);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        if (!oc.ok) {
            std::cerr << "[pool] infer failed ch=" << job.channel_id << " seq=" << job.seq << ": " << err << "\n";
        }
        // RGA 已同步消费完解码 fd — 归还 buffer
        if (job.release) job.release();

        // 统计必须在 oc 被 move 进 mailbox *之前* 取数。
        const bool ok = oc.ok;
        const uint64_t seq = oc.seq;
        if (ok) {
            std::lock_guard<std::mutex> slock(stats_mu_);
            ++stats_.frames;
            stats_.detections += oc.result.detections.size();
            stats_.detect_ms_total += oc.result.timings.detect_ms;
        }

        auto& mb = MailboxFor(job.channel_id);
        {
            std::lock_guard<std::mutex> lock(mb.mu);
            if (mb.by_seq.size() >= Mailbox::kMaxBuffered) {
                mb.by_seq.erase(mb.by_seq.begin());  // 丢最旧
                ++mb.dropped_oldest;
                std::lock_guard<std::mutex> slock(stats_mu_);
                ++stats_.dropped_mailbox;
            }
            mb.by_seq.emplace(seq, std::move(oc));
        }
        mb.cv.notify_all();

        std::lock_guard<std::mutex> slock(stats_mu_);
        ++stats_.completed;
        if (!ok) ++stats_.failed;
        stats_.infer_ms_total += ms;
        if (ms > stats_.infer_ms_max) stats_.infer_ms_max = ms;
    }

    // stop 后排空残余任务并归还 buffer (防 fd 泄漏)
    std::lock_guard<std::mutex> lock(task_mu_);
    while (!tasks_.empty()) {
        if (tasks_.front().release) tasks_.front().release();
        tasks_.pop_front();
    }
}

void NpuWorkerPool::Stop() {
    stop_.store(true);
    task_cv_.notify_all();
    {
        std::lock_guard<std::mutex> lock(mb_mu_);
        for (auto& kv : mailboxes_)
            kv.second->cv.notify_all();
    }
    for (auto& t : threads_) {
        if (t.joinable()) t.join();
    }
}

NpuWorkerPool::Stats NpuWorkerPool::GetStats() {
    std::lock_guard<std::mutex> lock(stats_mu_);
    return stats_;
}
