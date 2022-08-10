#include "../gfx.hpp"
#include <gfx/texture.hpp>
#include <params.hpp>

using namespace shards;
namespace gfx {

struct TextureShard {
  static inline shards::Types InputTypes{{CoreInfo::ImageType}};
  static SHTypesInfo inputTypes() { return InputTypes; }
  static SHTypesInfo outputTypes() { return CoreInfo::NoneType; }
  static SHOptionalString help() { return SHCCSTR("Creates a texture from an image"); }

  PARAM_IMPL(TextureShard);

  void warmup(SHContext *context) { PARAM_WARMUP(context); }
  void cleanup() { PARAM_CLEANUP(); }

  SHTypeInfo compose(SHInstanceData &data) {
    auto &inputType = data.inputType;
    return shards::CoreInfo::NoneType;
  }

  SHVar activate(SHContext *shContext, const SHVar &input) { return SHVar{}; }
};

void registerMeshShards() { REGISTER_SHARD("GFX.Texture", TextureShard); }
} // namespace gfx
