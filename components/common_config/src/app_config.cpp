#include "calc_common_config/app_config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace calc_config {
namespace {

std::string Trim(const std::string& value) {
    size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin]))) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1]))) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}

bool ParseBool(const std::string& value, bool* out) {
    const std::string v = Lower(Trim(value));
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        *out = true;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off") {
        *out = false;
        return true;
    }
    return false;
}

void SetBool(const std::string& value, bool* out) {
    bool parsed = false;
    if (ParseBool(value, &parsed)) {
        *out = parsed;
    }
}

void SetInt(const std::string& value, int* out) {
    try {
        *out = std::stoi(value);
    } catch (...) {
    }
}

void SetFloat(const std::string& value, float* out) {
    try {
        *out = std::stof(value);
    } catch (...) {
    }
}

void SetDouble(const std::string& value, double* out) {
    try {
        *out = std::stod(value);
    } catch (...) {
    }
}

std::string EnvOrEmpty(const char* key) {
    if (const char* value = std::getenv(key)) {
        if (*value) {
            return value;
        }
    }
    return {};
}

std::vector<std::string> SplitList(const std::string& value) {
    std::vector<std::string> out;
    std::string item;
    std::istringstream ss(value);
    while (std::getline(ss, item, ',')) {
        item = Trim(item);
        if (!item.empty()) out.push_back(item);
    }
    return out;
}

ChannelConfig& FindOrCreateChannel(AppConfig* cfg, const std::string& name) {
    for (auto& ch : cfg->channels) {
        if (ch.name == name) return ch;
    }
    ChannelConfig ch;
    ch.name = name;
    cfg->channels.push_back(ch);
    return cfg->channels.back();
}

// 按 channels.list 重排通道集合: 名单内的按名单顺序排前 (保留已有字段),
// 名单外已声明的通道保留在尾部.
void ApplyChannelList(AppConfig* cfg, const std::string& list_value) {
    const auto names = SplitList(list_value);
    if (names.empty()) return;
    std::vector<ChannelConfig> ordered;
    for (const auto& n : names) {
        auto it = std::find_if(cfg->channels.begin(), cfg->channels.end(),
                               [&](const ChannelConfig& c) { return c.name == n; });
        if (it != cfg->channels.end()) {
            ordered.push_back(*it);
        } else {
            ChannelConfig ch;
            ch.name = n;
            ordered.push_back(ch);
        }
    }
    for (const auto& c : cfg->channels) {
        if (std::find_if(ordered.begin(), ordered.end(), [&](const ChannelConfig& o) { return o.name == c.name; }) ==
            ordered.end()) {
            ordered.push_back(c);
        }
    }
    cfg->channels = ordered;
}

// CHANNEL_CAM0_STREAM_URL 形式: 通道名大写, 非字母数字→'_'
std::string ChannelEnvPrefix(const std::string& name) {
    std::string out = "CHANNEL_";
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        } else {
            out.push_back('_');
        }
    }
    return out;
}

void ApplyChannelField(const std::string& key, const std::string& value, AppConfig* cfg) {
    // key 形如 channel.<name>.<field>
    if (key.rfind("channel.", 0) != 0) return;
    const auto rest = key.substr(std::string("channel.").size());
    const auto dot = rest.rfind('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= rest.size()) {
        cfg->extra[key] = value;
        return;
    }
    const std::string name = rest.substr(0, dot);
    const std::string field = rest.substr(dot + 1);
    auto& ch = FindOrCreateChannel(cfg, name);
    if (field == "stream_url")
        ch.stream_url = value;
    else if (field == "enable")
        SetBool(value, &ch.enable);
    else if (field == "source_id")
        ch.source_id = value;
    else if (field == "codec")
        ch.codec = Lower(Trim(value));
    else if (field == "target_fps")
        SetDouble(value, &ch.target_fps);
    else
        cfg->extra[key] = value;
}

void ApplyEnvOverrides(AppConfig* cfg) {
    if (!cfg) return;

    auto ovBool = [&](const char* env, bool* field) {
        const std::string v = EnvOrEmpty(env);
        if (!v.empty()) SetBool(v, field);
    };
    auto ovInt = [&](const char* env, int* field) {
        const std::string v = EnvOrEmpty(env);
        if (!v.empty()) SetInt(v, field);
    };
    auto ovFloat = [&](const char* env, float* field) {
        const std::string v = EnvOrEmpty(env);
        if (!v.empty()) SetFloat(v, field);
    };
    auto ovDouble = [&](const char* env, double* field) {
        const std::string v = EnvOrEmpty(env);
        if (!v.empty()) SetDouble(v, field);
    };
    auto ovStr = [&](const char* env, std::string* field) {
        const std::string v = EnvOrEmpty(env);
        if (!v.empty()) *field = v;
    };

    // ── Video / pre-infer ──
    ovStr("INPUT_MODE", &cfg->video.input_mode);
    ovStr("SOURCE", &cfg->video.source);
    ovStr("STREAM_URL", &cfg->video.stream_url);
    ovStr("SOURCE_ID", &cfg->video.source_id);
    ovInt("SOURCE_WIDTH", &cfg->video.source_width);
    ovInt("SOURCE_HEIGHT", &cfg->video.source_height);
    ovInt("DETECTOR_WIDTH", &cfg->video.detector_width);
    ovInt("DETECTOR_HEIGHT", &cfg->video.detector_height);
    ovBool("LETTERBOX", &cfg->video.letterbox);
    ovBool("MAP_TO_SOURCE", &cfg->video.map_to_source);
    ovBool("KEEP_SOURCE_BGR", &cfg->video.keep_source_bgr);
    ovBool("PREFER_RGA", &cfg->video.prefer_rga);
    ovBool("REQUIRE_RGA", &cfg->video.require_rga);
    ovInt("VIDEO_LOG_EVERY", &cfg->video.log_every);

    // ── Infer ──
    ovStr("YOLO_MODEL", &cfg->infer.yolo_model);
    ovFloat("CONF_THRES", &cfg->infer.conf_thres);
    ovFloat("IOU_THRES", &cfg->infer.iou_thres);
    ovStr("YOLO_CORE_MASK", &cfg->infer.yolo_core_mask);
    ovInt("INFER_LOG_EVERY", &cfg->infer.log_every);

    // ── Post-infer ──
    ovBool("ENABLE_TRACKING", &cfg->post.enable_tracking);
    ovFloat("TRACK_IOU_THRES", &cfg->post.track_iou_thres);
    ovInt("TRACK_MAX_AGE", &cfg->post.track_max_age);
    ovBool("ENABLE_SNAPSHOT", &cfg->post.enable_snapshot);
    ovDouble("PROCESS_FPS", &cfg->post.process_fps);
    ovDouble("SNAPSHOT_SAVE_INTERVAL_SEC", &cfg->post.snapshot_save_interval_sec);
    ovBool("SNAPSHOT_SAVE_ON_DISAPPEAR", &cfg->post.snapshot_save_on_disappear);
    ovInt("SNAPSHOT_MIN_SOURCE_WIDTH", &cfg->post.snapshot_min_source_width);
    ovInt("SNAPSHOT_MIN_SOURCE_HEIGHT", &cfg->post.snapshot_min_source_height);
    ovDouble("SNAPSHOT_EXPAND_LEFT_RATIO", &cfg->post.snapshot_expand_left_ratio);
    ovDouble("SNAPSHOT_EXPAND_TOP_RATIO", &cfg->post.snapshot_expand_top_ratio);
    ovDouble("SNAPSHOT_EXPAND_RIGHT_RATIO", &cfg->post.snapshot_expand_right_ratio);
    ovDouble("SNAPSHOT_EXPAND_BOTTOM_RATIO", &cfg->post.snapshot_expand_bottom_ratio);
    ovStr("SNAPSHOT_DIR", &cfg->post.snapshot_dir);
    ovInt("SNAPSHOT_JPEG_QUALITY", &cfg->post.snapshot_jpeg_quality);
    ovInt("SNAPSHOT_CACHE_MAX_FILES", &cfg->post.snapshot_cache_max_files);
    ovInt("SNAPSHOT_CACHE_MAX_AGE_SEC", &cfg->post.snapshot_cache_max_age_sec);
    ovInt("POST_LOG_EVERY", &cfg->post.log_every);

    // ── Runtime ──
    ovBool("FOREGROUND_LOG", &cfg->runtime.foreground_log);
    ovBool("SAVE_LOG", &cfg->runtime.save_log);
    ovStr("LOG_DIR", &cfg->runtime.log_dir);
    ovStr("LOG_LEVEL", &cfg->runtime.log_level);
    ovBool("ENABLE_GST", &cfg->runtime.enable_gst);
    ovInt("PIPELINE_QUEUE", &cfg->runtime.pipeline_queue);
    ovInt("NPU_WORKERS", &cfg->runtime.npu_workers);
    ovDouble("TARGET_FPS", &cfg->runtime.target_fps);

    // ── Channels ──
    {
        const std::string v = EnvOrEmpty("CHANNELS_LIST");
        if (!v.empty()) ApplyChannelList(cfg, v);
    }
    for (auto& ch : cfg->channels) {
        const std::string prefix = ChannelEnvPrefix(ch.name);
        ovStr((prefix + "_STREAM_URL").c_str(), &ch.stream_url);
        ovBool((prefix + "_ENABLE").c_str(), &ch.enable);
        ovStr((prefix + "_SOURCE_ID").c_str(), &ch.source_id);
        ovStr((prefix + "_CODEC").c_str(), &ch.codec);
        ovDouble((prefix + "_TARGET_FPS").c_str(), &ch.target_fps);
    }
}

}  // namespace

bool LoadAppConfig(const std::string& path, AppConfig* cfg, std::string* err) {
    if (!cfg) {
        if (err) *err = "null config pointer";
        return false;
    }
    std::ifstream in(path);
    if (!in.is_open()) {
        if (err) *err = "failed to open config: " + path;
        return false;
    }

    std::string line;
    int line_no = 0;
    while (std::getline(in, line)) {
        ++line_no;
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line = line.substr(0, comment);
        }
        line = Trim(line);
        if (line.empty()) {
            continue;
        }

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            if (err) {
                *err = "invalid config line " + std::to_string(line_no) + ": " + line;
            }
            return false;
        }

        const std::string key = Trim(line.substr(0, eq));
        const std::string value = Trim(line.substr(eq + 1));
        if (key.empty()) {
            continue;
        }

        // Video / pre-infer.
        if (key == "input_mode" || key == "video.input_mode")
            cfg->video.input_mode = value;
        else if (key == "source" || key == "video.source")
            cfg->video.source = value;
        else if (key == "stream_url" || key == "video.stream_url")
            cfg->video.stream_url = value;
        else if (key == "source_id" || key == "video.source_id")
            cfg->video.source_id = value;
        else if (key == "source_width" || key == "video.source_width")
            SetInt(value, &cfg->video.source_width);
        else if (key == "source_height" || key == "video.source_height")
            SetInt(value, &cfg->video.source_height);
        else if (key == "detector_width" || key == "video.detector_width")
            SetInt(value, &cfg->video.detector_width);
        else if (key == "detector_height" || key == "video.detector_height")
            SetInt(value, &cfg->video.detector_height);
        else if (key == "letterbox" || key == "video.letterbox")
            SetBool(value, &cfg->video.letterbox);
        else if (key == "map_to_source" || key == "video.map_to_source")
            SetBool(value, &cfg->video.map_to_source);
        else if (key == "keep_source_bgr" || key == "video.keep_source_bgr")
            SetBool(value, &cfg->video.keep_source_bgr);
        else if (key == "prefer_rga" || key == "video.prefer_rga")
            SetBool(value, &cfg->video.prefer_rga);
        else if (key == "require_rga" || key == "video.require_rga")
            SetBool(value, &cfg->video.require_rga);
        else if (key == "video.log_every")
            SetInt(value, &cfg->video.log_every);

        // Infer.
        else if (key == "yolo_model" || key == "infer.yolo_model")
            cfg->infer.yolo_model = value;
        else if (key == "conf_thres" || key == "infer.conf_thres")
            SetFloat(value, &cfg->infer.conf_thres);
        else if (key == "iou_thres" || key == "infer.iou_thres")
            SetFloat(value, &cfg->infer.iou_thres);
        else if (key == "yolo_core_mask" || key == "infer.yolo_core_mask")
            cfg->infer.yolo_core_mask = value;
        else if (key == "infer.log_every")
            SetInt(value, &cfg->infer.log_every);

        // Post-infer.
        else if (key == "enable_tracking" || key == "post.enable_tracking")
            SetBool(value, &cfg->post.enable_tracking);
        else if (key == "track_iou_thres" || key == "post.track_iou_thres")
            SetFloat(value, &cfg->post.track_iou_thres);
        else if (key == "track_max_age" || key == "post.track_max_age")
            SetInt(value, &cfg->post.track_max_age);
        else if (key == "enable_snapshot" || key == "post.enable_snapshot")
            SetBool(value, &cfg->post.enable_snapshot);
        else if (key == "process_fps" || key == "detect_fps" || key == "post.process_fps")
            SetDouble(value, &cfg->post.process_fps);
        else if (key == "database_save_interval_sec" || key == "snapshot_save_interval_sec" ||
                 key == "post.snapshot_save_interval_sec")
            SetDouble(value, &cfg->post.snapshot_save_interval_sec);
        else if (key == "database_save_on_disappear" || key == "snapshot_save_on_disappear" ||
                 key == "post.snapshot_save_on_disappear")
            SetBool(value, &cfg->post.snapshot_save_on_disappear);
        else if (key == "database_disappear_grace_sec" || key == "snapshot_disappear_grace_sec" ||
                 key == "post.snapshot_disappear_grace_sec")
            SetDouble(value, &cfg->post.snapshot_disappear_grace_sec);
        else if (key == "snapshot_min_source_width" || key == "post.snapshot_min_source_width")
            SetInt(value, &cfg->post.snapshot_min_source_width);
        else if (key == "snapshot_min_source_height" || key == "post.snapshot_min_source_height")
            SetInt(value, &cfg->post.snapshot_min_source_height);
        else if (key == "snapshot_expand_left_ratio" || key == "post.snapshot_expand_left_ratio")
            SetDouble(value, &cfg->post.snapshot_expand_left_ratio);
        else if (key == "snapshot_expand_top_ratio" || key == "post.snapshot_expand_top_ratio")
            SetDouble(value, &cfg->post.snapshot_expand_top_ratio);
        else if (key == "snapshot_expand_right_ratio" || key == "post.snapshot_expand_right_ratio")
            SetDouble(value, &cfg->post.snapshot_expand_right_ratio);
        else if (key == "snapshot_expand_bottom_ratio" || key == "post.snapshot_expand_bottom_ratio")
            SetDouble(value, &cfg->post.snapshot_expand_bottom_ratio);
        else if (key == "snapshot_dir" || key == "post.snapshot_dir")
            cfg->post.snapshot_dir = value;
        else if (key == "snapshot_jpeg_quality" || key == "post.snapshot_jpeg_quality")
            SetInt(value, &cfg->post.snapshot_jpeg_quality);
        else if (key == "snapshot_cache_max_files" || key == "post.snapshot_cache_max_files")
            SetInt(value, &cfg->post.snapshot_cache_max_files);
        else if (key == "snapshot_cache_max_age_sec" || key == "post.snapshot_cache_max_age_sec")
            SetInt(value, &cfg->post.snapshot_cache_max_age_sec);
        else if (key == "post.log_every")
            SetInt(value, &cfg->post.log_every);

        // Runtime.
        else if (key == "foreground_log" || key == "runtime.foreground_log")
            SetBool(value, &cfg->runtime.foreground_log);
        else if (key == "save_log" || key == "runtime.save_log")
            SetBool(value, &cfg->runtime.save_log);
        else if (key == "log_dir" || key == "runtime.log_dir")
            cfg->runtime.log_dir = value;
        else if (key == "log_level" || key == "runtime.log_level")
            cfg->runtime.log_level = value;
        else if (key == "enable_gst" || key == "runtime.enable_gst")
            SetBool(value, &cfg->runtime.enable_gst);
        else if (key == "pipeline_queue" || key == "runtime.pipeline_queue")
            SetInt(value, &cfg->runtime.pipeline_queue);
        else if (key == "npu_workers" || key == "runtime.npu_workers")
            SetInt(value, &cfg->runtime.npu_workers);
        else if (key == "detect_fps" || key == "target_fps" || key == "runtime.target_fps")
            SetDouble(value, &cfg->runtime.target_fps);

        // Channels (3588 多路扩展).
        else if (key == "channels.list" || key == "channels_list")
            ApplyChannelList(cfg, value);
        else if (key.rfind("channel.", 0) == 0)
            ApplyChannelField(key, value, cfg);

        else
            cfg->extra[key] = value;
    }

    ApplyEnvOverrides(cfg);

    // 单通道回退: 未声明任何通道 → 合成 cam0
    if (cfg->channels.empty()) {
        ChannelConfig ch;
        ch.name = "cam0";
        cfg->channels.push_back(ch);
    }
    return true;
}

bool ValidateAppConfig(const AppConfig& cfg, std::vector<std::string>* errors) {
    std::vector<std::string> local;
    auto add = [&](const std::string& msg) { local.push_back(msg); };

    if (cfg.video.detector_width <= 0 || cfg.video.detector_height <= 0) {
        add("video detector size must be positive");
    }
    if (cfg.video.input_mode != "rtsp") {
        add("video.input_mode must be rtsp (3588 仅实现 rtsp 拉流)");
    }
    if (cfg.infer.yolo_model.empty()) {
        add("infer.yolo_model is required");
    }
    if (cfg.infer.conf_thres < 0.0f || cfg.infer.conf_thres > 1.0f) {
        add("infer.conf_thres must be in [0,1]");
    }
    if (cfg.infer.iou_thres < 0.0f || cfg.infer.iou_thres > 1.0f) {
        add("infer.iou_thres must be in [0,1]");
    }
    if (cfg.post.snapshot_min_source_width <= 0 || cfg.post.snapshot_min_source_height <= 0) {
        add("post snapshot min size must be positive");
    }
    if (cfg.post.snapshot_jpeg_quality <= 0 || cfg.post.snapshot_jpeg_quality > 100) {
        add("post.snapshot_jpeg_quality must be in [1,100]");
    }
    if (cfg.runtime.pipeline_queue <= 0) {
        add("runtime.pipeline_queue must be positive");
    }
    if (cfg.runtime.npu_workers <= 0) {
        add("runtime.npu_workers must be positive");
    }
    if (cfg.runtime.target_fps <= 0.0) {
        add("runtime.target_fps must be positive");
    }

    // Channels（上限 128，给 100 路压测留余量；资源由 fd/内存/解码器实况约束）
    if (cfg.channels.size() > 128) {
        add("channels count must be <= 128");
    }
    for (size_t i = 0; i < cfg.channels.size(); ++i) {
        const auto& ch = cfg.channels[i];
        for (size_t j = i + 1; j < cfg.channels.size(); ++j) {
            if (cfg.channels[j].name == ch.name) {
                add("duplicate channel name: " + ch.name);
                break;
            }
        }
        if (!ch.enable) continue;
        if (ChannelStreamUrl(cfg, ch).empty()) {
            add("channel." + ch.name + ".stream_url is required (video.stream_url fallback also empty)");
        }
        if (ChannelTargetFps(cfg, ch) <= 0.0) {
            add("channel." + ch.name + " target_fps must be positive (or set runtime.target_fps)");
        }
        const std::string codec = Lower(ch.codec);
        if (codec != "h264" && codec != "h265") {
            add("channel." + ch.name + ".codec must be h264 or h265");
        }
    }

    if (errors) {
        *errors = local;
    }
    return local.empty();
}

std::string SummarizeAppConfig(const AppConfig& cfg) {
    std::ostringstream os;
    os << "video.input_mode=" << cfg.video.input_mode << '\n';
    os << "video.detector_size=" << cfg.video.detector_width << "x" << cfg.video.detector_height << '\n';
    os << "video.keep_source_bgr=" << (cfg.video.keep_source_bgr ? "true" : "false") << '\n';
    os << "video.prefer_rga=" << (cfg.video.prefer_rga ? "true" : "false") << '\n';
    os << "video.require_rga=" << (cfg.video.require_rga ? "true" : "false") << '\n';
    os << "infer.yolo_model=" << cfg.infer.yolo_model << '\n';
    os << "infer.conf_thres=" << cfg.infer.conf_thres << '\n';
    os << "infer.iou_thres=" << cfg.infer.iou_thres << '\n';
    os << "infer.yolo_core_mask=" << cfg.infer.yolo_core_mask << '\n';
    os << "post.enable_tracking=" << (cfg.post.enable_tracking ? "true" : "false") << '\n';
    os << "post.enable_snapshot=" << (cfg.post.enable_snapshot ? "true" : "false") << '\n';
    os << "post.snapshot_dir=" << cfg.post.snapshot_dir << '\n';
    os << "post.snapshot_min_source=" << cfg.post.snapshot_min_source_width << "x"
       << cfg.post.snapshot_min_source_height << '\n';
    os << "post.snapshot_expand=" << cfg.post.snapshot_expand_left_ratio << "/" << cfg.post.snapshot_expand_top_ratio
       << "/" << cfg.post.snapshot_expand_right_ratio << "/" << cfg.post.snapshot_expand_bottom_ratio << '\n';
    os << "runtime.save_log=" << (cfg.runtime.save_log ? "true" : "false") << '\n';
    os << "runtime.pipeline_queue=" << cfg.runtime.pipeline_queue << '\n';
    os << "runtime.npu_workers=" << cfg.runtime.npu_workers << '\n';
    os << "runtime.target_fps=" << cfg.runtime.target_fps << '\n';

    os << "channels.count=" << cfg.channels.size() << '\n';
    for (const auto& ch : cfg.channels) {
        os << "channel." << ch.name << "=url:" << ChannelStreamUrl(cfg, ch) << " fps:" << ChannelTargetFps(cfg, ch)
           << " enable:" << (ch.enable ? "true" : "false") << " codec:" << ch.codec << '\n';
    }
    return os.str();
}

const std::string& ChannelStreamUrl(const AppConfig& cfg, const ChannelConfig& ch) {
    return ch.stream_url.empty() ? cfg.video.stream_url : ch.stream_url;
}

double ChannelTargetFps(const AppConfig& cfg, const ChannelConfig& ch) {
    return ch.target_fps > 0.0 ? ch.target_fps : cfg.runtime.target_fps;
}

}  // namespace calc_config
