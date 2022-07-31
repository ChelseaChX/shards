namespace shards {
extern "C" void rust_setupProfiling();
void setupProfiling() {
#if TRACY_ENABLE
  static bool initialized = false;
  if (!initialized) {
    rust_setupProfiling();
    initialized = true;
  }
#endif
}
} // namespace shards
