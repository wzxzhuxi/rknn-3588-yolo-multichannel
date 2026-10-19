#ifndef CALC_NPU_WORKER_POOL_H
#define CALC_NPU_WORKER_POOL_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "app/app_config.hpp"
#include "app/infer_types.hpp"

namespace app {
class InferEngine;
}

// N 通道共享的 NPU 推理池. 每个 worker 持有一个 YOLOv5s InferEngine,
// 按 w%3 绑 NPU 核. 任务有界, 结果按通道 mailbox 保序.
class NpuWorkerPool {
  public:
    struct InferJob {
        int channel_id = -1;
        uint64_t seq = 0;
        app::InferInputFrame infer_input;
        // worker 用完帧后归还解码 buffer (RGA 同步消费完 fd 才回调).
        std::function<void()> release;
    };

    struct InferOutcome {
        int channel_id = -1;
        uint64_t seq = 0;
        bool ok = false;
        app::InferResultFrame result;
        cv::Mat source_bgr;
    };

    struct Stats {
        uint64_t submitted = 0;
        uint64_t completed = 0;
        uint64_t dropped_submit = 0;
        uint64_t failed = 0;
        uint64_t dropped_mailbox = 0;
        double infer_ms_total = 0.0;
        double infer_ms_max = 0.0;

        double detect_ms_total = 0.0;
        uint64_t frames = 0;
        uint64_t detections = 0;
    };

    // num_workers 个 InferEngine, core mask 按 w%3 轮转 (config 的 yolo_core_mask
    // 被 per-worker 覆盖). 任一 worker 初始化失败整体失败.
    bool Start(int num_workers, const app::AppConfig& infer_cfg, std::string* err);

    // 有界提交; 满则返回 false — 调用方必须丢帧并自行调 job.release().
    bool Submit(InferJob job);

    // 队列是否有余量。提交前的廉价预检: 队列满时可提前判丢,
    // 让调用方跳过该帧的重预处理 (source_bgr RGA 转换) —— 高负载下这项
    // 转换是纯浪费.
    bool HasRoom();

    // 按序取结果: 优先等 seq==*next_seq; 超时后若存在更新的 seq 则跳过空缺
    // (*next_seq 前移到现有最小值, 容忍丢帧). 返回 false = 本次无结果 (超时/停止).
    bool PopOrdered(int channel_id, uint64_t* next_seq, InferOutcome* out, int timeout_ms);

    void Stop();
    Stats GetStats();

  private:
    struct Mailbox {
        std::mutex mu;
        std::condition_variable cv;
        std::map<uint64_t, InferOutcome> by_seq;
        static constexpr size_t kMaxBuffered = 8;
        uint64_t dropped_oldest = 0;
    };

    Mailbox& MailboxFor(int channel_id);
    void WorkerLoop(std::unique_ptr<app::InferEngine> engine);

    app::AppConfig infer_cfg_;
    std::atomic_bool stop_{false};
    std::vector<std::thread> threads_;

    std::mutex task_mu_;
    std::condition_variable task_cv_;
    std::deque<InferJob> tasks_;
    static constexpr size_t kMaxTasks = 32;

    std::mutex mb_mu_;
    std::map<int, std::unique_ptr<Mailbox>> mailboxes_;

    std::mutex stats_mu_;
    Stats stats_;
};

#endif  // CALC_NPU_WORKER_POOL_H
