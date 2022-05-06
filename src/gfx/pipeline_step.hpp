#ifndef GFX_PIPELINE_STEP
#define GFX_PIPELINE_STEP

#include "fwd.hpp"
#include <vector>

namespace gfx {

enum class SortMode {
  // Optimal for batching
  Batch,
  // For transparent object rendering
  BackToFront,
};

struct RenderDrawablesStep {
  std::vector<FeaturePtr> features;
  SortMode sortMode = SortMode::Batch;
};

typedef std::variant<RenderDrawablesStep> PipelineStep;
typedef std::shared_ptr<PipelineStep> PipelineStepPtr;
typedef std::vector<PipelineStepPtr> PipelineSteps;

PipelineStepPtr makeDrawablePipelineStep(RenderDrawablesStep &&step);
} // namespace gfx

#endif // GFX_PIPELINE_STEP
