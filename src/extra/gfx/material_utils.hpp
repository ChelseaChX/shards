#ifndef SH_EXTRA_GFX_MATERIAL_UTILS
#define SH_EXTRA_GFX_MATERIAL_UTILS

#include "shards_types.hpp"
#include <shards.hpp>
#include <foundation.hpp>
#include <gfx/material.hpp>
#include <gfx/params.hpp>
#include <magic_enum.hpp>
#include <spdlog/spdlog.h>

namespace gfx {
inline bool varToParam(const SHVar &var, ParamVariant &outVariant) {
  switch (var.valueType) {
  case SHType::Float: {
    float vec;
    memcpy(&vec, &var.payload.floatValue, sizeof(float));
    outVariant = vec;
  } break;
  case SHType::Float2: {
    float2 vec;
    memcpy(&vec.x, &var.payload.float2Value, sizeof(float) * 2);
    outVariant = vec;
  } break;
  case SHType::Float3: {
    float3 vec;
    memcpy(&vec.x, &var.payload.float3Value, sizeof(float) * 3);
    outVariant = vec;
  } break;
  case SHType::Float4: {
    float4 vec;
    memcpy(&vec.x, &var.payload.float4Value, sizeof(float) * 4);
    outVariant = vec;

  } break;
  case SHType::Seq:
    if (var.innerType == SHType::Float4) {
      float4x4 matrix;
      const SHSeq &seq = var.payload.seqValue;
      for (size_t i = 0; i < std::min<size_t>(4, seq.len); i++) {
        float4 row;
        memcpy(&row.x, &seq.elements[i].payload.float4Value, sizeof(float) * 4);
        matrix[i] = row;
      }
      outVariant = matrix;
    } else {
      spdlog::error("Seq inner type {} can not be converted to ParamVariant", magic_enum::enum_name(var.valueType));
      return false;
    }
    break;
  default:
    spdlog::error("Value type {} can not be converted to ParamVariant", magic_enum::enum_name(var.valueType));
    return false;
  }
  return true;
}

inline void initConstantShaderParams(MaterialParameters &out, SHTable &paramsTable) {
  SHTableIterator it{};
  SHString key{};
  SHVar value{};
  paramsTable.api->tableGetIterator(paramsTable, &it);
  while (paramsTable.api->tableNext(paramsTable, &it, &key, &value)) {
    ParamVariant variant;
    if (varToParam(value, variant)) {
      out.set(key, variant);
    }
  }
}

inline void initReferencedShaderParams(SHContext *shContext, SHShaderParameters &shShaderParameters, SHTable &paramsTable) {
  SHTableIterator it{};
  SHString key{};
  SHVar value{};
  paramsTable.api->tableGetIterator(paramsTable, &it);
  while (paramsTable.api->tableNext(paramsTable, &it, &key, &value)) {
    shards::ParamVar paramVar(value);
    paramVar.warmup(shContext);
    shShaderParameters.basic.emplace_back(key, std::move(paramVar));
  }
}

inline void validateShaderParamsType(SHTypeInfo &type) {
  using shards::ComposeError;

  if (type.basicType != SHType::Table) {
    throw ComposeError(fmt::format("Wrong type for Params: {}, should be a table", magic_enum::enum_name(type.basicType)));
  }

  size_t tableLen = type.table.types.len;
  for (size_t i = 0; i < tableLen; i++) {
    const char *key = type.table.keys.elements[i];
    auto valueType = type.table.types.elements[i];
    bool matched = false;
    for (auto &supportedType : Types::ShaderParamTypes._types) {
      if (valueType == supportedType) {
        matched = true;
        break;
      }
    }

    if (!matched) {
      throw ComposeError(
          fmt::format("Unsupported parameter type for param {}: {}", key, magic_enum::enum_name(valueType.basicType)));
    }
  }
}
} // namespace gfx

#endif // SH_EXTRA_GFX_MATERIAL_UTILS
