// Code in the spsc_sema namespace below is an adaptation of Jeff Preshing's
// portable + lightweight semaphore implementations, originally from
// https://github.com/preshing/cpp11-on-multicore/blob/master/common/sema.h
// LICENSE:
// Copyright (c) 2015 Jeff Preshing
//
// This software is provided 'as-is', without any express or implied
// warranty. In no event will the authors be held liable for any damages
// arising from the use of this software.
//
// Permission is granted to anyone to use this software for any purpose,
// including commercial applications, and to alter it and redistribute it
// freely, subject to the following restrictions:
//
// 1. The origin of this software must not be misrepresented; you must not
//    claim that you wrote the original software. If you use this software
//    in a product, an acknowledgement in the product documentation would be
//    appreciated but is not required.
// 2. Altered source versions must be plainly marked as such, and must not be
//    misrepresented as being the original software.
// 3. This notice may not be removed or altered from any source distribution.

#pragma once

#include "atomicops.h"

#if defined(_WIN32)
// Avoid including windows.h in a header; we only need a handful of
// items, so we'll redeclare them here (this is relatively safe since
// the API generally has to remain stable between Windows versions).
// I know this is an ugly hack but it still beats polluting the global
// namespace with thousands of generic names or adding a .cpp for nothing.
extern "C" {
struct _SECURITY_ATTRIBUTES;
__declspec(dllimport) void* __stdcall CreateSemaphoreW(_SECURITY_ATTRIBUTES* lpSemaphoreAttributes, long lInitialCount,
                                                       long lMaximumCount, const wchar_t* lpName);
__declspec(dllimport) int __stdcall CloseHandle(void* hObject);
__declspec(dllimport) unsigned long __stdcall WaitForSingleObject(void* hHandle, unsigned long dwMilliseconds);
__declspec(dllimport) int __stdcall ReleaseSemaphore(void* hSemaphore, long lReleaseCount, long* lpPreviousCount);
}
#elif defined(__MACH__)
#include <mach/mach.h>
#elif defined(__unix__)
#include <semaphore.h>
#endif

namespace moodycamel
{
namespace spsc_sema
{
#if defined(_WIN32)
class Semaphore
{
private:
  void* m_hSema;

  Semaphore(const Semaphore& other);
  Semaphore& operator=(const Semaphore& other);

public:
  Semaphore(int initialCount = 0)
  {
    assert(initialCount >= 0);
    const long maxLong = 0x7fffffff;
    m_hSema = CreateSemaphoreW(nullptr, initialCount, maxLong, nullptr);
  }

  ~Semaphore()
  {
    CloseHandle(m_hSema);
  }

  void wait()
  {
    const unsigned long infinite = 0xffffffff;
    WaitForSingleObject(m_hSema, infinite);
  }

  bool tryWait()
  {
    const unsigned long RC_WAIT_TIMEOUT = 0x00000102;
    return WaitForSingleObject(m_hSema, 0) != RC_WAIT_TIMEOUT;
  }

  bool timedWait(std::uint64_t usecs)
  {
    const unsigned long RC_WAIT_TIMEOUT = 0x00000102;
    return WaitForSingleObject(m_hSema, (unsigned long)(usecs / 1000)) != RC_WAIT_TIMEOUT;
  }

  void signal(int count = 1)
  {
    ReleaseSemaphore(m_hSema, count, nullptr);
  }
};
#elif defined(__MACH__)
//---------------------------------------------------------
// Semaphore (Apple iOS and OSX)
// Can't use POSIX semaphores due to https://lists.apple.com/archives/darwin-kernel/2009/Apr/msg00010.html
//---------------------------------------------------------
class Semaphore
{
private:
  semaphore_t sema_;

  Semaphore(const Semaphore& other);
  Semaphore& operator=(const Semaphore& other);

public:
  Semaphore(int initialCount = 0)
  {
    assert(initialCount >= 0);
    semaphore_create(mach_task_self(), &sema_, SYNC_POLICY_FIFO, initialCount);
  }

  ~Semaphore()
  {
    semaphore_destroy(mach_task_self(), sema_);
  }

  void wait()
  {
    semaphore_wait(sema_);
  }

  bool tryWait()
  {
    return timedWait(0);
  }

  bool timedWait(std::int64_t timeout_usecs)
  {
    mach_timespec_t ts;
    ts.tv_sec = timeout_usecs / 1000000;
    ts.tv_nsec = (timeout_usecs % 1000000) * 1000;

    // added in OSX 10.10:
    // https://developer.apple.com/library/prerelease/mac/documentation/General/Reference/APIDiffsMacOSX10_10SeedDiff/modules/Darwin.html
    kern_return_t rc = semaphore_timedwait(sema_, ts);

    return rc != KERN_OPERATION_TIMED_OUT;
  }

  void signal()
  {
    semaphore_signal(sema_);
  }

  void signal(int count)
  {
    while (count-- > 0)
    {
      semaphore_signal(sema_);
    }
  }
};
#elif defined(__unix__)
//---------------------------------------------------------
// Semaphore (POSIX, Linux)
//---------------------------------------------------------
class Semaphore
{
private:
  sem_t sema_;

  Semaphore(const Semaphore& other);
  Semaphore& operator=(const Semaphore& other);

public:
  Semaphore(int initialCount = 0)
  {
    assert(initialCount >= 0);
    sem_init(&sema_, 0, initialCount);
  }

  ~Semaphore()
  {
    sem_destroy(&sema_);
  }

  void wait()
  {
    // http://stackoverflow.com/questions/2013181/gdb-causes-sem-wait-to-fail-with-eintr-error
    int rc;
    do
    {
      rc = sem_wait(&sema_);
    } while (rc == -1 && errno == EINTR);
  }

  bool tryWait()
  {
    int rc;
    do
    {
      rc = sem_trywait(&sema_);
    } while (rc == -1 && errno == EINTR);
    return !(rc == -1 && errno == EAGAIN);
  }

  bool timedWait(std::uint64_t usecs)
  {
    struct timespec ts;
    const int usecs_in_1_sec = 1000000;
    const int nsecs_in_1_sec = 1000000000;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += usecs / usecs_in_1_sec;
    ts.tv_nsec += (usecs % usecs_in_1_sec) * 1000;
    // sem_timedwait bombs if you have more than 1e9 in tv_nsec
    // so we have to clean things up before passing it in
    if (ts.tv_nsec > nsecs_in_1_sec)
    {
      ts.tv_nsec -= nsecs_in_1_sec;
      ++ts.tv_sec;
    }

    int rc;
    do
    {
      rc = sem_timedwait(&sema_, &ts);
    } while (rc == -1 && errno == EINTR);
    return !(rc == -1 && errno == ETIMEDOUT);
  }

  void signal()
  {
    sem_post(&sema_);
  }

  void signal(int count)
  {
    while (count-- > 0)
    {
      sem_post(&sema_);
    }
  }
};
#else
#error Unsupported platform! (No semaphore wrapper available)
#endif

//---------------------------------------------------------
// LightweightSemaphore
//---------------------------------------------------------
class LightweightSemaphore
{
public:
  typedef std::make_signed<std::size_t>::type ssize_t;

private:
  WeakAtomic<ssize_t> count_;
  Semaphore sema_;

  bool waitWithPartialSpinning(std::int64_t timeout_usecs = -1)
  {
    ssize_t old_count;
    // Is there a better way to set the initial spin count?
    // If we lower it to 1000, testBenaphore becomes 15x slower on my Core i7-5930K Windows PC,
    // as threads start hitting the kernel semaphore.
    int spin = 10000;
    while (--spin >= 0)
    {
      if (count_.load() > 0)
      {
        count_.fetchAddAcquire(-1);
        return true;
      }
      compilerFence(memory_order_acquire);  // Prevent the compiler from collapsing the loop.
    }
    old_count = count_.fetchAddAcquire(-1);
    if (old_count > 0)
      return true;
    if (timeout_usecs < 0)
    {
      sema_.wait();
      return true;
    }
    if (sema_.timedWait(timeout_usecs))
      return true;
    // At this point, we've timed out waiting for the semaphore, but the
    // count is still decremented indicating we may still be waiting on
    // it. So we have to re-adjust the count, but only if the semaphore
    // wasn't signaled enough times for us too since then. If it was, we
    // need to release the semaphore too.
    while (true)
    {
      old_count = count_.fetchAddRelease(1);
      if (old_count < 0)
        return false;  // successfully restored things to the way they were
      // Oh, the producer thread just signaled the semaphore after all. Try again:
      old_count = count_.fetchAddAcquire(-1);
      if (old_count > 0 && sema_.tryWait())
        return true;
    }
  }

public:
  LightweightSemaphore(ssize_t initialCount = 0) : count_(initialCount)
  {
    assert(initialCount >= 0);
  }

  bool tryWait()
  {
    if (count_.load() > 0)
    {
      count_.fetchAddAcquire(-1);
      return true;
    }
    return false;
  }

  void wait()
  {
    if (!tryWait())
      waitWithPartialSpinning();
  }

  bool wait(std::int64_t timeout_usecs)
  {
    return tryWait() || waitWithPartialSpinning(timeout_usecs);
  }

  void signal(ssize_t count = 1)
  {
    assert(count >= 0);
    ssize_t old_count = count_.fetchAddRelease(count);
    assert(old_count >= -1);
    if (old_count < 0)
    {
      sema_.signal(1);
    }
  }

  ssize_t availableApprox() const
  {
    ssize_t count = count_.load();
    return count > 0 ? count : 0;
  }
};
}  // end namespace spsc_sema
}  // end namespace moodycamel

#if defined(AE_VCPP) && (_MSC_VER < 1700 || defined(__cplusplus_cli))
#pragma warning(pop)
#ifdef __cplusplus_cli
#pragma managed(pop)
#endif
#endif
