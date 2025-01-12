#ifndef GFX_TESTS_PLATFORM_ID
#define GFX_TESTS_PLATFORM_ID

#include "gfx/context.hpp"
#include <string>

namespace gfx {
struct Context;
struct TestPlatformId {
  std::string id;
  static TestPlatformId get(const Context &context);
  operator std::string() const;
};
} // namespace gfx

#endif // GFX_TESTS_PLATFORM_ID
