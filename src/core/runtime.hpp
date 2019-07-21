#pragma once

#ifdef CHAINBLOCKS_RUNTIME

// ONLY CLANG AND GCC SUPPORTED FOR NOW

#if defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wtype-limits"
#pragma GCC diagnostic ignored "-Wunused-function"
#elif defined(__clang__)
#pragma clang diagnostic ignored "-Wtype-limits"
#pragma clang diagnostic ignored "-Wunused-function"
#endif

#include <string.h> // memset

#include "chainblocks.hpp"
// C++ Mandatory from now!

// Stub inline blocks, actually implemented in respective nim code!
struct CBConstStub
{
  CBRuntimeBlock header;
  CBVar constValue;
};
struct CBSleepStub
{
  CBRuntimeBlock header;
  double sleepTime;
};
struct CBMathStub
{
  CBRuntimeBlock header;
  CBVar operand;
  CBVar* ctxOperand;
  CBSeq seqCache;
};
struct CBMathUnaryStub
{
  CBRuntimeBlock header;
  CBSeq seqCache;
};
struct CBCoreRepeat
{
  CBRuntimeBlock header;
  bool doForever;
  int32_t times;
  CBSeq blocks;
};
struct CBCoreIf
{
  CBRuntimeBlock header;
  uint8_t boolOp;
  CBVar match;
  CBVar* matchCtx;
  CBSeq trueBlocks;
  CBSeq falseBlocks;
  bool passthrough;
};
struct CBCoreSetVariable
{
  // Also Get and Add
  CBRuntimeBlock header;
  CBVar* target;
};
struct CBCoreSwapVariables
{
  CBRuntimeBlock header;
  CBVar* target1;
  CBVar* target2;
};

// Since we build the runtime we are free to use any std and lib
#include <vector>
#include <string>
#include <memory>
#include <iostream>
#include <chrono>
#include <atomic>
#include <map>
#include <list>
using Clock = std::chrono::high_resolution_clock;
using Duration = std::chrono::duration<double>;
using Time = std::chrono::time_point<Clock, Duration>;

// Required external dependencies
// For coroutines/context switches
#include <boost/context/continuation.hpp>
// For sleep
#if _WIN32
#include <Windows.h>
#else
#include <time.h>
#endif

// Included 3rdparty
#include "3rdparty/json.hpp"
#include "3rdparty/parallel_hashmap/phmap.h"

#include <tuple>
// Tuple hashing
namespace std
{
  namespace
  {

    // Code from boost
    // Reciprocal of the golden ratio helps spread entropy
    //     and handles duplicates.
    // See Mike Seymour in magic-numbers-in-boosthash-combine:
    //     http://stackoverflow.com/questions/4948780

    template <class T>
    inline void hash_combine(std::size_t& seed, T const& v)
    {
        seed ^= std::hash<T>()(v) + 0x9e3779b9 + (seed<<6) + (seed>>2);
    }

    // Recursive template code derived from Matthieu M.
    template <class Tuple, size_t Index = std::tuple_size<Tuple>::value - 1>
    struct HashValueImpl
    {
      static void apply(size_t& seed, Tuple const& tuple)
      {
        HashValueImpl<Tuple, Index-1>::apply(seed, tuple);
        hash_combine(seed, std::get<Index>(tuple));
      }
    };

    template <class Tuple>
    struct HashValueImpl<Tuple,0>
    {
      static void apply(size_t& seed, Tuple const& tuple)
      {
        hash_combine(seed, std::get<0>(tuple));
      }
    };
  }

  template <typename ... TT>
  struct hash<std::tuple<TT...>> 
  {
    size_t
    operator()(std::tuple<TT...> const& tt) const
    {                                              
      size_t seed = 0;                             
      HashValueImpl<std::tuple<TT...> >::apply(seed, tt);    
      return seed;                                 
    }                                              
  };
}

typedef boost::context::continuation CBCoro;

struct CBContext
{
  CBContext(CBCoro&& sink) : stack(nullptr), restarted(false), aborted(false), continuation(std::move(sink))
  {
  }

  phmap::node_hash_map<std::string, CBVar> variables;
  CBSeq stack;
  std::string error;

  // Those 2 go together with CBVar chainstates restart and stop
  bool restarted;
  // Also used to cancel a chain
  bool aborted;

  // Used within the coro stack! (suspend, etc)
  CBCoro&& continuation;

  void setError(const char* errorMsg)
  {
    error = errorMsg;
  }
};

struct CBChain
{
  CBChain(const char* chain_name) : 
    name(chain_name),
    coro(nullptr),
    next(0),
    started(false),
    finished(false),
    returned(false),
    rootTickInput(CBVar()),
    finishedOutput(CBVar()),
    context(nullptr),
    node(nullptr)
  {}

  ~CBChain()
  {
    cleanup();
  }

  void cleanup()
  {
    for(auto blk : blocks)
    {
      blk->cleanup(blk);
      blk->destroy(blk);
      //blk is responsible to free itself, as they might use any allocation strategy they wish!
    }
    blocks.clear();
  }

  std::string name;

  CBCoro* coro;
  Duration next;
  
  // we could simply null check coro but actually some chains (sub chains), will run without a coro within the root coro so we need this too
  bool started;
  
  // this gets cleared before every runChain and set after every runChain
  std::atomic_bool finished;
  
  // when running as coro if actually the coro lambda exited
  bool returned;

  CBVar rootTickInput;
  CBVar finishedOutput;
  
  CBContext* context;
  CBNode* node;
  std::vector<CBRuntimeBlock*> blocks;

  // Also the chain takes ownership of the block!
  void addBlock(CBRuntimeBlock* blk)
  {
    blocks.push_back(blk);
  }

  // Also removes ownership of the block
  void removeBlock(CBRuntimeBlock* blk)
  {
    auto findIt = std::find(blocks.begin(), blocks.end(), blk);
    if(findIt != blocks.end())
    {
      blocks.erase(findIt);
    }
  }
};

namespace chainblocks
{
  extern phmap::node_hash_map<std::string, CBBlockConstructor> BlocksRegister;
  extern phmap::node_hash_map<std::tuple<int32_t, int32_t>, CBObjectInfo> ObjectTypesRegister;
  extern phmap::node_hash_map<std::tuple<int32_t, int32_t>, CBEnumInfo> EnumTypesRegister;
  extern phmap::node_hash_map<std::string, CBVar> GlobalVariables;
  extern std::map<std::string, CBCallback> RunLoopHooks;
  extern phmap::node_hash_map<std::string, CBCallback> ExitHooks;
  extern phmap::node_hash_map<std::string, CBChain*> GlobalChains;
  extern thread_local CBChain* CurrentChain;

  static CBRuntimeBlock* createBlock(const char* name);

  class CBException : public std::exception
  {
    public:
      CBException(const char* errmsg) : errorMessage(errmsg) {}

      const char * what () const noexcept
      {
        return errorMessage;
      }

    private:
      const char* errorMessage;
  };
};

using json = nlohmann::json;
// The following procedures implement json.hpp protocol in order to allow easy integration! they must stay outside the namespace!
void to_json(json& j, const CBVar& var);
void from_json(const json& j, CBVar& var);
void to_json(json& j, const CBChainPtr& chain);
void from_json(const json& j, CBChainPtr& chain);

namespace chainblocks
{
  static int destroyVar(CBVar& var)
  {
    int freeCount = 0;
    switch(var.valueType)
    {
      case Seq:
      {
        // Notice we use capacity rather then len!
        // Assuming memory is nicely memset
        // We do that because we try our best to recycle memory
        int len = stbds_arrcap(var.payload.seqValue);
        for(int i = 0; i < len; i++)
        {
          freeCount += destroyVar(var.payload.seqValue[i]);
        }
        stbds_arrfree(var.payload.seqValue);
        freeCount++;
      }
      break;
      case String:
      case ContextVar:
      {
        delete[] var.payload.stringValue;
        freeCount++;
      }
      break;
      case Image:
      {
        delete[] var.payload.imageValue.data;
        freeCount++;
      }
      break;
      case Table:
      {
        auto len = stbds_shlen(var.payload.tableValue);
        for(auto i = 0; i < len; i++)
        {
          freeCount += destroyVar(var.payload.tableValue[i].value);
        }
        stbds_shfree(var.payload.tableValue);
        freeCount++;
      }
      break;
      default:
      break;
    };
    
    memset((void*)&var, 0x0, sizeof(CBVar));

    return freeCount;
  }
  
  static int cloneVar(CBVar& dst, const CBVar& src)
  {
    int freeCount = 0;
    if(src.valueType < EndOfBlittableTypes && src.valueType < EndOfBlittableTypes)
    {
      memcpy((void*)&dst, (const void*)&src, sizeof(CBVar));
    }
    else if(src.valueType < EndOfBlittableTypes)
    {
      freeCount += destroyVar(dst);
      memcpy((void*)&dst, (const void*)&src, sizeof(CBVar));
    }
    else
    {
      switch(src.valueType)
      {
        case Seq:
        {
          int srcLen = stbds_arrlen(src.payload.seqValue);
          // reuse if seq and we got enough capacity
          if(dst.valueType != Seq || (int)stbds_arrcap(dst.payload.seqValue) < srcLen)
          {
            freeCount += destroyVar(dst);
            dst.valueType = Seq;
            dst.payload.seqLen = -1;
            dst.payload.seqValue = nullptr;
          }
          
          stbds_arrsetlen(dst.payload.seqValue, (unsigned)srcLen);
          for(auto i = 0; i < srcLen; i++)
          {
            auto& subsrc = src.payload.seqValue[i];
            freeCount += cloneVar(dst.payload.seqValue[i], subsrc);
          }
        }
        break;
        case String:
        case ContextVar:
        {
          auto srcLen = strlen(src.payload.stringValue);
          if((dst.valueType != String && dst.valueType != ContextVar) || strlen(dst.payload.stringValue) < srcLen)
          {
            freeCount += destroyVar(dst);
            dst.valueType = String;
            dst.payload.stringValue = new char[srcLen + 1];
          }
          
          dst.valueType = src.valueType;
          strcpy((char*)dst.payload.stringValue, (char*)src.payload.stringValue);
        }
        break;
        case Image:
        {
          auto srcImgSize = src.payload.imageValue.height * src.payload.imageValue.width * src.payload.imageValue.channels;
          auto dstImgSize = dst.payload.imageValue.height * dst.payload.imageValue.width * dst.payload.imageValue.channels;
          if(dst.valueType != Image || srcImgSize > dstImgSize)
          {
            freeCount += destroyVar(dst);
            dst.valueType = Image;
            dst.payload.imageValue.height = src.payload.imageValue.height;
            dst.payload.imageValue.width = src.payload.imageValue.width;
            dst.payload.imageValue.channels = src.payload.imageValue.channels;
            dst.payload.imageValue.data = new uint8_t[dstImgSize];
          }
          
          memcpy(dst.payload.imageValue.data, src.payload.imageValue.data, srcImgSize);
        }
        break;
        case Table:
        {
          // Slowest case, it's a full copy using arena tho
          freeCount += destroyVar(dst);
          dst.valueType = Table;
          dst.payload.tableLen = -1;
          dst.payload.tableValue = nullptr;
          stbds_sh_new_arena(dst.payload.tableValue);
          auto srcLen = stbds_shlen(src.payload.tableValue);
          for(auto i = 0; i < srcLen; i++)
          {
            CBVar clone;
            freeCount += cloneVar(clone, src.payload.tableValue[i].value);
            stbds_shput(dst.payload.tableValue, src.payload.tableValue[i].key, clone);
          }
        }
        break;
        default:
        break;
      };
    }
    return freeCount;
  }

  static void setCurrentChain(CBChain* chain)
  {
    CurrentChain = chain;
  }

  static CBChain* getCurrentChain()
  {
    return CurrentChain;
  }

  static void registerChain(CBChain* chain)
  {
    chainblocks::GlobalChains[chain->name] = chain;
  }

  static void unregisterChain(CBChain* chain)
  {
    auto findIt = chainblocks::GlobalChains.find(chain->name);
    if(findIt != chainblocks::GlobalChains.end())
    {
      chainblocks::GlobalChains.erase(findIt);
    }
  }

  static void registerBlock(const char* fullName, CBBlockConstructor constructor)
  {
    auto cname = std::string(fullName);
    auto findIt = BlocksRegister.find(cname);
    if(findIt == BlocksRegister.end())
    {
      BlocksRegister.insert(std::make_pair(cname, constructor));
      std::cout << "added block: " << cname << "\n";
    }
    else
    {
      BlocksRegister[cname] = constructor;
      std::cout << "overridden block: " << cname << "\n";
    }
  }

  static void registerObjectType(int32_t vendorId, int32_t typeId, CBObjectInfo info)
  {
    auto tup = std::make_tuple(vendorId, typeId);
    auto typeName = std::string(info.name);
    auto findIt = ObjectTypesRegister.find(tup);
    if(findIt == ObjectTypesRegister.end())
    {
      ObjectTypesRegister.insert(std::make_pair(tup, info));
      std::cout << "added object type: " << typeName << "\n";
    }
    else
    {
      ObjectTypesRegister[tup] = info;
      std::cout << "overridden object type: " << typeName << "\n";
    }
  }

  static void registerEnumType(int32_t vendorId, int32_t typeId, CBEnumInfo info)
  {
    auto tup = std::make_tuple(vendorId, typeId);
    auto typeName = std::string(info.name);
    auto findIt = ObjectTypesRegister.find(tup);
    if(findIt == ObjectTypesRegister.end())
    {
      EnumTypesRegister.insert(std::make_pair(tup, info));
      std::cout << "added object type: " << typeName << "\n";
    }
    else
    {
      EnumTypesRegister[tup] = info;
      std::cout << "overridden object type: " << typeName << "\n";
    }
  }

  static void registerRunLoopCallback(const char* eventName, CBCallback callback)
  {
    chainblocks::RunLoopHooks[eventName] = callback;
  }

  static void unregisterRunLoopCallback(const char* eventName)
  {
    auto findIt = chainblocks::RunLoopHooks.find(eventName);
    if(findIt != chainblocks::RunLoopHooks.end())
    {
      chainblocks::RunLoopHooks.erase(findIt);
    }
  }

  static void registerExitCallback(const char* eventName, CBCallback callback)
  {
    chainblocks::ExitHooks[eventName] = callback;
  }

  static void unregisterExitCallback(const char* eventName)
  {
    auto findIt = chainblocks::ExitHooks.find(eventName);
    if(findIt != chainblocks::ExitHooks.end())
    {
      chainblocks::ExitHooks.erase(findIt);
    }
  }

  static void callExitCallbacks()
  {
    // Iterate backwards
    for(auto it = chainblocks::ExitHooks.begin(); it != chainblocks::ExitHooks.end(); ++it)
    {
      it->second();
    }
  }

  static CBVar* globalVariable(const char* name)
  {
    CBVar& v = GlobalVariables[name];
    return &v;
  }

  static bool hasGlobalVariable(const char* name)
  {
    auto findIt = GlobalVariables.find(name);
    if(findIt == GlobalVariables.end())
      return false;
    return true;
  }

  static CBVar* contextVariable(CBContext* ctx, const char* name)
  {
    CBVar& v = ctx->variables[name];
    return &v;
  }

  static CBRuntimeBlock* createBlock(const char* name)
  {
    auto it = BlocksRegister.find(name);
    if(it == BlocksRegister.end())
    {
      return nullptr;
    }

    auto blkp = it->second();

    // Hook inline blocks to override activation in runChain
    if(strcmp(name, "Const") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::CoreConst;
    }
    else if(strcmp(name, "Sleep") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::CoreSleep;
    }
    else if(strcmp(name, "Repeat") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::CoreRepeat;
    }
    else if(strcmp(name, "If") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::CoreIf;
    }
    else if(strcmp(name, "GetVariable") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::CoreGetVariable;
    }
    else if(strcmp(name, "SwapVariables") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::CoreSwapVariables;
    }
    else if(strcmp(name, "Math.Add") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathAdd;
    }
    else if(strcmp(name, "Math.Subtract") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathSubtract;
    }
    else if(strcmp(name, "Math.Multiply") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathMultiply;
    }
    else if(strcmp(name, "Math.Divide") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathDivide;
    }
    else if(strcmp(name, "Math.Xor") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathXor;
    }
    else if(strcmp(name, "Math.And") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathAnd;
    }
    else if(strcmp(name, "Math.Or") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathOr;
    }
    else if(strcmp(name, "Math.Mod") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathMod;
    }
    else if(strcmp(name, "Math.LShift") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathLShift;
    }
    else if(strcmp(name, "Math.RShift") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathRShift;
    }
    else if(strcmp(name, "Math.Abs") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathAbs;
    }
    else if(strcmp(name, "Math.Exp") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathExp;
    }
    else if(strcmp(name, "Math.Exp2") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathExp2;
    }
    else if(strcmp(name, "Math.Expm1") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathExpm1;
    }
    else if(strcmp(name, "Math.Log") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathLog;
    }
    else if(strcmp(name, "Math.Log10") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathLog10;
    }
    else if(strcmp(name, "Math.Log2") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathLog2;
    }
    else if(strcmp(name, "Math.Log1p") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathLog1p;
    }
    else if(strcmp(name, "Math.Sqrt") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathSqrt;
    }
    else if(strcmp(name, "Math.Cbrt") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathCbrt;
    }
    else if(strcmp(name, "Math.Sin") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathSin;
    }
    else if(strcmp(name, "Math.Cos") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathCos;
    }
    else if(strcmp(name, "Math.Tan") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathTan;
    }
    else if(strcmp(name, "Math.Asin") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathAsin;
    }
    else if(strcmp(name, "Math.Acos") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathAcos;
    }
    else if(strcmp(name, "Math.Atan") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathAtan;
    }
    else if(strcmp(name, "Math.Sinh") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathSinh;
    }
    else if(strcmp(name, "Math.Cosh") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathCosh;
    }
    else if(strcmp(name, "Math.Tanh") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathTanh;
    }
    else if(strcmp(name, "Math.Asinh") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathAsinh;
    }
    else if(strcmp(name, "Math.Acosh") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathAcosh;
    }
    else if(strcmp(name, "Math.Atanh") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathAtanh;
    }
    else if(strcmp(name, "Math.Erf") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathErf;
    }
    else if(strcmp(name, "Math.Erfc") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathErfc;
    }
    else if(strcmp(name, "Math.TGamma") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathTGamma;
    }
    else if(strcmp(name, "Math.LGamma") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathLGamma;
    }
    else if(strcmp(name, "Math.Ceil") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathCeil;
    }
    else if(strcmp(name, "Math.Floor") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathFloor;
    }
    else if(strcmp(name, "Math.Trunc") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathTrunc;
    }
    else if(strcmp(name, "Math.Round") == 0)
    {
      blkp->inlineBlockId = CBInlineBlocks::MathRound;
    }

    return blkp;
  }

  static CBVar suspend(double seconds)
  {
    auto current = chainblocks::CurrentChain;
    if(seconds <= 0)
    {
      current->next = Duration(0);
    }
    else
    {
      current->next = Clock::now().time_since_epoch() + Duration(seconds);
    }
    current->context->continuation = current->context->continuation.resume();
    if(current->context->restarted)
    {
      CBVar restart = {};
      restart.valueType = None;
      restart.payload.chainState = Restart;
      return restart;
    }
    else if(current->context->aborted)
    {
      CBVar stop = {};
      stop.valueType = None;
      stop.payload.chainState = Stop;
      return stop;
    }
    CBVar cont = {};
    cont.valueType = None;
    cont.payload.chainState = Continue;
    return cont;
  }

  static CBSeq blocks(CBChain* chain)
  {
    CBSeq result = nullptr;
    stbds_arrsetlen(result, chain->blocks.size());
    for(auto i = 0; i < chain->blocks.size(); i++)
    {
      CBVar blk;
      blk.valueType = Block;
      blk.payload.blockValue = chain->blocks[i];
      result[i] = blk;
    }
    return result;
  }

  #include "runtime_macros.hpp"

  inline static void activateBlock(CBRuntimeBlock* blk, CBContext* context, const CBVar& input, CBVar& previousOutput)
  {
    switch(blk->inlineBlockId)
    {
      case CoreConst:
      {
        auto cblock = reinterpret_cast<CBConstStub*>(blk);
        previousOutput = cblock->constValue;
        return;
      }
      case CoreSleep:
      {
        auto cblock = reinterpret_cast<CBSleepStub*>(blk);
        auto suspendRes = suspend(cblock->sleepTime);
        if(suspendRes.payload.chainState != Continue)
          previousOutput = suspendRes;
        else
          previousOutput = input;
        return;
      }
      case CoreRepeat:
      {
        auto cblock = reinterpret_cast<CBCoreRepeat*>(blk);
        auto repeats = cblock->doForever ? 1 : cblock->times;
        while(repeats)
        {
          for(auto i = 0; i < stbds_arrlen(cblock->blocks); i++)
          {
            // This looks dangerous and error prone but the reality of chainblocks is that
            // a chain is expected to be evaluated using blocks reflection before running!
            auto subBlk = cblock->blocks[i].payload.blockValue;
            activateBlock(subBlk, context, input, previousOutput);
          }

          // make sure to propagate cancelation, but prevent Stop/Restart if passthrough
          if(context->aborted)
          {
            previousOutput.valueType = None;
            previousOutput.payload.chainState = Stop;
            // Quick immediately
            return;
          }

          if(!cblock->doForever)
            repeats--;
        }
        previousOutput = input;
        return;
      }
      case CoreIf:
      {
        // We only do it quick in certain cases!
        auto cblock = reinterpret_cast<CBCoreIf*>(blk);            
        auto match = cblock->match.valueType == ContextVar ?
          cblock->matchCtx ? *cblock->matchCtx : *(cblock->matchCtx = contextVariable(context, cblock->match.payload.stringValue)) :
            cblock->match;
        auto result = false;
        if(unlikely(input.valueType != match.valueType))
        {
          goto ifFalsePath;
        }
        else
        {
          switch(input.valueType)
          {
            case Int:
              switch(cblock->boolOp)
              {
                case 0:
                  result = input.payload.intValue == match.payload.intValue;
                  break;
                case 1:
                  result = input.payload.intValue > match.payload.intValue;
                  break;
                case 2:
                  result = input.payload.intValue < match.payload.intValue;
                  break;
                case 3:
                  result = input.payload.intValue >= match.payload.intValue;
                  break;
                case 4:
                  result = input.payload.intValue <= match.payload.intValue;
                  break;
              }
              break;
            case Float:
              switch(cblock->boolOp)
              {
                case 0:
                  result = input.payload.floatValue == match.payload.floatValue;
                  break;
                case 1:
                  result = input.payload.floatValue > match.payload.floatValue;
                  break;
                case 2:
                  result = input.payload.floatValue < match.payload.floatValue;
                  break;
                case 3:
                  result = input.payload.floatValue >= match.payload.floatValue;
                  break;
                case 4:
                  result = input.payload.floatValue <= match.payload.floatValue;
                  break;
              }
              break;
            case String:
              // http://www.cplusplus.com/reference/string/string/operators/
              switch(cblock->boolOp)
              {
                case 0:
                  result = input.payload.stringValue == match.payload.stringValue;
                  break;
                case 1:
                  result = input.payload.stringValue > match.payload.stringValue;
                  break;
                case 2:
                  result = input.payload.stringValue < match.payload.stringValue;
                  break;
                case 3:
                  result = input.payload.stringValue >= match.payload.stringValue;
                  break;
                case 4:
                  result = input.payload.stringValue <= match.payload.stringValue;
                  break;
              }
              break;
            default:
              // too complex let's just make the activation call into nim
              previousOutput = blk->activate(blk, context, input);
              return;
          }
          if(result)
          {
            for(auto i = 0; i < stbds_arrlen(cblock->trueBlocks); i++)
            {
              // This looks dangerous and error prone but the reality of chainblocks is that
              // a chain is expected to be evaluated using blocks reflection before running!
              auto subBlk = cblock->trueBlocks[i].payload.blockValue;
              activateBlock(subBlk, context, input, previousOutput);
            }       
            // make sure to propagate cancelation, but prevent Stop/Restart if passthrough
            if(context->aborted)
            {
              previousOutput.valueType = None;
              previousOutput.payload.chainState = Stop;
            }
            else if(cblock->passthrough)
            {
              previousOutput = input;
            }
            return;
          }
          else
          {
          ifFalsePath:
            for(auto i = 0; i < stbds_arrlen(cblock->falseBlocks); i++)
            {
              // This looks dangerous and error prone but the reality of chainblocks is that
              // a chain is expected to be evaluated using blocks reflection before running!
              auto subBlk = cblock->falseBlocks[i].payload.blockValue;
              activateBlock(subBlk, context, input, previousOutput);
            }       
            // make sure to propagate cancelation, but prevent Stop/Restart if passthrough
            if(context->aborted)
            {
              previousOutput.valueType = None;
              previousOutput.payload.chainState = Stop;
            }
            else if(cblock->passthrough)
            {
              previousOutput = input;
            }
            return;
          }
        }
        break;
      }
      case CoreGetVariable:
      {
        auto cblock = reinterpret_cast<CBCoreSetVariable*>(blk);
        if(unlikely(!cblock->target)) // call first if we have no target
        {
          previousOutput = blk->activate(blk, context, input);
        }
        else
        {
          previousOutput = *cblock->target;
        }
        return;
      }
      case CoreSwapVariables:
      {
        auto cblock = reinterpret_cast<CBCoreSwapVariables*>(blk);
        if(unlikely(!cblock->target1 || !cblock->target2)) // call first if we have no targets
        {
          previousOutput = blk->activate(blk, context, input); // ignore previousOutput since we pass input
        }
        else
        {
          auto tmp = *cblock->target1;
          *cblock->target1 = *cblock->target2;
          *cblock->target2 = tmp;
          previousOutput = input;
        }
        return;
      }
      case MathAdd:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINEMATH(+, "+")
        return;
      }
      case MathSubtract:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINEMATH(-, "-")
        return;
      }
      case MathMultiply:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINEMATH(*, "*")
        return;
      }
      case MathDivide:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINEMATH(/, "/")
        return;
      }
      case MathXor:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINE_INT_MATH(^, "^")
        return;
      }
      case MathAnd:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINE_INT_MATH(&, "&")
        return;
      }
      case MathOr:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINE_INT_MATH(|, "|")
        return;
      }
      case MathMod:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINE_INT_MATH(%, "%")
        return;
      }
      case MathLShift:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINE_INT_MATH(<<, "<<")
        return;
      }
      case MathRShift:
      {
        auto cblock = reinterpret_cast<CBMathStub*>(blk);
        runChainINLINE_INT_MATH(>>, ">>")
        return;
      }
      case MathAbs:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(fabs, fabsf, "Abs")
        return;
      }
      case MathExp:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(exp, expf, "Exp")
        return;
      }
      case MathExp2:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(exp2, exp2f, "Exp2")
        return;
      }
      case MathExpm1:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(expm1, expm1f, "Expm1")
        return;
      }
      case MathLog:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(log, logf, "Log")
        return;
      }
      case MathLog10:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(log10, log10f, "Log10")
        return;
      }
      case MathLog2:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(log2, log2f, "Log2")
        return;
      }
      case MathLog1p:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(log1p, log1pf, "Log1p")
        return;
      }
      case MathSqrt:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(sqrt, sqrtf, "Sqrt")
        return;
      }
      case MathCbrt:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(cbrt, cbrtf, "Cbrt")
        return;
      }
      case MathSin:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(sin, sinf, "Sin")
        return;
      }
      case MathCos:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(cos, cosf, "Cos")
        return;
      }
      case MathTan:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(tan, tanf, "Tan")
        return;
      }
      case MathAsin:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(asin, asinf, "Asin")
        return;
      }
      case MathAcos:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(acos, acosf, "Acos")
        return;
      }
      case MathAtan:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(atan, atanf, "Atan")
        return;
      }
      case MathSinh:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(sinh, sinhf, "Sinh")
        return;
      }
      case MathCosh:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(cosh, coshf, "Cosh")
        return;
      }
      case MathTanh:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(tanh, tanhf, "Tanh")
        return;
      }
      case MathAsinh:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(asinh, asinhf, "Asinh")
        return;
      }
      case MathAcosh:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(acosh, acoshf, "Acosh")
        return;
      }
      case MathAtanh:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(atanh, atanhf, "Atanh")
        return;
      }
      case MathErf:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(erf, erff, "Erf")
        return;
      }
      case MathErfc:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(erfc, erfcf, "Erfc")
        return;
      }
      case MathTGamma:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(tgamma, tgammaf, "TGamma")
        return;
      }
      case MathLGamma:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(lgamma, lgammaf, "LGamma")
        return;
      }
      case MathCeil:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(ceil, ceilf, "Ceil")
        return;
      }
      case MathFloor:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(floor, floorf, "Floor")
        return;
      }
      case MathTrunc:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(trunc, truncf, "Trunc")
        return;
      }
      case MathRound:
      {
        auto cblock = reinterpret_cast<CBMathUnaryStub*>(blk);
        runChainINLINECMATH(round, roundf, "Round")
        return;
      }
      default: // NotInline
      {
        previousOutput = blk->activate(blk, context, input);
        return;
      }
    }
  }

  inline static std::tuple<bool, CBVar> runChain(CBChain* chain, CBContext* context, CBVar chainInput)
  {
    chain->started = true;
    chain->context = context;
    CBVar previousOutput;
    auto previousChain = CurrentChain;
    CurrentChain = chain;

    for(auto blk : chain->blocks)
    {
      try
      {
        blk->preChain(blk, context);
      }
      catch(const std::exception& e)
      {
        std::cerr << "Pre chain failure, failed block: " << std::string(blk->name(blk)) << "\n";
        if(context->error.length() > 0)
          std::cerr << "Last error: " << std::string(context->error) << "\n";
        std::cerr << e.what() << "\n";
        CurrentChain = previousChain;
        return { false, {} };
      }
      catch(...)
      {
        std::cerr << "Pre chain failure, failed block: " << std::string(blk->name(blk)) << "\n";
        if(context->error.length() > 0)
          std::cerr << "Last error: " << std::string(context->error) << "\n";
        CurrentChain = previousChain;
        return { false, {} };
      }
    }
    
    for(auto blk : chain->blocks)
    {
      try
      {
        #if 0
          std::cout << "Activating block: " << std::string(blk->name(blk)) << "\n";
        #endif

        // Pass chain root input every time we find None, this allows a long chain to re-process the root input if wanted!
        auto input = previousOutput.valueType == None ? chainInput : previousOutput;
        activateBlock(blk, context, input, previousOutput);

        if(previousOutput.valueType == None)
        {
          switch(previousOutput.payload.chainState)
          {
            case Restart:
            {
              runChainPOSTCHAIN
              return { true, previousOutput };
            }
            case Stop:
            {
              // Print errors if any, we might have stopped because of some error!
              if(context->error.length() > 0)
              {
                std::cerr << "Block activation error, failed block: " << std::string(blk->name(blk)) << "\n";
                std::cerr << "Last error: " << std::string(context->error) << "\n";
              }
              runChainPOSTCHAIN
              CurrentChain = previousChain;
              return { false, previousOutput };
            }
            case Continue:
              continue;
          }
        }
      }
      catch(const std::exception& e)
      {
        std::cerr << "Block activation error, failed block: " << std::string(blk->name(blk)) << "\n";
        if(context->error.length() > 0)
          std::cerr << "Last error: " << std::string(context->error) << "\n";
        std::cerr << e.what() << "\n";;
        runChainPOSTCHAIN
        CurrentChain = previousChain;
        return { false, previousOutput };
      }
      catch(...)
      {
        std::cerr << "Block activation error, failed block: " << std::string(blk->name(blk)) << "\n";
        if(context->error.length() > 0)
          std::cerr << "Last error: " << std::string(context->error) << "\n";
        runChainPOSTCHAIN
        CurrentChain = previousChain;
        return { false, previousOutput };
      }
    }

    runChainPOSTCHAIN
    CurrentChain = previousChain;
    return { true, previousOutput };
  }

  static void cleanup(CBChain* chain)
  {
    // Run cleanup on all blocks, prepare them for a new start if necessary
    // Do this in reverse to allow a safer cleanup
    for (auto it = chain->blocks.rbegin(); it != chain->blocks.rend(); ++it)
    {
      auto blk = *it;
      try
      {
        blk->cleanup(blk);
      }
      catch(const std::exception& e)
      {
        std::cerr << "Block cleanup error, failed block: " << std::string(blk->name(blk)) << "\n";
        std::cerr << e.what() << '\n';
      }
      catch(...)
      {
        std::cerr << "Block cleanup error, failed block: " << std::string(blk->name(blk)) << "\n";
      }
    }
  }

  static boost::context::continuation run(CBChain* chain, bool looped, bool unsafe, boost::context::continuation&& sink)
  {
    auto running = true;
    // Reset return state
    chain->returned = false;
    // Create a new context and copy the sink in
    CBContext context(std::move(sink));

    // We prerolled our coro, suspend here before actually starting.
    // This allows us to allocate the stack ahead of time.
    context.continuation = context.continuation.resume();
    if(context.aborted) // We might have stopped before even starting!
      goto endOfChain;
    
    while(running)
    {
      running = looped;
      context.restarted = false; // Remove restarted flag
      
      // Reset len to 0 of the stack
      if(context.stack)
        stbds_arrsetlen(context.stack, 0);
      
      chain->finished = false; // Reset finished flag (atomic)
      auto runRes = runChain(chain, &context, chain->rootTickInput);
      chain->finishedOutput = std::get<1>(runRes); // Write result before setting flag
      chain->finished = true; // Set finished flag (atomic)
      if(!std::get<0>(runRes))
      {
        context.aborted = true;
        break;
      }
      
      if(!unsafe && looped) 
      {
        // Ensure no while(true), yield anyway every run
        chain->next = Duration(0);
        context.continuation = context.continuation.resume();
        // This is delayed upon continuation!!
        if(context.aborted)
          break;
      }
    }
    
  endOfChain:
    // Completely free the stack
    if(context.stack)
      stbds_arrfree(context.stack);
    
    // run cleanup on all the blocks
    cleanup(chain);
    
    // Need to take care that we might have stopped the chain very early due to errors and the next eventual stop() should avoid resuming
    chain->returned = true;
    return std::move(context.continuation);
  }

  static void prepare(CBChain* chain, bool loop = false, bool unsafe = false)
  {
    if(chain->coro)
      return;
    
    chain->coro = new CBCoro(boost::context::callcc(
        [&chain, &loop, &unsafe](boost::context::continuation&& sink)
    {
      return run(chain, loop, unsafe, std::move(sink));
    }));
  }

  static void start(CBChain* chain, CBVar input = {})
  {
    if(!chain->coro || !(*chain->coro) || chain->started)
      return; // check if not null and bool operator also to see if alive!
    
    auto previousChain = chainblocks::CurrentChain;
    chainblocks::CurrentChain = chain;
    
    chain->rootTickInput = input;
    *chain->coro = chain->coro->resume();
    
    chainblocks::CurrentChain = previousChain;
  }

  static void stop(CBChain* chain, CBVar* result = nullptr)
  {
    // Clone the results if we need them
    if(result)
      cloneVar(*result, chain->finishedOutput);
    
    if(chain->coro)
    {
      // Run until exit if alive, need to propagate to all suspended blocks!
      if((*chain->coro) && !chain->returned)
      {
        // Push current chain
        auto previous = chainblocks::CurrentChain;
        chainblocks::CurrentChain = chain;
        
        // set abortion flag, we always have a context in this case
        chain->context->aborted = true;
        
        // BIG Warning: chain->context existed in the coro stack!!!
        // after this resume chain->context is trash!
        chain->coro->resume();
        
        // Pop current chain
        chainblocks::CurrentChain = previous;
      }
      
      // delete also the coro ptr
      delete chain->coro;
      chain->coro = nullptr;
    }
    else
    {
      // if we had a coro this will run inside it!
      cleanup(chain);
    }
    
    chain->started = false;
  }

  static void tick(CBChain* chain, CBVar rootInput = CBVar())
  {
    if(!chain->coro || !(*chain->coro) || chain->returned || !chain->started)
      return; // check if not null and bool operator also to see if alive!
    
    Duration now = Clock::now().time_since_epoch();
    if(now >= chain->next)
    {
      auto previousChain = chainblocks::CurrentChain;
      chainblocks::CurrentChain = chain;
      
      chain->rootTickInput = rootInput;
      *chain->coro = chain->coro->resume();
      
      chainblocks::CurrentChain = previousChain;
    }
  }

  static bool isRunning(CBChain* chain)
  {
    return chain->started && !chain->returned;
  }

  static bool isCanceled(CBContext* context)
  {
    return context->aborted;
  }

  static std::string store(CBVar var) 
  { 
    json jsonVar = var;
    return jsonVar.dump();
  }

  static void load(CBVar& var, const char* jsonStr)
  {
    auto j = json::parse(jsonStr);
    var = j.get<CBVar>();
  }

  static std::string store(CBChainPtr chain) 
  { 
    json jsonChain = chain;
    return jsonChain.dump(); 
  }

  static void load(CBChainPtr& chain, const char* jsonStr)
  {
    auto j = json::parse(jsonStr);
    chain = j.get<CBChainPtr>();
  }

  static void sleep(double seconds = -1.0)
  {
    //negative = no sleep, just run callbacks
    if(seconds >= 0)
    {
#ifdef _WIN32
      HANDLE timer;
      LARGE_INTEGER ft;
      ft.QuadPart = -(int64_t(seconds * 10000000));
      timer = CreateWaitableTimer(NULL, TRUE, NULL);
      SetWaitableTimer(timer, &ft, 0, NULL, NULL, 0);
      WaitForSingleObject(timer, INFINITE);
      CloseHandle(timer);
#else
      struct timespec delay = {0, int64_t(seconds * 1000000000)};
      while(nanosleep(&delay, &delay));
#endif
    }

    // Run loop callbacks after sleeping
    for(auto& cbinfo : RunLoopHooks)
    {
      if(cbinfo.second)
      {
        cbinfo.second();
      }
    }
  }
};

struct CBNode
{
  void schedule(CBChain* chain, CBVar input = {}, bool loop = false, bool unsafe = false)
  {
    chains.push_back(chain);
    chain->node = this;
    chainblocks::prepare(chain, loop, unsafe);
    chainblocks::start(chain, input);
  }
  
  void tick(CBVar input = {})
  {
    chainsTicking = chains;
    for(auto chain : chainsTicking)
    {
      chainblocks::tick(chain, input);
      if(!chainblocks::isRunning(chain))
      {
        chainblocks::stop(chain);
        chains.remove(chain);
      }
    }
  }
  
  void stop()
  {
    for(auto chain : chains)
    {
      chainblocks::stop(chain);
    }
  }
  
private:
  std::list<CBChain*> chains;
  std::list<CBChain*> chainsTicking;
};

#endif //CHAINBLOCKS_RUNTIME
