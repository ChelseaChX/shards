#pragma once

#include "enums.hpp"
#include "gfx_wgpu.hpp"
#include "platform.hpp"
#include "types.hpp"
#include <cassert>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace gfx {
struct ContextCreationOptions {
  bool debug = true;
  void *overrideNativeWindowHandle = nullptr;
};

struct CopyBuffer {
  std::vector<uint8_t> data;
};

enum class ContextState {
  Uninitialized,
  Ok,
  Incomplete,
};

enum class ContextFrameState {
  Ok,
  WaitingForEnd,
};

struct Window;
struct ContextData;
struct ContextMainOutput;
struct Context {
private:
  std::shared_ptr<ContextMainOutput> mainOutput;
  ContextState state = ContextState::Uninitialized;
  ContextFrameState frameState = ContextFrameState::Ok;
  bool suspended = false;

public:
  WGPUInstance wgpuInstance = nullptr;
  WGPUAdapter wgpuAdapter = nullptr;
  WGPUDevice wgpuDevice = nullptr;
  WGPUQueue wgpuQueue = nullptr;
  ContextCreationOptions options;

  std::unordered_map<ContextData *, std::weak_ptr<ContextData>> contextDatas;

public:
  Context();
  ~Context();

  // Initialize a context on a window's surface
  void init(Window &window, const ContextCreationOptions &options = ContextCreationOptions{});
  // Initialize headless context
  void init(const ContextCreationOptions &options = ContextCreationOptions{});

  void release();
  bool isInitialized() const { return state != ContextState::Uninitialized; }

  Window &getWindow();
  void resizeMainOutputConditional(const int2 &newSize);
  int2 getMainOutputSize() const;
  WGPUTextureView getMainOutputTextureView();
  WGPUTextureFormat getMainOutputFormat() const;
  bool isHeadless() const;

  // Returns when a frame can be rendered
  // Returns false while device is lost an can not be reacquired
  bool beginFrame();
  void endFrame();
  void sync();

  // When entering background, releases all graphics resources and pause rendering
  void suspend();
  // Call after suspend when resuming, will recreate graphics device and continue rendering
  void resume();

  void submit(WGPUCommandBuffer cmdBuffer);

  // start tracking an object implementing WithContextData so it's data is released with this context
  void addContextDataInternal(std::weak_ptr<ContextData> ptr);
  void removeContextDataInternal(ContextData *ptr);

private:
  void deviceLost();

  void acquireDevice();
  bool tryAcquireDevice();
  void releaseDevice();

  WGPUSurface getOrCreateSurface();

  void createAdapter();
  void releaseAdapter();

  void initCommon();

  void present();

  void collectContextData();
  void releaseAllContextData();
};

} // namespace gfx
