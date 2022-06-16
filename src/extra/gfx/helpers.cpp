#include "../gfx.hpp"
#include "shards_macros.hpp"
#include <gfx/helpers/wireframe.hpp>

using namespace shards;
namespace gfx {

struct HelperContext {
  static constexpr uint32_t TypeId = 'hlrr';
  static inline shards::Type Type{{SHType::Object, {.object = {.vendorId = VendorId, .typeId = TypeId}}}};

  DrawQueuePtr queue;
  WireframeRenderer renderer;

  HelperContext() : renderer(true) {}
};

struct HighlightBase {
  static inline const char *helperContextVarName = "GFX.HelperRenderer";
  static inline SHExposedTypeInfo highlightRendererInfo =
      shards::ExposedInfo::Variable(helperContextVarName, SHCCSTR("The helper renderer."), HelperContext::Type);
  static inline shards::ExposedInfo requiredInfo = shards::ExposedInfo(highlightRendererInfo);

  SHVar *_highlightRendererVar{nullptr};

  HelperContext &getContext() {
    HelperContext *ptr = reinterpret_cast<HelperContext *>(_highlightRendererVar->payload.objectValue);
    if (!ptr) {
      throw shards::ActivationError("Highlight renderer context not set");
    }
    return *ptr;
  }

  SHExposedTypesInfo requiredVariables() { return SHExposedTypesInfo(Base::requiredInfo); }

  void baseConsumerWarmup(SHContext *context) {
    _highlightRendererVar = shards::referenceVariable(context, helperContextVarName);
    assert(_highlightRendererVar->valueType == SHType::Object);
  }

  void baseConsumerCleanup() {
    if (_highlightRendererVar) {
      shards::releaseVariable(_highlightRendererVar);
      _highlightRendererVar = nullptr;
    }
  }
};

struct HelperContextShard {
  static SHTypesInfo inputTypes() { return CoreInfo::AnyType; }
  static SHTypesInfo outputTypes() { return CoreInfo::AnyType; }
  static SHOptionalString help() {
    return SHCCSTR("Provides a scope to render helper geometry within (lines,gizmos,highlights)");
  }

  static SHParametersInfo parameters() {
    static shards::Parameters params{
        {"Queue", SHCCSTR("The queue to use"), {Type::VariableOf(Types::DrawQueue)}},
        {"View", SHCCSTR("The view used "), {Type::VariableOf(Types::View)}},
        {"Content", SHCCSTR("The helpers to draw"), {CoreInfo::ShardsOrNone}},
    };
    return params;
  }

  ParamVar _queueVar{};
  ShardsVar _shards{};
  HelperContext context{};
  SHVar *_contextVar{nullptr};

  void setParam(int index, const SHVar &value) {
    switch (index) {
    case 0:
      _queueVar = value;
      break;
    case 1:
      _shards = value;
      break;
    default:
      break;
    }
  }

  SHVar getParam(int index) {
    switch (index) {
    case 0:
      return _queueVar;
    case 1:
      return _shards;
      break;
    default:
      return Var::Empty;
    }
  }

  void warmup(SHContext *shContext) {
    _contextVar = referenceVariable(shContext, HighlightBase::helperContextVarName);
    _contextVar->payload.objectTypeId = HelperContext::TypeId;
    _contextVar->payload.objectValue = &context;
    _contextVar->valueType = SHType::Object;

    _queueVar.warmup(shContext);
    _shards.warmup(shContext);
  }

  void cleanup() {
    _queueVar.cleanup();
    _shards.cleanup();

    if (_contextVar) {
      if (_contextVar->refcount > 1) {
        SHLOG_ERROR("Found {} dangling reference(s) to GFX.HelperRenderer", _contextVar->refcount - 1);
      }
      releaseVariable(_contextVar);
    }
  }

  SHTypeInfo compose(SHInstanceData &data) {
    // twice to actually own the data and release...
    IterableExposedInfo rshared(data.shared);
    IterableExposedInfo shared(rshared);
    shared.push_back(ExposedInfo::ProtectedVariable(HighlightBase::helperContextVarName, SHCCSTR("The graphics context"),
                                                    HelperContext::Type));
    data.shared = shared;

    return _shards.compose(data).outputType;
  }

  SHVar activate(SHContext *shContext, const SHVar &input) {
    SHVar queueVar = _queueVar.get();
    assert(queueVar.payload.objectValue);
    context.queue = static_cast<SHDrawQueue *>(queueVar.payload.objectValue)->queue;

    SHVar _shardsOutput{};
    _shards.activate(shContext, input, _shardsOutput);
    return _shardsOutput;
  }
};

struct HighlightShard : public HighlightBase {
  // TODO: Merge with DrawShard type
  static inline shards::Types SingleDrawableTypes = shards::Types{Types::Drawable, Types::DrawableHierarchy};
  static inline Type DrawableSeqType = Type::SeqOf(SingleDrawableTypes);
  static inline shards::Types DrawableTypes{Types::Drawable, Types::DrawableHierarchy, DrawableSeqType};

  static SHTypesInfo inputTypes() { return DrawableTypes; }
  static SHTypesInfo outputTypes() { return CoreInfo::NoneType; }
  static SHOptionalString help() { return SHCCSTR("Queues a draw operation to highlight a drawable"); }

  SHTypeInfo compose(const SHInstanceData &data) {
    if (data.inputType.basicType == SHType::Seq) {
      OVERRIDE_ACTIVATE(data, activateSeq);
    } else {
      OVERRIDE_ACTIVATE(data, activateSingle);
    }
    return CoreInfo::NoneType;
  }

  SHVar activateSingle(SHContext *shContext, const SHVar &input) {
    HelperContext &ctx = getContext();

    SHTypeInfo inputType{.basicType = SHType::Object,
                         .object = {.vendorId = input.payload.objectVendorId, .typeId = input.payload.objectTypeId}};
    if (Types::Drawable == inputType) {
      SHDrawable *dPtr = static_cast<SHDrawable *>(input.payload.objectValue);
      ctx.renderer.overlayWireframe(ctx.queue, dPtr->drawable);
    } else if (Types::DrawableHierarchy == inputType) {
      SHDrawableHierarchy *dhPtr = static_cast<SHDrawableHierarchy *>(input.payload.objectValue);
      ctx.renderer.overlayWireframe(ctx.queue, dhPtr->drawableHierarchy);
    }

    return SHVar{};
  }

  SHVar activateSeq(SHContext *shContext, const SHVar &input) {
    auto &seq = input.payload.seqValue;
    for (size_t i = 0; i < seq.len; i++) {
      (void)activateSingle(shContext, seq.elements[i]);
    }

    return SHVar{};
  }

  SHVar activate(SHContext *shContext, const SHVar &input) { throw ActivationError("Unsupported input type"); }
  void warmup(SHContext *shContext) { baseConsumerWarmup(shContext); }
  void cleanup() { baseConsumerCleanup(); }
};

void registerHighlightShards() {
  REGISTER_SHARD("GFX.Highlight", HighlightShard);
  REGISTER_SHARD("GFX.HelperRenderer", HelperContextShard);
}
} // namespace gfx