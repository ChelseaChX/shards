#include "foundation.hpp"

static struct Globals {
  Globals() {
    LOG(TRACE) << "EM MAIN FIBER INIT";
    emscripten_fiber_init_from_current_context(&main_coro, asyncify_main_stack,
                                               CBCoro::as_stack_size);

#ifndef NDEBUG
    CBCoro c1;
    c1.init([&]() {
      LOG(TRACE) << "Inside coro";
      c1.yield();
      LOG(TRACE) << "Inside coro again";
      c1.yield();
    });
    c1.resume();
    c1.resume();
#endif
  }

  emscripten_fiber_t main_coro{};
  uint8_t asyncify_main_stack[CBCoro::as_stack_size];
} Globals;

[[noreturn]] static void action(void *p) {
  LOG(TRACE) << "EM FIBER ACTION RUN";
  auto coro = reinterpret_cast<CBCoro *>(p);
  coro->func();
  // If entry_func returns, the entire program will end, as if main had
  // returned.
  throw;
}

void CBCoro::init(const std::function<void()> &func) {
  LOG(TRACE) << "EM FIBER INIT";
  this->func = func;
  c_stack = new (std::align_val_t{16}) uint8_t[CB_STACK_SIZE];
  emscripten_fiber_init(&em_fiber, action, this, c_stack, stack_size,
                        asyncify_stack, as_stack_size);
}

NO_INLINE void CBCoro::resume() {
  LOG(TRACE) << "EM FIBER SWAP RESUME " << (void *)(&em_fiber);
  emscripten_fiber_swap(&Globals.main_coro, &em_fiber);
}

NO_INLINE void CBCoro::yield() {
  LOG(TRACE) << "EM FIBER SWAP YIELD " << (void *)(&em_fiber);
  emscripten_fiber_swap(&em_fiber, &Globals.main_coro);
}
