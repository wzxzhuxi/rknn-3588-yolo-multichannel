#ifndef CALC_COMMON_CONFIG_PROJECT_ADAPTERS_HPP
#define CALC_COMMON_CONFIG_PROJECT_ADAPTERS_HPP

#include "app/app_config.hpp"
#include "calc_common_config/app_config.hpp"
#include "calc_post_infer/post_config.hpp"
#include "calc_pre_infer/config.hpp"

namespace calc_config {

// AppConfig → 组件配置. 与 1126b 一致: ToInferConfig 额外把 video.letterbox
// 拷进 infer 结构的自有 letterbox; pre 适配按通道填 stream_url/codec/source_id.
calc_pre::PreInferConfig ToPreInferConfig(const AppConfig& cfg, const ChannelConfig& ch);
app::AppConfig ToInferConfig(const AppConfig& cfg);
calc_post::PostInferConfig ToPostInferConfig(const AppConfig& cfg);

}  // namespace calc_config

#endif  // CALC_COMMON_CONFIG_PROJECT_ADAPTERS_HPP
