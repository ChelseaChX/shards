#include "../gfx.hpp"
#include <gfx/texture.hpp>
#include <gfx/error_utils.hpp>
#include <params.hpp>

using namespace shards;
namespace gfx {

struct TextureShard {
  static inline shards::Types InputTypes{{CoreInfo::ImageType}};
  static SHTypesInfo inputTypes() { return InputTypes; }
  static SHTypesInfo outputTypes() { return Types::Texture; }
  static SHOptionalString help() { return SHCCSTR("Creates a texture from an image"); }

  TexturePtr texture;
  OwnedVar textureVar;

  PARAM_IMPL(TextureShard);

  void warmup(SHContext *context) { PARAM_WARMUP(context); }
  void cleanup() { PARAM_CLEANUP(); }

  SHTypeInfo compose(SHInstanceData &data) {
    auto &inputType = data.inputType;
    return shards::CoreInfo::NoneType;
  }

  SHVar activate(SHContext *shContext, const SHVar &input) {
    auto &image = input.payload.imageValue;

    textureVar.valueType = SHType::Object;
    textureVar.payload.objectTypeId = Types::TextureTypeId;
    textureVar.payload.objectVendorId = gfx::VendorId;
    textureVar.payload.objectValue = &texture;

    if (!texture)
      texture = std::make_shared<Texture>();

    // Float types are not filterable
    if (image.flags & SHIMAGE_FLAGS_32BITS_FLOAT)
      throw formatException("Unfilterable texture format not supported");

    TextureFormat format{};
    switch (image.channels) {
    case 1:
      if (image.flags & SHIMAGE_FLAGS_16BITS_INT)
        format.pixelFormat = WGPUTextureFormat_R16Sint;
      else
        format.pixelFormat = WGPUTextureFormat_R8Uint;
      break;
    case 2:
      if (image.flags & SHIMAGE_FLAGS_16BITS_INT)
        format.pixelFormat = WGPUTextureFormat_RG16Sint;
      else
        format.pixelFormat = WGPUTextureFormat_RG8Uint;
      break;
    case 3:
      throw formatException("RGB textures not supported");
    case 4:
      if (image.flags & SHIMAGE_FLAGS_16BITS_INT)
        format.pixelFormat = WGPUTextureFormat_RGBA16Sint;
      else
        format.pixelFormat = WGPUTextureFormat_RGBA8Uint;
      break;
    }

    auto &inputFormat = Texture::getInputFormat(format.pixelFormat);
    size_t imageSize = inputFormat.pixelSize * image.width * image.height;

    // Copy the data since we can't keep a reference to the image variable
    ImmutableSharedBuffer isb(image.data, imageSize);
    texture->init(format, int2(image.width, image.height), SamplerState(), std::move(isb));

    return SHVar{};
  }
};

void registerMeshShards() { REGISTER_SHARD("GFX.Texture", TextureShard); }
} // namespace gfx
