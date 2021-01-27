/* SPDX-License-Identifier: BSD 3-Clause "New" or "Revised" License */
/* Copyright © 2019-2020 Giovanni Petrantoni */

#ifndef CB_CORE_HPP
#define CB_CORE_HPP

#ifdef CB_USE_TSAN
extern "C" {
void *__tsan_get_current_fiber(void);
void *__tsan_create_fiber(unsigned flags);
void __tsan_destroy_fiber(void *fiber);
void __tsan_switch_to_fiber(void *fiber, unsigned flags);
void __tsan_set_fiber_name(void *fiber, const char *name);
const unsigned __tsan_switch_to_fiber_no_sync = 1 << 0;
}
#endif

#include "chainblocks.h"
#include "ops_internal.hpp"
#include <chainblocks.hpp>

// Included 3rdparty
#include "easylogging++.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <deque>
#include <iomanip>
#include <list>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <type_traits>
#include <unordered_set>
#include <variant>

#include "blockwrapper.hpp"

// Needed specially for win32/32bit
#include <boost/align/aligned_allocator.hpp>

// TODO make it into a run-time param
#define CB_STACK_SIZE 128 * 1024

#ifndef __EMSCRIPTEN__
// For coroutines/context switches
#include <boost/context/continuation.hpp>
typedef boost::context::continuation CBCoro;
#else
#include <emscripten/fiber.h>
struct CBCoro {
  static constexpr int stack_size = CB_STACK_SIZE;
  static constexpr int as_stack_size = 32770;

  CBCoro() {}
  ~CBCoro() {
    if (c_stack)
      ::operator delete[](c_stack, std::align_val_t{16});
  }
  void init(const std::function<void()> &func);
  NO_INLINE void resume();
  NO_INLINE void yield();

  // compatibility with boost
  operator bool() const { return true; }

  emscripten_fiber_t em_fiber;
  emscripten_fiber_t *em_parent_fiber{nullptr};
  std::function<void()> func;
  uint8_t asyncify_stack[as_stack_size];
  uint8_t *c_stack{nullptr};
};
#endif

#ifdef NDEBUG
#define CB_COMPRESSED_STRINGS 1
#endif

#ifdef CB_COMPRESSED_STRINGS
#define CBCCSTR(_str_)                                                         \
  ::chainblocks::getCompiledCompressedString(                                  \
      ::chainblocks::constant<::chainblocks::crc32(_str_)>::value)
#else
#define CBCCSTR(_str_)                                                         \
  ::chainblocks::setCompiledCompressedString(                                  \
      ::chainblocks::constant<::chainblocks::crc32(_str_)>::value, _str_)
#endif

namespace chainblocks {
#ifdef CB_COMPRESSED_STRINGS
CBOptionalString getCompiledCompressedString(uint32_t crc);
#else
CBOptionalString setCompiledCompressedString(uint32_t crc, const char *str);
#endif

CBString getString(uint32_t crc);
void setString(uint32_t crc, CBString str);
[[nodiscard]] CBComposeResult composeChain(const CBlocks chain,
                                           CBValidationCallback callback,
                                           void *userData, CBInstanceData data);
CBChainState activateBlocks(CBSeq blocks, CBContext *context,
                            const CBVar &chainInput, CBVar &output,
                            const bool handlesReturn = false);
CBChainState activateBlocks(CBlocks blocks, CBContext *context,
                            const CBVar &chainInput, CBVar &output,
                            const bool handlesReturn = false);
CBVar *referenceGlobalVariable(CBContext *ctx, const char *name);
CBVar *referenceVariable(CBContext *ctx, const char *name);
void releaseVariable(CBVar *variable);
void setSharedVariable(const char *name, const CBVar &value);
void unsetSharedVariable(const char *name);
CBVar getSharedVariable(const char *name);
CBChainState suspend(CBContext *context, double seconds);
void registerEnumType(int32_t vendorId, int32_t enumId, CBEnumInfo info);

CBlock *createBlock(std::string_view name);
void registerCoreBlocks();
void registerBlock(std::string_view name, CBBlockConstructor constructor,
                   std::string_view fullTypeName = std::string_view());
void registerObjectType(int32_t vendorId, int32_t typeId, CBObjectInfo info);
void registerEnumType(int32_t vendorId, int32_t typeId, CBEnumInfo info);
void registerRunLoopCallback(std::string_view eventName, CBCallback callback);
void unregisterRunLoopCallback(std::string_view eventName);
void registerExitCallback(std::string_view eventName, CBCallback callback);
void unregisterExitCallback(std::string_view eventName);
void callExitCallbacks();
void registerChain(CBChain *chain);
void unregisterChain(CBChain *chain);
CBVar awaitne(CBContext *context, std::function<CBVar()> func) noexcept;
void await(CBContext *context, std::function<void()> func);
struct RuntimeObserver {
  virtual void registerBlock(const char *fullName,
                             CBBlockConstructor constructor) {}
  virtual void registerObjectType(int32_t vendorId, int32_t typeId,
                                  CBObjectInfo info) {}
  virtual void registerEnumType(int32_t vendorId, int32_t typeId,
                                CBEnumInfo info) {}
};

inline void cloneVar(CBVar &dst, const CBVar &src);
inline void destroyVar(CBVar &src);

struct InternalCore;
using OwnedVar = TOwnedVar<InternalCore>;

// utility stack allocator (stolen from stackoverflow)
template <std::size_t Size = 512> struct bumping_memory_resource {
  char buffer[Size];
  std::size_t _used = 0;
  char *_ptr;

  explicit bumping_memory_resource() : _ptr(&buffer[0]) {}

  void *allocate(std::size_t size) {
    _used += size;
    if (_used > Size)
      throw chainblocks::CBException("Stack allocator memory exhausted");

    auto ret = _ptr;
    _ptr += size;
    return ret;
  }

  void deallocate(void *) noexcept {}
};

template <typename T, typename Resource = bumping_memory_resource<512>>
struct stack_allocator {
  Resource _res;
  using value_type = T;

  stack_allocator() {}

  stack_allocator(const stack_allocator &) = default;

  template <typename U>
  stack_allocator(const stack_allocator<U, Resource> &other)
      : _res(other._res) {}

  T *allocate(std::size_t n) {
    return static_cast<T *>(_res.allocate(sizeof(T) * n));
  }
  void deallocate(T *ptr, std::size_t) { _res.deallocate(ptr); }

  friend bool operator==(const stack_allocator &lhs,
                         const stack_allocator &rhs) {
    return lhs._res == rhs._res;
  }

  friend bool operator!=(const stack_allocator &lhs,
                         const stack_allocator &rhs) {
    return lhs._res != rhs._res;
  }
};

void freeDerivedInfo(CBTypeInfo info);
CBTypeInfo deriveTypeInfo(const CBVar &value);
CBTypeInfo cloneTypeInfo(const CBTypeInfo &other);

uint64_t deriveTypeHash(const CBVar &value);
uint64_t deriveTypeHash(const CBTypeInfo &value);

struct TypeInfo {
  TypeInfo() {}
  TypeInfo(const CBVar &var) { _info = deriveTypeInfo(var); }
  TypeInfo(const CBTypeInfo &info) { _info = cloneTypeInfo(info); }
  TypeInfo &operator=(const CBTypeInfo &info) {
    freeDerivedInfo(_info);
    _info = cloneTypeInfo(info);
    return *this;
  }

  ~TypeInfo() { freeDerivedInfo(_info); }

  operator const CBTypeInfo &() { return _info; }

  const CBTypeInfo *operator->() const { return &_info; }

  const CBTypeInfo &operator*() const { return _info; }

private:
  CBTypeInfo _info{};
};
} // namespace chainblocks

#ifndef __EMSCRIPTEN__
struct CBStackAllocator {
  uint8_t *mem{nullptr};

  boost::context::stack_context allocate() {
    constexpr auto size = CB_STACK_SIZE;
    boost::context::stack_context ctx;
    ctx.size = size;
    ctx.sp = mem + size;
#if defined(BOOST_USE_VALGRIND)
    ctx.valgrind_stack_id = VALGRIND_STACK_REGISTER(ctx.sp, mem);
#endif
    return ctx;
  }

  void deallocate(boost::context::stack_context &sctx) {
#if defined(BOOST_USE_VALGRIND)
    VALGRIND_STACK_DEREGISTER(sctx.valgrind_stack_id);
#endif
  }
};
#endif

struct CBChain : public std::enable_shared_from_this<CBChain> {
  static std::shared_ptr<CBChain> make(std::string_view chain_name) {
    return std::shared_ptr<CBChain>(new CBChain(chain_name));
  }

  static std::shared_ptr<CBChain> make() {
    return std::shared_ptr<CBChain>(new CBChain());
  }

  static std::shared_ptr<CBChain> *makePtr(std::string_view chain_name) {
    return new std::shared_ptr<CBChain>(new CBChain(chain_name));
  }

  enum State {
    Stopped,
    Prepared,
    Starting,
    Iterating,
    IterationEnded,
    Failed,
    Ended
  };

  ~CBChain() {
    reset();
#ifdef CB_USE_TSAN
    if (tsan_coro) {
      __tsan_destroy_fiber(tsan_coro);
    }
#endif
    LOG(TRACE) << "~CBChain() " << name;
  }

  void warmup(CBContext *context);

  void cleanup(bool force = false);

  // Also the chain takes ownership of the block!
  void addBlock(CBlock *blk) {
    assert(!blk->owned);
    blk->owned = true;
    blocks.push_back(blk);
  }

  // Also removes ownership of the block
  void removeBlock(CBlock *blk) {
    auto findIt = std::find(blocks.begin(), blocks.end(), blk);
    if (findIt != blocks.end()) {
      blocks.erase(findIt);
      blk->owned = false;
    } else {
      throw chainblocks::CBException("removeBlock: block not found!");
    }
  }

  // Attributes
  bool looped{false};
  bool unsafe{false};

  std::string name;

  std::optional<CBCoro> coro;
#ifdef CB_USE_TSAN
  void *tsan_coro{nullptr};
#endif

  std::atomic<State> state{Stopped};

  CBVar rootTickInput{};
  CBVar currentInput{};
  CBVar previousOutput{};

  chainblocks::OwnedVar finishedOutput{};
  std::string finishedError{};

  uint64_t composedHash{};
  bool warmedUp{false};
  std::unordered_set<void *> chainUsers;

  // we need to clone this, as might disappear, since outside chain
  mutable chainblocks::TypeInfo inputType{};
  // this one is a block inside the chain, so won't disappear
  mutable CBTypeInfo outputType{};
  mutable std::vector<std::string> requiredVariables;

  CBContext *context{nullptr};
  CBChain *resumer{nullptr}; // used in Resume/Start blocks

  std::weak_ptr<CBNode> node;

  std::vector<CBlock *> blocks;

  std::unordered_map<std::string, CBVar, std::hash<std::string>,
                     std::equal_to<std::string>,
                     boost::alignment::aligned_allocator<
                         std::pair<const std::string, CBVar>, 16>>
      variables;

  // this is the eventual coroutine stack memory buffer
  uint8_t *stack_mem{nullptr};

  static std::shared_ptr<CBChain> sharedFromRef(CBChainRef ref) {
    return *reinterpret_cast<std::shared_ptr<CBChain> *>(ref);
  }

  static void deleteRef(CBChainRef ref) {
    auto pref = reinterpret_cast<std::shared_ptr<CBChain> *>(ref);
    LOG(TRACE) << (*pref)->name
               << " chain deleteRef - use_count: " << pref->use_count();
    delete pref;
  }

  CBChainRef newRef() {
    return reinterpret_cast<CBChainRef>(
        new std::shared_ptr<CBChain>(shared_from_this()));
  }

  static CBChainRef weakRef(const std::shared_ptr<CBChain> &shared) {
    return reinterpret_cast<CBChainRef>(
        &const_cast<std::shared_ptr<CBChain> &>(shared));
  }

  static CBChainRef addRef(CBChainRef ref) {
    auto cref = sharedFromRef(ref);
    LOG(TRACE) << cref->name
               << " chain addRef - use_count: " << cref.use_count();
    auto res = new std::shared_ptr<CBChain>(cref);
    return reinterpret_cast<CBChainRef>(res);
  }

  std::vector<std::function<void()>> onStart;
  std::vector<std::function<void()>> onStop;

private:
  CBChain(std::string_view chain_name) : name(chain_name) {
    LOG(TRACE) << "CBChain() " << name;
  }

  CBChain() { LOG(TRACE) << "CBChain() (no name)"; }

  void reset();
};

namespace chainblocks {
using CBSet =
    std::unordered_set<OwnedVar, std::hash<CBVar>, std::equal_to<CBVar>,
                       boost::alignment::aligned_allocator<OwnedVar, 16>>;
using CBSetIt = CBSet::iterator;

using CBMap = std::unordered_map<
    std::string, OwnedVar, std::hash<std::string>, std::equal_to<std::string>,
    boost::alignment::aligned_allocator<std::pair<const std::string, OwnedVar>,
                                        16>>;
using CBMapIt = CBMap::iterator;

struct Globals {
  static inline int SigIntTerm{0};
  static inline std::unordered_map<std::string_view, CBBlockConstructor>
      BlocksRegister;
  static inline std::unordered_map<std::string_view, std::string_view>
      BlockNamesToFullTypeNames;
  static inline std::unordered_map<int64_t, CBObjectInfo> ObjectTypesRegister;
  static inline std::unordered_map<int64_t, CBEnumInfo> EnumTypesRegister;

  // map = ordered! we need that for those
  static inline std::map<std::string_view, CBCallback> RunLoopHooks;
  static inline std::map<std::string_view, CBCallback> ExitHooks;

  static inline std::unordered_map<std::string, std::shared_ptr<CBChain>>
      GlobalChains;

  static inline std::list<std::weak_ptr<RuntimeObserver>> Observers;

  static inline std::string RootPath;
  static inline std::string ExePath;

  static inline std::unordered_map<uint32_t, CBOptionalString>
      *CompressedStrings{nullptr};

  static inline CBTableInterface TableInterface{
      .tableForEach =
          [](CBTable table, CBTableForEachCallback cb, void *data) {
            chainblocks::CBMap *map =
                reinterpret_cast<chainblocks::CBMap *>(table.opaque);
            for (auto &entry : *map) {
              if (!cb(entry.first.c_str(), &entry.second, data))
                break;
            }
          },
      .tableGetIterator =
          [](CBTable table, CBTableIterator *outIter) {
            if (outIter == nullptr)
              LOG(FATAL) << "tableGetIterator - outIter was nullptr";
            chainblocks::CBMap *map =
                reinterpret_cast<chainblocks::CBMap *>(table.opaque);
            chainblocks::CBMapIt *mapIt =
                reinterpret_cast<chainblocks::CBMapIt *>(outIter);
            *mapIt = map->begin();
          },
      .tableNext =
          [](CBTable table, CBTableIterator *inIter, CBString *outKey,
             CBVar *outVar) {
            if (inIter == nullptr)
              LOG(FATAL) << "tableGetIterator - outIter was nullptr";
            chainblocks::CBMap *map =
                reinterpret_cast<chainblocks::CBMap *>(table.opaque);
            chainblocks::CBMapIt *mapIt =
                reinterpret_cast<chainblocks::CBMapIt *>(inIter);
            if ((*mapIt) != map->end()) {
              *outKey = (*(*mapIt)).first.c_str();
              *outVar = (*(*mapIt)).second;
              (*mapIt)++;
              return true;
            } else {
              return false;
            }
          },
      .tableSize =
          [](CBTable table) {
            chainblocks::CBMap *map =
                reinterpret_cast<chainblocks::CBMap *>(table.opaque);
            return map->size();
          },
      .tableContains =
          [](CBTable table, const char *key) {
            chainblocks::CBMap *map =
                reinterpret_cast<chainblocks::CBMap *>(table.opaque);
            return map->count(key) > 0;
          },
      .tableAt =
          [](CBTable table, const char *key) {
            chainblocks::CBMap *map =
                reinterpret_cast<chainblocks::CBMap *>(table.opaque);
            CBVar &vref = (*map)[key];
            return &vref;
          },
      .tableRemove =
          [](CBTable table, const char *key) {
            chainblocks::CBMap *map =
                reinterpret_cast<chainblocks::CBMap *>(table.opaque);
            map->erase(key);
          },
      .tableClear =
          [](CBTable table) {
            chainblocks::CBMap *map =
                reinterpret_cast<chainblocks::CBMap *>(table.opaque);
            map->clear();
          },
      .tableFree =
          [](CBTable table) {
            chainblocks::CBMap *map =
                reinterpret_cast<chainblocks::CBMap *>(table.opaque);
            delete map;
          }};
};

template <typename T>
NO_INLINE void arrayGrow(T &arr, size_t addlen, size_t min_cap = 4);

template <typename T, typename V> inline void arrayPush(T &arr, const V &val) {
  if ((arr.len + 1) > arr.cap) {
    arrayGrow(arr, 1);
  }
  arr.elements[arr.len++] = val;
}

template <typename T> inline void arrayResize(T &arr, uint32_t size) {
  if (arr.cap < size) {
    arrayGrow(arr, size - arr.len);
  }
  arr.len = size;
}

template <typename T, typename V>
inline void arrayInsert(T &arr, uint32_t index, const V &val) {
  if ((arr.len + 1) > arr.cap) {
    arrayGrow(arr, 1);
  }
  memmove(&arr.elements[index + 1], &arr.elements[index],
          sizeof(V) * (arr.len - index));
  arr.len++;
  arr.elements[index] = val;
}

template <typename T, typename V> inline V arrayPop(T &arr) {
  assert(arr.len > 0);
  arr.len--;
  return arr.elements[arr.len];
}

template <typename T> inline void arrayDelFast(T &arr, uint32_t index) {
  assert(arr.len > 0);
  arr.len--;
  // this allows eventual destroyVar/cloneVar magic
  // avoiding allocations even in nested seqs
  std::swap(arr.elements[index], arr.elements[arr.len]);
}

template <typename T> inline void arrayDel(T &arr, uint32_t index) {
  assert(arr.len > 0);
  // this allows eventual destroyVar/cloneVar magic
  // avoiding allocations even in nested seqs
  arr.len--;
  while (index < arr.len) {
    std::swap(arr.elements[index], arr.elements[index + 1]);
    index++;
  }
}

template <typename T> inline void arrayFree(T &arr) {
  if (arr.elements)
    ::operator delete[](arr.elements, std::align_val_t{16});
  memset(&arr, 0x0, sizeof(T));
}

template <typename T> class PtrIterator {
public:
  typedef T value_type;

  PtrIterator(T *ptr) : ptr(ptr) {}

  PtrIterator &operator+=(std::ptrdiff_t s) {
    ptr += s;
    return *this;
  }
  PtrIterator &operator-=(std::ptrdiff_t s) {
    ptr -= s;
    return *this;
  }

  PtrIterator operator+(std::ptrdiff_t s) {
    PtrIterator it(ptr);
    return it += s;
  }
  PtrIterator operator-(std::ptrdiff_t s) {
    PtrIterator it(ptr);
    return it -= s;
  }

  PtrIterator operator++() {
    ++ptr;
    return *this;
  }
  PtrIterator operator++(int s) {
    ptr += s;
    return *this;
  }

  PtrIterator operator--() {
    --ptr;
    return *this;
  }
  PtrIterator operator--(int s) {
    ptr -= s;
    return *this;
  }

  std::ptrdiff_t operator-(PtrIterator const &other) const {
    return ptr - other.ptr;
  }

  T &operator*() const { return *ptr; }

  bool operator==(const PtrIterator &rhs) const { return ptr == rhs.ptr; }
  bool operator!=(const PtrIterator &rhs) const { return ptr != rhs.ptr; }
  bool operator>(const PtrIterator &rhs) const { return ptr > rhs.ptr; }
  bool operator<(const PtrIterator &rhs) const { return ptr < rhs.ptr; }
  bool operator>=(const PtrIterator &rhs) const { return ptr >= rhs.ptr; }
  bool operator<=(const PtrIterator &rhs) const { return ptr <= rhs.ptr; }

private:
  T *ptr;
};

template <typename S, typename T, typename Allocator = std::allocator<T>>
class IterableArray {
public:
  typedef S seq_type;
  typedef T value_type;
  typedef size_t size_type;

  typedef PtrIterator<T> iterator;
  typedef PtrIterator<const T> const_iterator;

  IterableArray() : _seq({}), _owned(true) {}

  // Not OWNING!
  IterableArray(const seq_type &seq) : _seq(seq), _owned(false) {}

  // implicit converter
  IterableArray(const CBVar &v) : _seq(v.payload.seqValue), _owned(false) {
    assert(v.valueType == Seq);
  }

  IterableArray(size_t s) : _seq({}), _owned(true) { arrayResize(_seq, s); }

  IterableArray(size_t s, T v) : _seq({}), _owned(true) {
    arrayResize(_seq, s);
    for (size_t i = 0; i < s; i++) {
      _seq[i] = v;
    }
  }

  IterableArray(iterator first, iterator last) : _seq({}), _owned(true) {
    size_t size = last - first;
    arrayResize(_seq, size);
    for (size_t i = 0; i < size; i++) {
      _seq.elements[i] = *(first + i);
    }
  }

  IterableArray(const_iterator first, const_iterator last)
      : _seq({}), _owned(true) {
    size_t size = last - first;
    arrayResize(_seq, size);
    for (size_t i = 0; i < size; i++) {
      _seq.elements[i] = *(first + i);
    }
  }

  IterableArray(const IterableArray &other) : _seq({}), _owned(true) {
    size_t size = other._seq.len;
    arrayResize(_seq, size);
    for (size_t i = 0; i < size; i++) {
      _seq.elements[i] = other._seq.elements[i];
    }
  }

  IterableArray &operator=(IterableArray &other) {
    std::swap(_seq, other._seq);
    std::swap(_owned, other._owned);
    return *this;
  }

  IterableArray &operator=(const IterableArray &other) {
    _seq = {};
    _owned = true;
    size_t size = other._seq.len;
    arrayResize(_seq, size);
    for (size_t i = 0; i < size; i++) {
      _seq.elements[i] = other._seq.elements[i];
    }
    return *this;
  }

  ~IterableArray() {
    if (_owned) {
      arrayFree(_seq);
    }
  }

private:
  seq_type _seq;
  bool _owned;

public:
  iterator begin() { return iterator(&_seq.elements[0]); }
  const_iterator begin() const { return const_iterator(&_seq.elements[0]); }
  const_iterator cbegin() const { return const_iterator(&_seq.elements[0]); }
  iterator end() { return iterator(&_seq.elements[0] + size()); }
  const_iterator end() const {
    return const_iterator(&_seq.elements[0] + size());
  }
  const_iterator cend() const {
    return const_iterator(&_seq.elements[0] + size());
  }
  T &operator[](int index) { return _seq.elements[index]; }
  const T &operator[](int index) const { return _seq.elements[index]; }
  T &front() { return _seq.elements[0]; }
  const T &front() const { return _seq.elements[0]; }
  T &back() { return _seq.elements[size() - 1]; }
  const T &back() const { return _seq.elements[size() - 1]; }
  T *data() { return _seq; }
  size_t size() const { return _seq.len; }
  bool empty() const { return _seq.elements == nullptr || size() == 0; }
  void resize(size_t nsize) { arrayResize(_seq, nsize); }
  void push_back(const T &value) { arrayPush(_seq, value); }
  void clear() { arrayResize(_seq, 0); }
  operator seq_type() const { return _seq; }
};

using IterableSeq = IterableArray<CBSeq, CBVar>;
using IterableExposedInfo =
    IterableArray<CBExposedTypesInfo, CBExposedTypeInfo>;
using IterableTypesInfo = IterableArray<CBTypesInfo, CBTypeInfo>;
} // namespace chainblocks

// needed by some c++ library
namespace std {
template <typename T> class iterator_traits<chainblocks::PtrIterator<T>> {
public:
  using difference_type = ptrdiff_t;
  using size_type = size_t;
  using value_type = T;
  using pointer = T *;
  using reference = T &;
  using iterator_category = random_access_iterator_tag;
};

inline void swap(CBVar &v1, CBVar &v2) {
  auto t = v1;
  v1 = v2;
  v2 = t;
}
} // namespace std

namespace chainblocks {
NO_INLINE void _destroyVarSlow(CBVar &var);
NO_INLINE void _cloneVarSlow(CBVar &dst, const CBVar &src);

ALWAYS_INLINE inline void destroyVar(CBVar &var) {
  switch (var.valueType) {
  case Table:
  case Seq:
    _destroyVarSlow(var);
    break;
  case CBType::Path:
  case CBType::String:
  case ContextVar:
    delete[] var.payload.stringValue;
    break;
  case Image:
    delete[] var.payload.imageValue.data;
    break;
  case Bytes:
    delete[] var.payload.bytesValue;
    break;
  case CBType::Array:
    arrayFree(var.payload.arrayValue);
    break;
  case Object:
    if ((var.flags & CBVAR_FLAGS_USES_OBJINFO) == CBVAR_FLAGS_USES_OBJINFO &&
        var.objectInfo && var.objectInfo->release) {
      // in this case the custom object needs actual destruction
      var.objectInfo->release(var.payload.objectValue);
    }
    break;
  case CBType::Chain:
    CBChain::deleteRef(var.payload.chainValue);
    break;
  default:
    break;
  };

  memset(&var.payload, 0x0, sizeof(CBVarPayload));
  var.valueType = CBType::None;
}

ALWAYS_INLINE inline void cloneVar(CBVar &dst, const CBVar &src) {
  if (src.valueType < EndOfBlittableTypes &&
      dst.valueType < EndOfBlittableTypes) {
    dst.valueType = src.valueType;
    memcpy(&dst.payload, &src.payload, sizeof(CBVarPayload));
  } else if (src.valueType < EndOfBlittableTypes) {
    destroyVar(dst);
    dst.valueType = src.valueType;
    memcpy(&dst.payload, &src.payload, sizeof(CBVarPayload));
  } else {
    _cloneVarSlow(dst, src);
  }
}

struct InternalCore {
  // need to emulate dllblock Core a bit
  static CBVar *referenceVariable(CBContext *context, const char *name) {
    return chainblocks::referenceVariable(context, name);
  }

  static void releaseVariable(CBVar *variable) {
    chainblocks::releaseVariable(variable);
  }

  static void cloneVar(CBVar &dst, const CBVar &src) {
    chainblocks::cloneVar(dst, src);
  }

  static void destroyVar(CBVar &var) { chainblocks::destroyVar(var); }

  template <typename T> static void arrayFree(T &arr) {
    chainblocks::arrayFree<T>(arr);
  }

  static void expTypesFree(CBExposedTypesInfo &arr) { arrayFree(arr); }

  static void log(const char *msg) { LOG(INFO) << msg; }

  static void registerEnumType(int32_t vendorId, int32_t enumId,
                               CBEnumInfo info) {
    chainblocks::registerEnumType(vendorId, enumId, info);
  }

  static void registerObjectType(int32_t vendorId, int32_t objectId,
                                 CBObjectInfo info) {
    chainblocks::registerObjectType(vendorId, objectId, info);
  }

  static CBComposeResult composeBlocks(CBlocks blocks,
                                       CBValidationCallback callback,
                                       void *userData, CBInstanceData data) {
    return chainblocks::composeChain(blocks, callback, userData, data);
  }

  static CBChainState runBlocks(CBlocks blocks, CBContext *context,
                                const CBVar &input, CBVar &output,
                                const CBBool handleReturn) {
    return chainblocks::activateBlocks(blocks, context, input, output,
                                       handleReturn);
  }

  static CBChainState suspend(CBContext *ctx, double seconds) {
    return chainblocks::suspend(ctx, seconds);
  }
};

typedef TParamVar<InternalCore> ParamVar;

template <Parameters &Params, size_t NPARAMS, Type &InputType, Type &OutputType>
struct SimpleBlock : public TSimpleBlock<InternalCore, Params, NPARAMS,
                                         InputType, OutputType> {};

template <typename E> class EnumInfo : public TEnumInfo<InternalCore, E> {
public:
  EnumInfo(const char *name, int32_t vendorId, int32_t enumId)
      : TEnumInfo<InternalCore, E>(name, vendorId, enumId) {}
};

template <typename E, std::vector<uint8_t> (*Serializer)(const E &) = nullptr,
          E (*Deserializer)(const std::string_view &) = nullptr>
class ObjectVar : public TObjectVar<InternalCore, E, Serializer, Deserializer> {
public:
  ObjectVar(const char *name, int32_t vendorId, int32_t objectId)
      : TObjectVar<InternalCore, E, Serializer, Deserializer>(name, vendorId,
                                                              objectId) {}
};

typedef TBlocksVar<InternalCore> BlocksVar;

struct ParamsInfo {
  /*
   DO NOT USE ANYMORE, THIS IS DEPRECATED AND MESSY
   IT WAS USEFUL WHEN DYNAMIC ARRAYS WERE STB ARRAYS
   BUT NOW WE CAN JUST USE DESIGNATED/AGGREGATE INITIALIZERS
 */
  ParamsInfo(const ParamsInfo &other) {
    chainblocks::arrayResize(_innerInfo, 0);
    for (uint32_t i = 0; i < other._innerInfo.len; i++) {
      chainblocks::arrayPush(_innerInfo, other._innerInfo.elements[i]);
    }
  }

  ParamsInfo &operator=(const ParamsInfo &other) {
    _innerInfo = {};
    for (uint32_t i = 0; i < other._innerInfo.len; i++) {
      chainblocks::arrayPush(_innerInfo, other._innerInfo.elements[i]);
    }
    return *this;
  }

  template <typename... Types>
  explicit ParamsInfo(CBParameterInfo first, Types... types) {
    std::vector<CBParameterInfo> vec = {first, types...};
    _innerInfo = {};
    for (auto pi : vec) {
      chainblocks::arrayPush(_innerInfo, pi);
    }
  }

  template <typename... Types>
  explicit ParamsInfo(const ParamsInfo &other, CBParameterInfo first,
                      Types... types) {
    _innerInfo = {};

    for (uint32_t i = 0; i < other._innerInfo.len; i++) {
      chainblocks::arrayPush(_innerInfo, other._innerInfo.elements[i]);
    }

    std::vector<CBParameterInfo> vec = {first, types...};
    for (auto pi : vec) {
      chainblocks::arrayPush(_innerInfo, pi);
    }
  }

  static CBParameterInfo Param(CBString name, CBOptionalString help,
                               CBTypesInfo types) {
    CBParameterInfo res = {name, help, types};
    return res;
  }

  ~ParamsInfo() {
    if (_innerInfo.elements)
      chainblocks::arrayFree(_innerInfo);
  }

  explicit operator CBParametersInfo() const { return _innerInfo; }

  CBParametersInfo _innerInfo{};
};

struct ExposedInfo {
  /*
   DO NOT USE ANYMORE, THIS IS DEPRECATED AND MESSY
   IT WAS USEFUL WHEN DYNAMIC ARRAYS WERE STB ARRAYS
   BUT NOW WE CAN JUST USE DESIGNATED/AGGREGATE INITIALIZERS
 */
  ExposedInfo() { _innerInfo = {}; }

  ExposedInfo(const ExposedInfo &other) {
    _innerInfo = {};
    for (uint32_t i = 0; i < other._innerInfo.len; i++) {
      chainblocks::arrayPush(_innerInfo, other._innerInfo.elements[i]);
    }
  }

  ExposedInfo &operator=(const ExposedInfo &other) {
    chainblocks::arrayResize(_innerInfo, 0);
    for (uint32_t i = 0; i < other._innerInfo.len; i++) {
      chainblocks::arrayPush(_innerInfo, other._innerInfo.elements[i]);
    }
    return *this;
  }

  template <typename... Types>
  explicit ExposedInfo(const ExposedInfo &other, Types... types) {
    _innerInfo = {};

    for (uint32_t i = 0; i < other._innerInfo.len; i++) {
      chainblocks::arrayPush(_innerInfo, other._innerInfo.elements[i]);
    }

    std::vector<CBExposedTypeInfo> vec = {types...};
    for (auto pi : vec) {
      chainblocks::arrayPush(_innerInfo, pi);
    }
  }

  template <typename... Types>
  explicit ExposedInfo(const CBExposedTypesInfo other, Types... types) {
    _innerInfo = {};

    for (uint32_t i = 0; i < other.len; i++) {
      chainblocks::arrayPush(_innerInfo, other.elements[i]);
    }

    std::vector<CBExposedTypeInfo> vec = {types...};
    for (auto pi : vec) {
      chainblocks::arrayPush(_innerInfo, pi);
    }
  }

  template <typename... Types>
  explicit ExposedInfo(const CBExposedTypeInfo first, Types... types) {
    std::vector<CBExposedTypeInfo> vec = {first, types...};
    _innerInfo = {};
    for (auto pi : vec) {
      chainblocks::arrayPush(_innerInfo, pi);
    }
  }

  constexpr static CBExposedTypeInfo
  Variable(CBString name, CBOptionalString help, CBTypeInfo type,
           bool isMutable = false, bool isTableField = false) {
    CBExposedTypeInfo res = {name,  help,         type, isMutable,
                             false, isTableField, false};
    return res;
  }

  constexpr static CBExposedTypeInfo ProtectedVariable(CBString name,
                                                       CBOptionalString help,
                                                       CBTypeInfo type,
                                                       bool isMutable = false) {
    CBExposedTypeInfo res = {name, help, type, isMutable, true, false, false};
    return res;
  }

  constexpr static CBExposedTypeInfo
  GlobalVariable(CBString name, CBOptionalString help, CBTypeInfo type,
                 bool isMutable = false, bool isTableField = false) {
    CBExposedTypeInfo res = {name,  help,         type, isMutable,
                             false, isTableField, true};
    return res;
  }

  ~ExposedInfo() {
    if (_innerInfo.elements)
      chainblocks::arrayFree(_innerInfo);
  }

  explicit operator CBExposedTypesInfo() const { return _innerInfo; }

  CBExposedTypesInfo _innerInfo;
};

struct CachedStreamBuf : std::streambuf {
  std::vector<char> data;

  void reset() { data.clear(); }

  std::streamsize xsputn(const char *s, std::streamsize n) override {
    data.insert(data.end(), &s[0], s + n);
    return n;
  }

  int overflow(int c) override {
    data.push_back(static_cast<char>(c));
    return 0;
  }

  void done() { data.push_back('\0'); }

  const char *str() { return &data[0]; }
};

struct StringStreamBuf : std::streambuf {
  StringStreamBuf(const std::string_view &s) : data(s) {}

  std::string_view data;
  uint32_t index{0};
  bool done{false};

  std::streamsize xsgetn(char *s, std::streamsize n) override {
    if (unlikely(done)) {
      return 0;
    } else if ((size_t(index) + n) > data.size()) {
      const auto len = data.size() - index;
      memcpy(s, &data[index], len);
      done = true; // flag to indicate we are done
      return len;
    } else {
      memcpy(s, &data[index], n);
      index += n;
      return n;
    }
  }

  int underflow() override {
    if (index >= data.size())
      return EOF;

    return data[index++];
  }
};

struct VarStringStream {
  CachedStreamBuf cache;
  CBVar previousValue{};

  ~VarStringStream() { destroyVar(previousValue); }

  void write(const CBVar &var) {
    if (var != previousValue) {
      cache.reset();
      std::ostream stream(&cache);
      stream << var;
      cache.done();
      cloneVar(previousValue, var);
    }
  }

  void tryWriteHex(const CBVar &var) {
    if (var != previousValue) {
      cache.reset();
      std::ostream stream(&cache);
      if (var.valueType == Int) {
        stream << "0x" << std::hex << var.payload.intValue << std::dec;
      } else if (var.valueType == Bytes) {
        stream << std::hex;
        for (uint32_t i = 0; i < var.payload.bytesSize; i++)
          stream << std::setw(2) << std::setfill('0')
                 << (int)var.payload.bytesValue[i];
      } else {
        stream << var;
      }
      cache.done();
      cloneVar(previousValue, var);
    }
  }

  const char *str() { return cache.str(); }
};

using BlocksCollection =
    std::variant<const CBChain *, CBlockPtr, CBlocks, CBVar>;

struct CBlockInfo {
  CBlockInfo(const std::string_view &name, const CBlock *block)
      : name(name), block(block) {}
  std::string_view name;
  const CBlock *block;
};

void gatherBlocks(const BlocksCollection &coll, std::vector<CBlockInfo> &out);
}; // namespace chainblocks

#endif
