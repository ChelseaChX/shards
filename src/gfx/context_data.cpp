#include "context_data.hpp"
#include "context.hpp"
#include <spdlog/spdlog.h>

namespace gfx {
void ContextData::bindToContext(Context &context) {
  assert(!this->context);
  this->context = &context;
  this->context->addContextDataInternal(weak_from_this());
}

void ContextData::unbindFromContext() {
  assert(context);
  context->removeContextDataInternal(this);
  context = nullptr;
}

void ContextData::releaseContextDataConditional() {
  if (context) {
    SPDLOG_INFO("releaseContextData({})", getDebugTag());

    releaseContextData();
    unbindFromContext();
  }
}

} // namespace gfx
