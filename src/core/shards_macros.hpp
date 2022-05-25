/* SPDX-License-Identifier: BSD-3-Clause */
/* Copyright © 2019 Fragcolor Pte. Ltd. */

// TODO, get rid of this, use Shardswrapper

#ifndef SH_CORE_SHARDS_MACROS
#define SH_CORE_SHARDS_MACROS

#define RUNTIME_SHARD(_namespace_, _name_)                                                                                \
  struct _name_##Runtime {                                                                                                \
    Shard header;                                                                                                        \
    _name_ core;                                                                                                          \
  };                                                                                                                      \
  __cdecl Shard *createShard##_name_() {                                                                                 \
    Shard *result = reinterpret_cast<Shard *>(new (std::align_val_t{16}) _name_##Runtime());                            \
    result->name = static_cast<SHNameProc>([](Shard *shard) { return #_namespace_ "." #_name_; });                       \
    result->hash = static_cast<SHHashProc>([](Shard *shard) {                                                            \
      return ::shards::constant<::shards::crc32(#_namespace_ "." #_name_ SHARDS_CURRENT_ABI_STR)>::value;  \
    });                                                                                                                   \
    result->help = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                             \
    result->setup = static_cast<SHSetupProc>([](Shard *shard) {});                                                       \
    result->destroy = static_cast<SHDestroyProc>([](Shard *shard) {                                                      \
      auto blk = (_name_##Runtime *)shard;                                                                                \
      blk->_name_##Runtime::~_name_##Runtime();                                                                           \
      ::operator delete ((_name_##Runtime *)shard, std::align_val_t{16});                                                 \
    });                                                                                                                   \
    result->inputTypes = static_cast<SHInputTypesProc>([](Shard *shard) { return SHTypesInfo(); });                      \
    result->outputTypes = static_cast<SHOutputTypesProc>([](Shard *shard) { return SHTypesInfo(); });                    \
    result->exposedVariables = static_cast<SHExposedVariablesProc>([](Shard *shard) { return SHExposedTypesInfo(); });   \
    result->requiredVariables = static_cast<SHRequiredVariablesProc>([](Shard *shard) { return SHExposedTypesInfo(); }); \
    result->parameters = static_cast<SHParametersProc>([](Shard *shard) { return SHParametersInfo(); });                 \
    result->setParam = static_cast<SHSetParamProc>([](Shard *shard, int index, const SHVar *value) {});                  \
    result->getParam = static_cast<SHGetParamProc>([](Shard *shard, int index) { return SHVar(); });                     \
    result->compose = nullptr;                                                                                            \
    result->warmup = nullptr;                                                                                             \
    result->inputHelp = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                        \
    result->outputHelp = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                       \
    result->properties = static_cast<SHPropertiesProc>([](Shard *shard) -> const SHTable * { return nullptr; });         \
    result->nextFrame = nullptr;                                                                                          \
    result->mutate = nullptr;                                                                                             \
    result->cleanup = static_cast<SHCleanupProc>([](Shard *shard) {});

#define RUNTIME_CORE_SHARD(_name_)                                                                                        \
  struct _name_##Runtime {                                                                                                \
    Shard header;                                                                                                        \
    _name_ core;                                                                                                          \
  };                                                                                                                      \
  __cdecl Shard *createShard##_name_() {                                                                                 \
    Shard *result = reinterpret_cast<Shard *>(new (std::align_val_t{16}) _name_##Runtime());                            \
    result->name = static_cast<SHNameProc>([](Shard *shard) { return #_name_; });                                        \
    result->hash = static_cast<SHHashProc>([](Shard *shard) {                                                            \
      return ::shards::constant<::shards::crc32(#_name_ SHARDS_CURRENT_ABI_STR)>::value;                   \
    });                                                                                                                   \
    result->help = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                             \
    result->setup = static_cast<SHSetupProc>([](Shard *shard) {});                                                       \
    result->destroy = static_cast<SHDestroyProc>([](Shard *shard) {                                                      \
      auto blk = (_name_##Runtime *)shard;                                                                                \
      blk->_name_##Runtime::~_name_##Runtime();                                                                           \
      ::operator delete ((_name_##Runtime *)shard, std::align_val_t{16});                                                 \
    });                                                                                                                   \
    result->inputTypes = static_cast<SHInputTypesProc>([](Shard *shard) { return SHTypesInfo(); });                      \
    result->outputTypes = static_cast<SHOutputTypesProc>([](Shard *shard) { return SHTypesInfo(); });                    \
    result->exposedVariables = static_cast<SHExposedVariablesProc>([](Shard *shard) { return SHExposedTypesInfo(); });   \
    result->requiredVariables = static_cast<SHRequiredVariablesProc>([](Shard *shard) { return SHExposedTypesInfo(); }); \
    result->parameters = static_cast<SHParametersProc>([](Shard *shard) { return SHParametersInfo(); });                 \
    result->setParam = static_cast<SHSetParamProc>([](Shard *shard, int index, const SHVar *value) {});                  \
    result->getParam = static_cast<SHGetParamProc>([](Shard *shard, int index) { return SHVar(); });                     \
    result->compose = nullptr;                                                                                            \
    result->warmup = nullptr;                                                                                             \
    result->inputHelp = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                        \
    result->outputHelp = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                       \
    result->properties = static_cast<SHPropertiesProc>([](Shard *shard) -> const SHTable * { return nullptr; });         \
    result->nextFrame = nullptr;                                                                                          \
    result->mutate = nullptr;                                                                                             \
    result->cleanup = static_cast<SHCleanupProc>([](Shard *shard) {});

#define RUNTIME_SHARD_TYPE(_namespace_, _name_) \
  struct _name_##Runtime {                      \
    Shard header;                              \
    _name_ core;                                \
  };
#define RUNTIME_SHARD_FACTORY(_namespace_, _name_)                                                                        \
  __cdecl Shard *createShard##_name_() {                                                                                 \
    Shard *result = reinterpret_cast<Shard *>(new (std::align_val_t{16}) _name_##Runtime());                            \
    result->name = static_cast<SHNameProc>([](Shard *shard) { return #_namespace_ "." #_name_; });                       \
    result->hash = static_cast<SHHashProc>([](Shard *shard) {                                                            \
      return ::shards::constant<::shards::crc32(#_namespace_ "." #_name_ SHARDS_CURRENT_ABI_STR)>::value;  \
    });                                                                                                                   \
    result->help = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                             \
    result->setup = static_cast<SHSetupProc>([](Shard *shard) {});                                                       \
    result->destroy = static_cast<SHDestroyProc>([](Shard *shard) {                                                      \
      auto blk = (_name_##Runtime *)shard;                                                                                \
      blk->_name_##Runtime::~_name_##Runtime();                                                                           \
      ::operator delete ((_name_##Runtime *)shard, std::align_val_t{16});                                                 \
    });                                                                                                                   \
    result->inputTypes = static_cast<SHInputTypesProc>([](Shard *shard) { return SHTypesInfo(); });                      \
    result->outputTypes = static_cast<SHOutputTypesProc>([](Shard *shard) { return SHTypesInfo(); });                    \
    result->exposedVariables = static_cast<SHExposedVariablesProc>([](Shard *shard) { return SHExposedTypesInfo(); });   \
    result->requiredVariables = static_cast<SHRequiredVariablesProc>([](Shard *shard) { return SHExposedTypesInfo(); }); \
    result->parameters = static_cast<SHParametersProc>([](Shard *shard) { return SHParametersInfo(); });                 \
    result->setParam = static_cast<SHSetParamProc>([](Shard *shard, int index, const SHVar *value) {});                  \
    result->getParam = static_cast<SHGetParamProc>([](Shard *shard, int index) { return SHVar(); });                     \
    result->compose = nullptr;                                                                                            \
    result->warmup = nullptr;                                                                                             \
    result->inputHelp = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                        \
    result->outputHelp = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                       \
    result->properties = static_cast<SHPropertiesProc>([](Shard *shard) -> const SHTable * { return nullptr; });         \
    result->nextFrame = nullptr;                                                                                          \
    result->mutate = nullptr;                                                                                             \
    result->cleanup = static_cast<SHCleanupProc>([](Shard *shard) {});

#define RUNTIME_CORE_SHARD_TYPE(_name_) \
  struct _name_##Runtime {              \
    Shard header;                      \
    _name_ core;                        \
  };
#define RUNTIME_CORE_SHARD_FACTORY(_name_)                                                                                \
  __cdecl Shard *createShard##_name_() {                                                                                 \
    Shard *result = reinterpret_cast<Shard *>(new (std::align_val_t{16}) _name_##Runtime());                            \
    result->name = static_cast<SHNameProc>([](Shard *shard) { return #_name_; });                                        \
    result->hash = static_cast<SHHashProc>([](Shard *shard) {                                                            \
      return ::shards::constant<::shards::crc32(#_name_ SHARDS_CURRENT_ABI_STR)>::value;                   \
    });                                                                                                                   \
    result->help = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                             \
    result->setup = static_cast<SHSetupProc>([](Shard *shard) {});                                                       \
    result->destroy = static_cast<SHDestroyProc>([](Shard *shard) {                                                      \
      auto blk = (_name_##Runtime *)shard;                                                                                \
      blk->_name_##Runtime::~_name_##Runtime();                                                                           \
      ::operator delete ((_name_##Runtime *)shard, std::align_val_t{16});                                                 \
    });                                                                                                                   \
    result->inputTypes = static_cast<SHInputTypesProc>([](Shard *shard) { return SHTypesInfo(); });                      \
    result->outputTypes = static_cast<SHOutputTypesProc>([](Shard *shard) { return SHTypesInfo(); });                    \
    result->exposedVariables = static_cast<SHExposedVariablesProc>([](Shard *shard) { return SHExposedTypesInfo(); });   \
    result->requiredVariables = static_cast<SHRequiredVariablesProc>([](Shard *shard) { return SHExposedTypesInfo(); }); \
    result->parameters = static_cast<SHParametersProc>([](Shard *shard) { return SHParametersInfo(); });                 \
    result->setParam = static_cast<SHSetParamProc>([](Shard *shard, int index, const SHVar *value) {});                  \
    result->getParam = static_cast<SHGetParamProc>([](Shard *shard, int index) { return SHVar(); });                     \
    result->compose = nullptr;                                                                                            \
    result->warmup = nullptr;                                                                                             \
    result->inputHelp = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                        \
    result->outputHelp = static_cast<SHHelpProc>([](Shard *shard) { return SHOptionalString(); });                       \
    result->properties = static_cast<SHPropertiesProc>([](Shard *shard) -> const SHTable * { return nullptr; });         \
    result->nextFrame = nullptr;                                                                                          \
    result->mutate = nullptr;                                                                                             \
    result->cleanup = static_cast<SHCleanupProc>([](Shard *shard) {});

// Those get nicely inlined fully so only 1 indirection will happen at the root
// of the call if the shard is all inline
#define RUNTIME_SHARD_name(_name_) \
  result->name = static_cast<SHSetupProc>([](Shard *shard) { return reinterpret_cast<_name_##Runtime *>(shard)->core.name(); });
#define RUNTIME_SHARD_help(_name_) \
  result->help = static_cast<SHHelpProc>([](Shard *shard) { return reinterpret_cast<_name_##Runtime *>(shard)->core.help(); });
#define RUNTIME_SHARD_inputHelp(_name_) \
  result->inputHelp =                   \
      static_cast<SHHelpProc>([](Shard *shard) { return reinterpret_cast<_name_##Runtime *>(shard)->core.inputHelp(); });
#define RUNTIME_SHARD_outputHelp(_name_) \
  result->outputHelp =                   \
      static_cast<SHHelpProc>([](Shard *shard) { return reinterpret_cast<_name_##Runtime *>(shard)->core.outputHelp(); });

#define RUNTIME_SHARD_setup(_name_) \
  result->setup = static_cast<SHSetupProc>([](Shard *shard) { reinterpret_cast<_name_##Runtime *>(shard)->core.setup(); });
#define RUNTIME_SHARD_destroy(_name_)                                   \
  result->destroy = static_cast<SHDestroyProc>([](Shard *shard) {      \
    auto blk = (_name_##Runtime *)shard;                                \
    reinterpret_cast<_name_##Runtime *>(shard)->core.destroy();         \
    blk->_name_##Runtime::~_name_##Runtime();                           \
    ::operator delete ((_name_##Runtime *)shard, std::align_val_t{16}); \
  });

#define RUNTIME_SHARD_inputTypes(_name_)              \
  result->inputTypes = static_cast<SHInputTypesProc>( \
      [](Shard *shard) { return reinterpret_cast<_name_##Runtime *>(shard)->core.inputTypes(); });
#define RUNTIME_SHARD_outputTypes(_name_)               \
  result->outputTypes = static_cast<SHOutputTypesProc>( \
      [](Shard *shard) { return reinterpret_cast<_name_##Runtime *>(shard)->core.outputTypes(); });

#define RUNTIME_SHARD_exposedVariables(_name_)                    \
  result->exposedVariables = static_cast<SHExposedVariablesProc>( \
      [](Shard *shard) { return reinterpret_cast<_name_##Runtime *>(shard)->core.exposedVariables(); });
#define RUNTIME_SHARD_requiredVariables(_name_)                     \
  result->requiredVariables = static_cast<SHRequiredVariablesProc>( \
      [](Shard *shard) { return reinterpret_cast<_name_##Runtime *>(shard)->core.requiredVariables(); });

#define RUNTIME_SHARD_parameters(_name_)              \
  result->parameters = static_cast<SHParametersProc>( \
      [](Shard *shard) { return reinterpret_cast<_name_##Runtime *>(shard)->core.parameters(); });
#define RUNTIME_SHARD_setParam(_name_)                                                              \
  result->setParam = static_cast<SHSetParamProc>([](Shard *shard, int index, const SHVar *value) { \
    reinterpret_cast<_name_##Runtime *>(shard)->core.setParam(index, *value);                       \
  });
#define RUNTIME_SHARD_getParam(_name_)            \
  result->getParam = static_cast<SHGetParamProc>( \
      [](Shard *shard, int index) { return reinterpret_cast<_name_##Runtime *>(shard)->core.getParam(index); });

#define RUNTIME_SHARD_compose(_name_)           \
  result->compose = static_cast<SHComposeProc>( \
      [](Shard *shard, SHInstanceData data) { return reinterpret_cast<_name_##Runtime *>(shard)->core.compose(data); });

#define RUNTIME_SHARD_warmup(_name_)          \
  result->warmup = static_cast<SHWarmupProc>( \
      [](Shard *shard, SHContext *ctx) { reinterpret_cast<_name_##Runtime *>(shard)->core.warmup(ctx); });

#define RUNTIME_SHARD_activate(_name_)                                                                       \
  result->activate = static_cast<SHActivateProc>([](Shard *shard, SHContext *context, const SHVar *input) { \
    return reinterpret_cast<_name_##Runtime *>(shard)->core.activate(context, *input);                       \
  });

#define RUNTIME_SHARD_cleanup(_name_) \
  result->cleanup = static_cast<SHCleanupProc>([](Shard *shard) { reinterpret_cast<_name_##Runtime *>(shard)->core.cleanup(); });

#define RUNTIME_SHARD_mutate(_name_)          \
  result->mutate = static_cast<SHMutateProc>( \
      [](Shard *shard, SHTable options) { reinterpret_cast<_name_##Runtime *>(shard)->core.mutate(options); });

#define RUNTIME_SHARD_END(_name_) \
  return result;                  \
  }

#define REGISTER_SHARD2(_namespace_, _name_) shards::registerShard(#_namespace_ "." #_name_, _namespace_::createShard##_name_)
#define REGISTER_CORE_SHARD(_name_) shards::registerShard(#_name_, createShard##_name_)

#endif // SH_CORE_SHARDS_MACROS