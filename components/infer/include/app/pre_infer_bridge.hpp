#ifndef CALC_INFER_PRE_INFER_BRIDGE_HPP
#define CALC_INFER_PRE_INFER_BRIDGE_HPP

#include "app/infer_types.hpp"
#include "calc_pre_infer/types.hpp"

namespace app {

InferInputFrame FromPreInferFrame(const calc_pre::PreInferFrame& frame);

}  // namespace app

#endif  // CALC_INFER_PRE_INFER_BRIDGE_HPP
