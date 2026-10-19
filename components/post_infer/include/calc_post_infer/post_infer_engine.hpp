#ifndef CALC_POST_INFER_ENGINE_HPP
#define CALC_POST_INFER_ENGINE_HPP

#include <memory>
#include <string>

#include "calc_post_infer/person_tracker.hpp"
#include "calc_post_infer/post_config.hpp"
#include "calc_post_infer/post_types.hpp"
#include "calc_post_infer/snapshot_manager.hpp"

namespace calc_post {

class PostInferEngine {
  public:
    explicit PostInferEngine(PostInferConfig config);
    ~PostInferEngine();

    bool init(std::string* err);
    bool process(const PostInferInput& input, PostInferResult* output, std::string* err);
    bool flush(std::string* err);

  private:
    PostInferConfig cfg_;
    std::unique_ptr<PersonTracker> tracker_;
    std::unique_ptr<SnapshotManager> snapshot_manager_;
    bool ready_ = false;
};

}  // namespace calc_post

#endif  // CALC_POST_INFER_ENGINE_HPP
