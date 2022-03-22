# Automatic rust target config
set(Rust_BUILD_SUBDIR_HAS_TARGET ON)

if(NOT Rust_CARGO_TARGET)
  if(ANDROID)
    if(ANDROID_ABI MATCHES "arm64-v8a")
      set(Rust_CARGO_TARGET aarch64-linux-android)
    else()
      message(FATAL_ERROR "Unsupported rust android ABI: ${ANDROID_ABI}")
    endif()
  elseif(EMSCRIPTEN)
    set(Rust_CARGO_TARGET wasm32-unknown-emscripten)
  elseif(APPLE)
    if(IOS)
      set(PLATFORM "ios")
    else()
      set(PLATFORM "darwin")
    endif()
    if(CMAKE_OSX_ARCHITECTURES MATCHES "arm64")
      set(Rust_CARGO_TARGET aarch64-apple-${PLATFORM})
    else()
      set(Rust_CARGO_TARGET x86_64-apple-${PLATFORM})
    endif()
  elseif(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(Rust_CARGO_TARGET i686-pc-windows-gnu)
  elseif(WIN32)
    set(Rust_CARGO_TARGET x86_64-pc-windows-${WINDOWS_ABI})
  elseif(LINUX)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64")
      set(Rust_CARGO_TARGET x86_64-unknown-linux-gnu)
    endif()
  endif()

  if(NOT Rust_CARGO_TARGET)
    message(FATAL_ERROR "Unsupported rust target")
  endif()
endif()


set(Rust_LIB_PREFIX ${LIB_PREFIX})
set(Rust_LIB_SUFFIX ${LIB_SUFFIX})

message(STATUS "Rust_CARGO_TARGET = ${Rust_CARGO_TARGET}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(Rust_BUILD_SUBDIR_CONFIGURATION debug)
else()
  set(Rust_CARGO_FLAGS --release)
  set(Rust_BUILD_SUBDIR_CONFIGURATION release)
endif()

if(Rust_BUILD_SUBDIR_HAS_TARGET)
  set(Rust_BUILD_SUBDIR ${Rust_CARGO_TARGET}/${Rust_BUILD_SUBDIR_CONFIGURATION})
else()
  set(Rust_BUILD_SUBDIR ${Rust_BUILD_SUBDIR_CONFIGURATION})
endif()
message(STATUS "Rust_BUILD_SUBDIR = ${Rust_BUILD_SUBDIR}")

set(Rust_FLAGS ${Rust_FLAGS} -Ctarget-cpu=${ARCH})

if(RUST_USE_LTO)
  set(Rust_FLAGS ${Rust_FLAGS} -Clinker-plugin-lto -Clinker=clang -Clink-arg=-fuse-ld=lld)
endif()

# Currently required for --crate-type argument
set(RUST_NIGHTLY TRUE)
if(RUST_NIGHTLY)
  list(APPEND Rust_CARGO_UNSTABLE_FLAGS -Zunstable-options)
  set(Rust_CARGO_TOOLCHAIN "+nightly")
endif()

if(EMSCRIPTEN_PTHREADS)
  list(APPEND Rust_FLAGS -Ctarget-feature=+atomics,+bulk-memory)
  list(APPEND Rust_CARGO_UNSTABLE_FLAGS -Zbuild-std=panic_abort,std)
  set(Rust_CARGO_TOOLCHAIN "+nightly")
endif()

macro(ADD_RUST_FEATURE VAR FEATURE)
  if(${VAR})
    set(${VAR} ${${VAR}},${FEATURE})
  else()
    set(${VAR} ${FEATURE})
  endif()
endmacro()
