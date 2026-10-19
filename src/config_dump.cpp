// 配置系统板上冒烟: 加载配置 → 校验 → 打印 Summarize (含通道解析结果)
// usage: config_dump [config/yolov5_only.conf]
#include <iostream>
#include <vector>

#include "calc_common_config/app_config.hpp"

int main(int argc, char** argv) {
    const std::string path = argc > 1 ? argv[1] : "config/yolov5_only.conf";

    calc_config::AppConfig cfg;
    std::string err;
    if (!calc_config::LoadAppConfig(path, &cfg, &err)) {
        std::cerr << "[config_dump] load error: " << err << "\n";
        return 1;
    }
    std::vector<std::string> errors;
    const bool valid = calc_config::ValidateAppConfig(cfg, &errors);
    for (const auto& e : errors) {
        std::cerr << "[config_dump] invalid: " << e << "\n";
    }
    std::cout << calc_config::SummarizeAppConfig(cfg);
    std::cout << "[config_dump] valid=" << (valid ? "true" : "false") << "\n";
    return valid ? 0 : 2;
}
