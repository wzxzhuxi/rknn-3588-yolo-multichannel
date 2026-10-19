#ifndef CALC_INFER_ENGINE_HPP
#define CALC_INFER_ENGINE_HPP

#include <memory>
#include <string>

#include "app/app_config.hpp"
#include "app/infer_types.hpp"

namespace app {

class Detector;

class InferEngine {
  public:
    explicit InferEngine(AppConfig config);
    ~InferEngine();

    bool init(std::string* err);
    bool process(const InferInputFrame& input, InferResultFrame* output, std::string* err);

  private:
    AppConfig cfg_;
    std::unique_ptr<Detector> detector_;
};

}  // namespace app

#endif  // CALC_INFER_ENGINE_HPP
