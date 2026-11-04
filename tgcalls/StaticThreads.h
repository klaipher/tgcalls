#pragma once

#include <cstddef>
#include <memory>

namespace rtc {
class Thread;
}

namespace tgcalls {

class Threads {
public:
  virtual ~Threads() = default;
  virtual rtc::Thread *getNetworkThread() = 0;
  virtual rtc::Thread *getMediaThread() = 0;
  virtual rtc::Thread *getWorkerThread() = 0;

  // it is not possible to decrease pool size
  static void setPoolSize(size_t size);
  static std::shared_ptr<Threads> getThreads();
};

namespace StaticThreads {

// Apple only: applies or restores user-initiated QoS on every live tgcalls thread, following CallsRuntimeSwitches::threadQoS() (the one QoS mechanism for both apps).
void applyQoS();
rtc::Thread *getNetworkThread();
rtc::Thread *getMediaThread();
rtc::Thread *getWorkerThread();
std::shared_ptr<Threads> &getThreads();
}

};
