/* SPDX-License-Identifier: BSD 3-Clause "New" or "Revised" License */
/* Copyright © 2019-2020 Giovanni Petrantoni */

#include "shared.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>

// for now a carbon copy of wasm3 simple wasi

#if defined(__clang__)
#pragma clang diagnostic ignored "-Wunused-variable"
#elif defined(__GNUC__)
#pragma GCC diagnostic ignored "-Wunused-variable"
#endif

// WIN32 is defined by us.. but in wasm3 is actually an error, it's never
// defined and causes some issues with vectorcall definition
#pragma push_macro("WIN32")
#undef WIN32
#include "wasm3.h"

#include "m3_api_defs.h"
#include "m3_api_libc.h"
#include "m3_config.h"
#include "m3_env.h"
#include "m3_exception.h"
#pragma pop_macro("WIN32")

#include "extra/wasi_core.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>

#if defined(__wasi__) || defined(__APPLE__) || defined(__ANDROID_API__) ||     \
    defined(__OpenBSD__) || defined(__linux__) || defined(__EMSCRIPTEN__)
#include <sys/uio.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX // TARGET_OS_MAC includes iOS
#include <sys/random.h>
#else // iOS / Simulator
#include <Security/Security.h>
#endif
#else
#include <sys/random.h>
#endif
#define HAS_IOVEC
#elif defined(_WIN32)
#include <Windows.h>
#include <io.h>
// See http://msdn.microsoft.com/en-us/library/windows/desktop/aa387694.aspx
#define SystemFunction036 NTAPI SystemFunction036
#include <NTSecAPI.h>
#undef SystemFunction036
#define ssize_t SSIZE_T

#define open _open
#define read _read
#define write _write
#define close _close
#endif

namespace chainblocks {
namespace Wasm {
namespace WASI {
typedef struct wasi_iovec_t {
  __wasi_size_t buf;
  __wasi_size_t buf_len;
} wasi_iovec_t;

#define PREOPEN_CNT 4

typedef struct Preopen {
  int fd;
  const char *path;
} Preopen;

Preopen preopen[PREOPEN_CNT] = {
    {0, "<stdin>"},
    {1, "<stdout>"},
    {2, "<stderr>"},
    {-1, "./"},
};

static __wasi_errno_t errno_to_wasi(int errnum) {
  switch (errnum) {
  case EPERM:
    return __WASI_ERRNO_PERM;
    break;
  case ENOENT:
    return __WASI_ERRNO_NOENT;
    break;
  case ESRCH:
    return __WASI_ERRNO_SRCH;
    break;
  case EINTR:
    return __WASI_ERRNO_INTR;
    break;
  case EIO:
    return __WASI_ERRNO_IO;
    break;
  case ENXIO:
    return __WASI_ERRNO_NXIO;
    break;
  case E2BIG:
    return __WASI_ERRNO_2BIG;
    break;
  case ENOEXEC:
    return __WASI_ERRNO_NOEXEC;
    break;
  case EBADF:
    return __WASI_ERRNO_BADF;
    break;
  case ECHILD:
    return __WASI_ERRNO_CHILD;
    break;
  case EAGAIN:
    return __WASI_ERRNO_AGAIN;
    break;
  case ENOMEM:
    return __WASI_ERRNO_NOMEM;
    break;
  case EACCES:
    return __WASI_ERRNO_ACCES;
    break;
  case EFAULT:
    return __WASI_ERRNO_FAULT;
    break;
  case EBUSY:
    return __WASI_ERRNO_BUSY;
    break;
  case EEXIST:
    return __WASI_ERRNO_EXIST;
    break;
  case EXDEV:
    return __WASI_ERRNO_XDEV;
    break;
  case ENODEV:
    return __WASI_ERRNO_NODEV;
    break;
  case ENOTDIR:
    return __WASI_ERRNO_NOTDIR;
    break;
  case EISDIR:
    return __WASI_ERRNO_ISDIR;
    break;
  case EINVAL:
    return __WASI_ERRNO_INVAL;
    break;
  case ENFILE:
    return __WASI_ERRNO_NFILE;
    break;
  case EMFILE:
    return __WASI_ERRNO_MFILE;
    break;
  case ENOTTY:
    return __WASI_ERRNO_NOTTY;
    break;
  case ETXTBSY:
    return __WASI_ERRNO_TXTBSY;
    break;
  case EFBIG:
    return __WASI_ERRNO_FBIG;
    break;
  case ENOSPC:
    return __WASI_ERRNO_NOSPC;
    break;
  case ESPIPE:
    return __WASI_ERRNO_SPIPE;
    break;
  case EROFS:
    return __WASI_ERRNO_ROFS;
    break;
  case EMLINK:
    return __WASI_ERRNO_MLINK;
    break;
  case EPIPE:
    return __WASI_ERRNO_PIPE;
    break;
  case EDOM:
    return __WASI_ERRNO_DOM;
    break;
  case ERANGE:
    return __WASI_ERRNO_RANGE;
    break;
  default:
    return __WASI_ERRNO_INVAL;
  }
}

#if defined(_WIN32)

#if !defined(__MINGW32__)

static inline int clock_gettime(int clk_id, struct timespec *spec) {
  __int64 wintime;
  GetSystemTimeAsFileTime((FILETIME *)&wintime);
  wintime -= 116444736000000000i64;            // 1jan1601 to 1jan1970
  spec->tv_sec = wintime / 10000000i64;        // seconds
  spec->tv_nsec = wintime % 10000000i64 * 100; // nano-seconds
  return 0;
}

static inline int clock_getres(int clk_id, struct timespec *spec) {
  return -1; // Defaults to 1000000
}

#endif

static inline int convert_clockid(__wasi_clockid_t in) { return 0; }

#else // _WIN32

static inline auto convert_clockid(__wasi_clockid_t in) {
  switch (in) {
  case __WASI_CLOCKID_MONOTONIC:
    return CLOCK_MONOTONIC;
  case __WASI_CLOCKID_PROCESS_CPUTIME_ID:
    return CLOCK_PROCESS_CPUTIME_ID;
  case __WASI_CLOCKID_REALTIME:
    return CLOCK_REALTIME;
  case __WASI_CLOCKID_THREAD_CPUTIME_ID:
    return CLOCK_THREAD_CPUTIME_ID;
  default:
    throw std::runtime_error("Invalid clockid case");
  }
}

#endif // _WIN32

static inline __wasi_timestamp_t convert_timespec(const struct timespec *ts) {
  if (ts->tv_sec < 0)
    return 0;
  if ((__wasi_timestamp_t)ts->tv_sec >= UINT64_MAX / 1000000000)
    return UINT64_MAX;
  return (__wasi_timestamp_t)ts->tv_sec * 1000000000 + ts->tv_nsec;
}

#if defined(HAS_IOVEC)

static inline void copy_iov_to_host(void *_mem, struct iovec *host_iov,
                                    wasi_iovec_t *wasi_iov, int32_t iovs_len) {
  // Convert wasi memory offsets to host addresses
  for (int i = 0; i < iovs_len; i++) {
    host_iov[i].iov_base = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iov[i].buf));
    host_iov[i].iov_len = m3ApiReadMem32(&wasi_iov[i].buf_len);
  }
}

#endif

/*
 * WASI API implementation
 */

m3ApiRawFunction(m3_wasi_unstable_args_get) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArgMem(uint32_t *, argv);
  m3ApiGetArgMem(char *, argv_buf);

  // LOG(TRACE) << "WASI args_get";

  if (runtime == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

  for (u32 i = 0; i < runtime->argc; ++i) {
    m3ApiWriteMem32(&argv[i], m3ApiPtrToOffset(argv_buf));

    size_t len = strlen(runtime->argv[i]);
    memcpy(argv_buf, runtime->argv[i], len);
    argv_buf += len;
    *argv_buf++ = 0;
  }

  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_args_sizes_get) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArgMem(__wasi_size_t *, argc);
  m3ApiGetArgMem(__wasi_size_t *, argv_buf_size);

  // LOG(TRACE) << "WASI args_sizes_get";

  if (runtime == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

  __wasi_size_t buflen = 0;
  for (u32 i = 0; i < runtime->argc; ++i) {
    buflen += strlen(runtime->argv[i]) + 1;
  }

  m3ApiWriteMem32(argc, runtime->argc);
  m3ApiWriteMem32(argv_buf_size, buflen);

  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_environ_get) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArgMem(uint32_t *, env);
  m3ApiGetArgMem(char *, env_buf);

  // LOG(TRACE) << "WASI environ_get";

  if (runtime == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }
  // TODO
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_environ_sizes_get) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArgMem(__wasi_size_t *, env_count);
  m3ApiGetArgMem(__wasi_size_t *, env_buf_size);

  // LOG(TRACE) << "WASI environ_sizes_get";

  if (runtime == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

  // TODO
  m3ApiWriteMem32(env_count, 0);
  m3ApiWriteMem32(env_buf_size, 0);

  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_dir_name) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, fd);
  m3ApiGetArgMem(char *, path);
  m3ApiGetArg(__wasi_size_t, path_len);

  // LOG(TRACE) << "WASI fd_prestat_dir_name";

  if (runtime == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }
  if (fd < 3 || fd >= PREOPEN_CNT) {
    m3ApiReturn(__WASI_ERRNO_BADF);
  }
  size_t slen = strlen(preopen[fd].path);
  memcpy(path, preopen[fd].path, M3_MIN(slen, path_len));
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_prestat_get) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, fd);
  m3ApiGetArgMem(uint32_t *, buf); // TODO: use actual struct

  // LOG(TRACE) << "WASI fd_prestat_get";

  if (runtime == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }
  if (fd < 3 || fd >= PREOPEN_CNT) {
    m3ApiReturn(__WASI_ERRNO_BADF);
  }
  m3ApiWriteMem32(buf, __WASI_PREOPENTYPE_DIR);
  m3ApiWriteMem32(buf + 1, strlen(preopen[fd].path));
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_fdstat_get) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, fd);
  m3ApiGetArgMem(__wasi_fdstat_t *, fdstat);

  // LOG(TRACE) << "WASI fd_fdstat_get";

  if (runtime == NULL || fdstat == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

#ifdef _WIN32

  // TODO: This needs a proper implementation
  if (fd < PREOPEN_CNT) {
    fdstat->fs_filetype = __WASI_FILETYPE_DIRECTORY;
  } else {
    fdstat->fs_filetype = __WASI_FILETYPE_REGULAR_FILE;
  }

  fdstat->fs_flags = 0;
  fdstat->fs_rights_base = (uint64_t)-1;       // all rights
  fdstat->fs_rights_inheriting = (uint64_t)-1; // all rights
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
#else
  struct stat fd_stat;
  int fl = fcntl(fd, F_GETFL);
  if (fl < 0) {
    m3ApiReturn(errno_to_wasi(errno));
  }
  fstat(fd, &fd_stat);
  int mode = fd_stat.st_mode;
  fdstat->fs_filetype =
      (S_ISBLK(mode) ? __WASI_FILETYPE_BLOCK_DEVICE : 0) |
      (S_ISCHR(mode) ? __WASI_FILETYPE_CHARACTER_DEVICE : 0) |
      (S_ISDIR(mode) ? __WASI_FILETYPE_DIRECTORY : 0) |
      (S_ISREG(mode) ? __WASI_FILETYPE_REGULAR_FILE : 0) |
      //(S_ISSOCK(mode)  ? __WASI_FILETYPE_SOCKET_STREAM    : 0) |
      (S_ISLNK(mode) ? __WASI_FILETYPE_SYMBOLIC_LINK : 0);
  m3ApiWriteMem16(&fdstat->fs_flags,
                  ((fl & O_APPEND) ? __WASI_FDFLAGS_APPEND : 0) |
                      ((fl & O_DSYNC) ? __WASI_FDFLAGS_DSYNC : 0) |
                      ((fl & O_NONBLOCK) ? __WASI_FDFLAGS_NONBLOCK : 0) |
                      //((fl & O_RSYNC)     ? __WASI_FDFLAGS_RSYNC     : 0) |
                      ((fl & O_SYNC) ? __WASI_FDFLAGS_SYNC : 0));
  fdstat->fs_rights_base = (uint64_t)-1;       // all rights
  fdstat->fs_rights_inheriting = (uint64_t)-1; // all rights
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
#endif
}

m3ApiRawFunction(m3_wasi_unstable_fd_fdstat_set_flags) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, fd);
  m3ApiGetArg(__wasi_fdflags_t, flags);

  // LOG(TRACE) << "WASI m3_wasi_unstable_fd_fdstat_set_flags";

  // TODO

  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_fd_seek) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, fd);
  m3ApiGetArg(__wasi_filedelta_t, offset);
  m3ApiGetArg(__wasi_whence_t, wasi_whence);
  m3ApiGetArgMem(__wasi_filesize_t *, result);

  // LOG(TRACE) << "WASI m3_wasi_unstable_fd_seek";

  if (runtime == NULL || result == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

  int whence;
  switch (wasi_whence) {
  case __WASI_WHENCE_CUR:
    whence = SEEK_CUR;
    break;
  case __WASI_WHENCE_END:
    whence = SEEK_END;
    break;
  case __WASI_WHENCE_SET:
    whence = SEEK_SET;
    break;
  default:
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

  int64_t ret;
#if defined(M3_COMPILER_MSVC) || defined(__MINGW32__)
  ret = _lseeki64(fd, offset, whence);
#else
  ret = lseek(fd, offset, whence);
#endif
  if (ret < 0) {
    m3ApiReturn(errno_to_wasi(errno));
  }
  m3ApiWriteMem64(result, ret);
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_path_open) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, dirfd);
  m3ApiGetArg(__wasi_lookupflags_t, dirflags);
  m3ApiGetArgMem(const char *, path);
  m3ApiGetArg(__wasi_size_t, path_len);
  m3ApiGetArg(__wasi_oflags_t, oflags);
  m3ApiGetArg(__wasi_rights_t, fs_rights_base);
  m3ApiGetArg(__wasi_rights_t, fs_rights_inheriting);
  m3ApiGetArg(__wasi_fdflags_t, fs_flags);
  m3ApiGetArgMem(__wasi_fd_t *, fd);

  // LOG(TRACE) << "WASI m3_wasi_unstable_path_open";

  if (path_len >= 512)
    m3ApiReturn(__WASI_ERRNO_INVAL);

    // copy path so we can ensure it is NULL terminated
#if defined(M3_COMPILER_MSVC)
  char host_path[512];
#else
  char host_path[path_len + 1];
#endif
  memcpy(host_path, path, path_len);
  host_path[path_len] = '\0'; // NULL terminator

  // LOG(TRACE) << "WASI path_open: " << std::string_view(host_path, path_len);

#if defined(_WIN32)
  // TODO: This all needs a proper implementation

  int flags = ((oflags & __WASI_OFLAGS_CREAT) ? _O_CREAT : 0) |
              ((oflags & __WASI_OFLAGS_EXCL) ? _O_EXCL : 0) |
              ((oflags & __WASI_OFLAGS_TRUNC) ? _O_TRUNC : 0) |
              ((fs_flags & __WASI_FDFLAGS_APPEND) ? _O_APPEND : 0);

  if ((fs_rights_base & __WASI_RIGHTS_FD_READ) &&
      (fs_rights_base & __WASI_RIGHTS_FD_WRITE)) {
    flags |= _O_RDWR;
  } else if ((fs_rights_base & __WASI_RIGHTS_FD_WRITE)) {
    flags |= _O_WRONLY;
  } else if ((fs_rights_base & __WASI_RIGHTS_FD_READ)) {
    flags |= _O_RDONLY; // no-op because O_RDONLY is 0
  }
  int mode = 0644;

  int host_fd = open(host_path, flags, mode);

  if (host_fd < 0) {
    m3ApiReturn(errno_to_wasi(errno));
  } else {
    m3ApiWriteMem32(fd, host_fd);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
  }
#else
  // translate o_flags and fs_flags into flags and mode
  int flags = ((oflags & __WASI_OFLAGS_CREAT) ? O_CREAT : 0) |
              //((oflags & __WASI_OFLAGS_DIRECTORY)         ? O_DIRECTORY : 0) |
              ((oflags & __WASI_OFLAGS_EXCL) ? O_EXCL : 0) |
              ((oflags & __WASI_OFLAGS_TRUNC) ? O_TRUNC : 0) |
              ((fs_flags & __WASI_FDFLAGS_APPEND) ? O_APPEND : 0) |
              ((fs_flags & __WASI_FDFLAGS_DSYNC) ? O_DSYNC : 0) |
              ((fs_flags & __WASI_FDFLAGS_NONBLOCK) ? O_NONBLOCK : 0) |
              //((fs_flags & __WASI_FDFLAGS_RSYNC)      ? O_RSYNC     : 0) |
              ((fs_flags & __WASI_FDFLAGS_SYNC) ? O_SYNC : 0);
  if ((fs_rights_base & __WASI_RIGHTS_FD_READ) &&
      (fs_rights_base & __WASI_RIGHTS_FD_WRITE)) {
    flags |= O_RDWR;
  } else if ((fs_rights_base & __WASI_RIGHTS_FD_WRITE)) {
    flags |= O_WRONLY;
  } else if ((fs_rights_base & __WASI_RIGHTS_FD_READ)) {
    flags |= O_RDONLY; // no-op because O_RDONLY is 0
  }
  int mode = 0644;
  int host_fd = openat(dirfd, host_path, flags, mode);

  if (host_fd < 0) {
    m3ApiReturn(errno_to_wasi(errno));
  } else {
    m3ApiWriteMem32(fd, host_fd);
    m3ApiReturn(__WASI_ERRNO_SUCCESS);
  }
#endif
}

m3ApiRawFunction(m3_wasi_unstable_fd_read) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, fd);
  m3ApiGetArgMem(wasi_iovec_t *, wasi_iovs);
  m3ApiGetArg(__wasi_size_t, iovs_len);
  m3ApiGetArgMem(__wasi_size_t *, nread);

  // LOG(TRACE) << "WASI fd_read, fd: " << fd;

  if (runtime == NULL || nread == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

#if defined(HAS_IOVEC)
  struct iovec iovs[iovs_len];
  copy_iov_to_host(_mem, iovs, wasi_iovs, iovs_len);

  ssize_t ret = readv(fd, iovs, iovs_len);
  if (ret < 0) {
    m3ApiReturn(errno_to_wasi(errno));
  }
  m3ApiWriteMem32(nread, ret);
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
#else
  ssize_t res = 0;
  for (__wasi_size_t i = 0; i < iovs_len; i++) {
    void *addr = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iovs[i].buf));
    size_t len = m3ApiReadMem32(&wasi_iovs[i].buf_len);
    if (len == 0)
      continue;

    int ret = read(fd, addr, len);
    if (ret < 0)
      m3ApiReturn(errno_to_wasi(errno));
    res += ret;
    if ((size_t)ret < len)
      break;
  }
  m3ApiWriteMem32(nread, res);
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
#endif
}

m3ApiRawFunction(m3_wasi_unstable_fd_write) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, fd);
  m3ApiGetArgMem(wasi_iovec_t *, wasi_iovs);
  m3ApiGetArg(__wasi_size_t, iovs_len);
  m3ApiGetArgMem(__wasi_size_t *, nwritten);

  // LOG(TRACE) << "WASI fd_write, fd: " << fd;

  if (runtime == NULL || nwritten == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

#if defined(HAS_IOVEC)
  struct iovec iovs[iovs_len];
  copy_iov_to_host(_mem, iovs, wasi_iovs, iovs_len);

  ssize_t ret = writev(fd, iovs, iovs_len);
  if (ret < 0) {
    m3ApiReturn(errno_to_wasi(errno));
  }
  m3ApiWriteMem32(nwritten, ret);
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
#else
  ssize_t res = 0;
  for (__wasi_size_t i = 0; i < iovs_len; i++) {
    void *addr = m3ApiOffsetToPtr(m3ApiReadMem32(&wasi_iovs[i].buf));
    size_t len = m3ApiReadMem32(&wasi_iovs[i].buf_len);
    if (len == 0)
      continue;

    int ret = write(fd, addr, len);
    if (ret < 0)
      m3ApiReturn(errno_to_wasi(errno));
    res += ret;
    if ((size_t)ret < len)
      break;
  }
  m3ApiWriteMem32(nwritten, res);
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
#endif
}

m3ApiRawFunction(m3_wasi_unstable_fd_close) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, fd);

  // LOG(TRACE) << "WASI m3_wasi_unstable_fd_close";

  int ret = close(fd);
  m3ApiReturn(ret == 0 ? __WASI_ERRNO_SUCCESS : ret);
}

m3ApiRawFunction(m3_wasi_unstable_fd_datasync) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_fd_t, fd);

  // LOG(TRACE) << "WASI m3_wasi_unstable_fd_datasync";

#if defined(_WIN32)
  int ret = _commit(fd);
#elif defined(__APPLE__)
  int ret = fsync(fd);
#elif defined(__ANDROID_API__) || defined(__OpenBSD__) ||                      \
    defined(__linux__) || defined(__EMSCRIPTEN__)
  int ret = fdatasync(fd);
#else
  int ret = __WASI_ERRNO_NOSYS;
#endif
  m3ApiReturn(ret == 0 ? __WASI_ERRNO_SUCCESS : ret);
}

m3ApiRawFunction(m3_wasi_unstable_random_get) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArgMem(uint8_t *, buf);
  m3ApiGetArg(__wasi_size_t, buflen);

  // LOG(TRACE) << "WASI m3_wasi_unstable_random_get";

  while (1) {
    ssize_t retlen = 0;

#if defined(__wasi__) || defined(__APPLE__) || defined(__ANDROID_API__) ||     \
    defined(__OpenBSD__) || defined(__EMSCRIPTEN__)
    size_t reqlen = M3_MIN(buflen, 256);
#if defined(__APPLE__) && (TARGET_OS_IPHONE || TARGET_IPHONE_SIMULATOR)
    retlen =
        SecRandomCopyBytes(kSecRandomDefault, reqlen, buf) < 0 ? -1 : reqlen;
#else
    retlen = getentropy(buf, reqlen) < 0 ? -1 : reqlen;
#endif
#elif defined(__FreeBSD__) || defined(__linux__)
    retlen = getrandom(buf, buflen, 0);
#elif defined(_WIN32)
    if (RtlGenRandom(buf, buflen) == TRUE) {
      m3ApiReturn(__WASI_ERRNO_SUCCESS);
    }
#else
    m3ApiReturn(__WASI_ERRNO_NOSYS);
#endif
    if (retlen < 0) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      m3ApiReturn(errno_to_wasi(errno));
    } else if (retlen == buflen) {
      m3ApiReturn(__WASI_ERRNO_SUCCESS);
    } else {
      buf += retlen;
      buflen -= retlen;
    }
  }
}

m3ApiRawFunction(m3_wasi_unstable_clock_res_get) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_clockid_t, wasi_clk_id);
  m3ApiGetArgMem(__wasi_timestamp_t *, resolution);

  // LOG(TRACE) << "WASI m3_wasi_unstable_clock_res_get";

  if (runtime == NULL || resolution == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

  auto clk = convert_clockid(wasi_clk_id);
  if (clk < 0)
    m3ApiReturn(__WASI_ERRNO_INVAL);

  struct timespec tp;
  if (clock_getres(clk, &tp) != 0) {
    m3ApiWriteMem64(resolution, 1000000);
  } else {
    m3ApiWriteMem64(resolution, convert_timespec(&tp));
  }

  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_clock_time_get) {
  m3ApiReturnType(uint32_t);
  m3ApiGetArg(__wasi_clockid_t, wasi_clk_id);
  m3ApiGetArg(__wasi_timestamp_t, precision);
  m3ApiGetArgMem(__wasi_timestamp_t *, time);

  // LOG(TRACE) << "WASI m3_wasi_unstable_clock_time_get";

  if (runtime == NULL || time == NULL) {
    m3ApiReturn(__WASI_ERRNO_INVAL);
  }

  auto clk = convert_clockid(wasi_clk_id);
  if (clk < 0)
    m3ApiReturn(__WASI_ERRNO_INVAL);

  struct timespec tp;
  if (clock_gettime(clk, &tp) != 0) {
    m3ApiReturn(errno_to_wasi(errno));
  }

  m3ApiWriteMem64(time, convert_timespec(&tp));
  m3ApiReturn(__WASI_ERRNO_SUCCESS);
}

m3ApiRawFunction(m3_wasi_unstable_proc_exit) {
  m3ApiGetArg(uint32_t, code);

  // LOG(TRACE) << "WASI m3_wasi_unstable_proc_exit";

  runtime->exit_code = code;
  m3ApiTrap(m3Err_trapExit);
}

static M3Result SuppressLookupFailure(M3Result i_result) {
  if (i_result == m3Err_functionLookupFailed)
    return m3Err_none;
  else
    return i_result;
}

M3Result m3_LinkWASI(IM3Module module) {
  M3Result result = m3Err_none;

#ifdef _WIN32
  setmode(fileno(stdin), O_BINARY);
  setmode(fileno(stdout), O_BINARY);
  setmode(fileno(stderr), O_BINARY);
#else

  // Preopen dirs
  for (int i = 3; i < PREOPEN_CNT; i++) {
    preopen[i].fd = open(preopen[i].path, O_RDONLY);
  }
#endif

  static const char *namespaces[2] = {"wasi_unstable",
                                      "wasi_snapshot_preview1"};

  for (int i = 0; i < 2; i++) {
    const char *wasi = namespaces[i];

    _(SuppressLookupFailure(m3_LinkRawFunction(
        module, wasi, "args_get", "i(**)", &m3_wasi_unstable_args_get)));
    _(SuppressLookupFailure(
        m3_LinkRawFunction(module, wasi, "args_sizes_get", "i(**)",
                           &m3_wasi_unstable_args_sizes_get)));
    _(SuppressLookupFailure(
        m3_LinkRawFunction(module, wasi, "clock_res_get", "i(i*)",
                           &m3_wasi_unstable_clock_res_get)));
    _(SuppressLookupFailure(
        m3_LinkRawFunction(module, wasi, "clock_time_get", "i(iI*)",
                           &m3_wasi_unstable_clock_time_get)));
    _(SuppressLookupFailure(m3_LinkRawFunction(
        module, wasi, "environ_get", "i(**)", &m3_wasi_unstable_environ_get)));
    _(SuppressLookupFailure(
        m3_LinkRawFunction(module, wasi, "environ_sizes_get", "i(**)",
                           &m3_wasi_unstable_environ_sizes_get)));

    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"fd_advise",            "i(iIIi)", ))); _     (SuppressLookupFailure
    //(m3_LinkRawFunction (module, wasi, "fd_allocate",          "i(iII)",  )));
    _(SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "fd_close", "i(i)",
                                               &m3_wasi_unstable_fd_close)));
    _(SuppressLookupFailure(m3_LinkRawFunction(
        module, wasi, "fd_datasync", "i(i)", &m3_wasi_unstable_fd_datasync)));
    _(SuppressLookupFailure(
        m3_LinkRawFunction(module, wasi, "fd_fdstat_get", "i(i*)",
                           &m3_wasi_unstable_fd_fdstat_get)));
    _(SuppressLookupFailure(
        m3_LinkRawFunction(module, wasi, "fd_fdstat_set_flags", "i(ii)",
                           &m3_wasi_unstable_fd_fdstat_set_flags)));
    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"fd_fdstat_set_rights", "i(iII)",  ))); _     (SuppressLookupFailure
    //(m3_LinkRawFunction (module, wasi, "fd_filestat_get",      "i(i*)",   )));
    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"fd_filestat_set_size", "i(iI)",   ))); _     (SuppressLookupFailure
    //(m3_LinkRawFunction (module, wasi, "fd_filestat_set_times","i(iIIi)", )));
    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"fd_pread",             "i(i*iI*)",)));
    _(SuppressLookupFailure(
        m3_LinkRawFunction(module, wasi, "fd_prestat_get", "i(i*)",
                           &m3_wasi_unstable_fd_prestat_get)));
    _(SuppressLookupFailure(
        m3_LinkRawFunction(module, wasi, "fd_prestat_dir_name", "i(i*i)",
                           &m3_wasi_unstable_fd_prestat_dir_name)));
    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"fd_pwrite",            "i(i*iI*)",)));
    _(SuppressLookupFailure(m3_LinkRawFunction(
        module, wasi, "fd_read", "i(i*i*)", &m3_wasi_unstable_fd_read)));
    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"fd_readdir",           "i(i*iI*)",))); _     (SuppressLookupFailure
    //(m3_LinkRawFunction (module, wasi, "fd_renumber",          "i(ii)",   )));
    _(SuppressLookupFailure(m3_LinkRawFunction(
        module, wasi, "fd_seek", "i(iIi*)", &m3_wasi_unstable_fd_seek)));
    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi, "fd_sync",
    //"i(i)",    ))); _     (SuppressLookupFailure (m3_LinkRawFunction (module,
    // wasi, "fd_tell",              "i(i*)",   )));
    _(SuppressLookupFailure(m3_LinkRawFunction(
        module, wasi, "fd_write", "i(i*i*)", &m3_wasi_unstable_fd_write)));

    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"path_create_directory",    "i(i*i)",       ))); _ (SuppressLookupFailure
    //(m3_LinkRawFunction (module, wasi, "path_filestat_get",        "i(ii*i*)",
    //&m3_wasi_unstable_path_filestat_get))); _     (SuppressLookupFailure
    //(m3_LinkRawFunction (module, wasi, "path_filestat_set_times",
    //"i(ii*iIIi)",   ))); _     (SuppressLookupFailure (m3_LinkRawFunction
    //(module, wasi, "path_link",                "i(ii*ii*i)",   )));
    _(SuppressLookupFailure(m3_LinkRawFunction(module, wasi, "path_open",
                                               "i(ii*iiIIi*)",
                                               &m3_wasi_unstable_path_open)));
    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"path_readlink",            "i(i*i*i*)",    ))); _ (SuppressLookupFailure
    //(m3_LinkRawFunction (module, wasi, "path_remove_directory",    "i(i*i)",
    //))); _     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"path_rename",              "i(i*ii*i)",    ))); _ (SuppressLookupFailure
    //(m3_LinkRawFunction (module, wasi, "path_symlink",             "i(*ii*i)",
    //))); _     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"path_unlink_file",         "i(i*i)",       )));

    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"poll_oneoff",          "i(**i*)", &m3_wasi_unstable_poll_oneoff)));
    _(SuppressLookupFailure(m3_LinkRawFunction(
        module, wasi, "proc_exit", "v(i)", &m3_wasi_unstable_proc_exit)));
    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"proc_raise",           "i(i)",    )));
    _(SuppressLookupFailure(m3_LinkRawFunction(
        module, wasi, "random_get", "i(*i)", &m3_wasi_unstable_random_get)));
    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"sched_yield",          "i()",     )));

    //_     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"sock_recv",            "i(i*ii**)",        ))); _ (SuppressLookupFailure
    //(m3_LinkRawFunction (module, wasi, "sock_send",            "i(i*ii*)",
    //))); _     (SuppressLookupFailure (m3_LinkRawFunction (module, wasi,
    //"sock_shutdown",        "i(ii)",            )));
  }

_catch:
  return result;
}
} // namespace WASI

#define CHECK_COMPOSE_ERR(_err_)                                               \
  if (_err_ != m3Err_none) {                                                   \
    std::string _errMsg("Wasm error " + std::to_string(__LINE__) + ": ");      \
    _errMsg.append(_err_);                                                     \
    throw ComposeError(_errMsg);                                               \
  }

#define CHECK_ACTIVATION_ERR(_err_)                                            \
  if (_err_ != m3Err_none) {                                                   \
    std::string _errMsg("Wasm error " + std::to_string(__LINE__) + ": ");      \
    _errMsg.append(_err_);                                                     \
    throw ActivationError(_errMsg);                                            \
  }

struct Run {
  static constexpr CBString wasmExt = ".wasm";
  static constexpr CBStrings wasmExts = {(const char **)&wasmExt, 1, 0};
  static constexpr CBTypeInfo wasmFileType{
      CBType::Path, {.path = {wasmExts, true, true, true}}};
  static inline Type WasmFilePath{wasmFileType};

  std::string _moduleName;
  std::string _moduleFileName;
  size_t _stackSize{64 * 1024};
  std::string _entryPoint{"_start"};
  ParamVar _arguments{};
  std::vector<const char *> _argsArray{};

  std::shared_ptr<M3Environment> _env;
  std::shared_ptr<M3Runtime> _runtime;
  IM3Function _mainFunc{nullptr};

  static CBTypesInfo inputTypes() { return CoreInfo::StringType; }
  static CBTypesInfo outputTypes() { return CoreInfo::StringType; }
  static inline Parameters params{
      {"Module",
       "The wasm module to run.",
       {WasmFilePath, CoreInfo::StringType}},
      {"Arguments",
       "The arguments to pass to the module main function.",
       {CoreInfo::NoneType, CoreInfo::StringSeqType,
        CoreInfo::StringVarSeqType}},
      {"EntryPoint",
       "The entry point function to call when activating.",
       {CoreInfo::StringType}},
      {"StackSize",
       "The stack size in kilobytes to use.",
       {CoreInfo::IntType}}};
  static CBParametersInfo parameters() { return params; }

  void setParam(int index, const CBVar &value) {
    switch (index) {
    case 0:
      _moduleName = value.payload.stringValue;
      break;
    case 1:
      _arguments = value;
      break;
    case 2:
      _entryPoint = value.payload.stringValue;
      break;
    case 3:
      _stackSize = size_t(value.payload.intValue * 1024);
      break;
    default:
      throw CBException("setParam out of range");
    }
  }

  CBVar getParam(int index) {
    switch (index) {
    case 0:
      return Var(_moduleName);
    case 1:
      return _arguments;
    case 2:
      return Var(_entryPoint);
    case 3:
      return Var(int64_t(_stackSize) / 1024);
    default:
      throw CBException("getParam out of range");
    }
  }

  void loadModule() {
    // here we load the module, that's why Module parameter is not variable
    std::filesystem::path p(_moduleName);
    if (p.is_absolute()) {
      if (!std::filesystem::exists(p)) {
        throw ComposeError("Wasm module not found at the given path");
      }
    } else {
      std::filesystem::path cp(Globals::RootPath);
      if (std::filesystem::exists(cp)) {
        p = cp / p;
        if (!std::filesystem::exists(p)) {
          throw ComposeError("Wasm module not found at the given path");
        }
      }
    }

    _moduleFileName = p.filename().string();

    _env.reset(m3_NewEnvironment(), &m3_FreeEnvironment);
    assert(_env.get());
    _runtime.reset(m3_NewRuntime(_env.get(), _stackSize, nullptr),
                   &m3_FreeRuntime);
    assert(_runtime.get());

    // LOG(TRACE) << "Wasm env: " << _env.get() << " runtime: " <<
    // _runtime.get();

    std::ifstream wasmFile(p.string(), std::ios::binary);
    // apparently if we use std::copy we need to make sure this is set
    wasmFile.unsetf(std::ios::skipws);
    std::vector<uint8_t> in_bytes;
    std::copy(std::istream_iterator<uint8_t>(wasmFile),
              std::istream_iterator<uint8_t>(), std::back_inserter(in_bytes));
    IM3Module pmodule;
    // LOG(TRACE) << "Calling: m3_ParseModule";
    M3Result err =
        m3_ParseModule(_env.get(), &pmodule, &in_bytes[0], in_bytes.size());
    CHECK_COMPOSE_ERR(err);

    // LOG(TRACE) << "Calling: m3_LoadModule";
    err = m3_LoadModule(_runtime.get(), pmodule);
    CHECK_COMPOSE_ERR(err);

    // LOG(TRACE) << "Calling: m3_LinkWASI";
    err = WASI::m3_LinkWASI(_runtime->modules);
    CHECK_COMPOSE_ERR(err);

    // LOG(TRACE) << "Calling: m3_LinkLibC";
    err = m3_LinkLibC(_runtime->modules);
    CHECK_COMPOSE_ERR(err);

    // LOG(TRACE) << "Calling: m3_FindFunction";
    err = m3_FindFunction(&_mainFunc, _runtime.get(), _entryPoint.c_str());
    CHECK_COMPOSE_ERR(err);
  }

  CBTypeInfo compose(const CBInstanceData &data) { return data.inputType; }

  void warmup(CBContext *context) { _arguments.warmup(context); }

  void cleanup() { _arguments.cleanup(); }

  CBVar activate(CBContext *context, const CBVar &input) {
    if (!_mainFunc) {
      // if we don't do the loadModule() here, wasm3 runtime fails to execute
      // properly wasi code (test.wasm) especially if that code is executed
      // twice in two different blocks, see wasm.clj test I am not yet sure why
      loadModule();
    }

    _argsArray.clear();

    // WASI modules need this
    if (_entryPoint == "_start") {
      _argsArray.push_back(_moduleFileName.c_str());
    }

    // add any arguments we have
    auto argsVar = _arguments.get();
    if (argsVar.valueType == Seq) {
      IterableSeq args(argsVar.payload.seqValue);
      for (auto &arg : args) {
        _argsArray.push_back(arg.payload.stringValue);
      }
    }

    auto result = m3_CallWithArgs(_mainFunc, _argsArray.size(), &_argsArray[0]);

    if (result == m3Err_trapExit) {
      if (_runtime->exit_code != 0) {
        std::string emsg("Wasm module run failed, exit code: " +
                         std::to_string(_runtime->exit_code));
        throw ActivationError(emsg);
      }
    } else {
      CHECK_ACTIVATION_ERR(result);
    }
    return input;
  }
};

void registerBlocks() { REGISTER_CBLOCK("Wasm.Run", Wasm::Run); }

} // namespace Wasm
} // namespace chainblocks