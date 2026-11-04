#include "StaticThreads.h"

#include "rtc_base/thread.h"
#include "call/call.h"

#include "CallSystemPolicy.h"
#include "CallsRuntimeSwitches.h"

#include <mutex>
#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <pthread.h>
#include <pthread/qos.h>
#endif
#include <algorithm>
#include <vector>

namespace tgcalls {

template <class ValueT, class CreatorT>
class Pool : public std::enable_shared_from_this<Pool<ValueT, CreatorT>> {
  struct Entry {
    std::unique_ptr<ValueT> value;
    size_t refcnt;

    bool operator < (const Entry &other) const {
      return refcnt < other.refcnt;
    }
  };

public:
  explicit Pool(CreatorT creator) : creator_(std::move(creator)) {
  }
  std::shared_ptr<ValueT> get() {
    std::unique_lock<std::mutex> lock(mutex_);
    set_pool_size_locked(1);
    auto i = std::min_element(entries_.begin(), entries_.end()) - entries_.begin();
    return std::shared_ptr<ValueT>(entries_[i].value.get(),
      [i, self = this->shared_from_this()](auto *ptr) {
        self->dec_ref(i);
      });
  }

  void set_pool_size(size_t size) {
    std::unique_lock<std::mutex> lock(mutex_);
    set_pool_size_locked(size);
  }

  void dec_ref(size_t i) {
    std::unique_lock<std::mutex> lock(mutex_);
    entries_.at(i).refcnt--;
  }

private:
  std::mutex mutex_;
  std::vector<Entry> entries_;

  CreatorT creator_;

  void set_pool_size_locked(size_t size) {
    for (size_t i = entries_.size(); i < size; i++) {
      entries_.emplace_back(Entry{creator_(i + 1), 0});
    }
  }
};

namespace {

std::mutex &registryMutex() {
  static std::mutex value;
  return value;
}

struct RegisteredThread {
  rtc::Thread *thread = nullptr;
  systempolicy::EngineThreadRole role = systempolicy::EngineThreadRole::Media;
  // True once this thread was moved to the policy class, so "off" restores the default class only for
  // threads this code changed (a thread never touched keeps whatever it was created with).
  bool qosApplied = false;
};

std::vector<RegisteredThread> &registry() {
  static std::vector<RegisteredThread> value;
  return value;
}

// The one QoS mechanism of the engine threads (both apps): role-based, read from the switch (default
// off), applied live. Called with registryMutex held.
void applyQoSToThread(RegisteredThread &entry) {
#if defined(__APPLE__) && (TARGET_OS_IOS || TARGET_OS_OSX)
  const auto qos = systempolicy::engineThreadQos(entry.role, CallsRuntimeSwitches::threadQoS());
  if (qos == systempolicy::EngineThreadQos::Unchanged && !entry.qosApplied) {
    return;
  }
  const bool raise = qos == systempolicy::EngineThreadQos::UserInitiated;
  entry.qosApplied = raise;
  entry.thread->PostTask([raise]() {
    pthread_set_qos_class_self_np(raise ? QOS_CLASS_USER_INITIATED : QOS_CLASS_DEFAULT, 0);
  });
#else
  (void)entry;
#endif
}

}

class ThreadsImpl : public Threads {
  using Thread = std::unique_ptr<rtc::Thread>;
public:
  explicit ThreadsImpl(size_t i) {
    auto suffix = i == 0 ? "" : "#" + std::to_string(i);
    media_ = create("tgc-media" + suffix);
    worker_ = create("tgc-work" + suffix);
    network_ = create_network("tgc-net" + suffix);
    media_->AllowInvokesToThread(worker_.get());
    media_->AllowInvokesToThread(network_.get());
    worker_->AllowInvokesToThread(network_.get());
      
    //network_->DisallowAllInvokes();
    //worker_->DisallowAllInvokes();
    //worker_->AllowInvokesToThread(network_.get());

    std::unique_lock<std::mutex> lock(registryMutex());
    registry().push_back({media_.get(), systempolicy::EngineThreadRole::Media});
    registry().push_back({worker_.get(), systempolicy::EngineThreadRole::Worker});
    registry().push_back({network_.get(), systempolicy::EngineThreadRole::Network});
    for (auto &entry : registry()) {
      if (entry.thread == media_.get() || entry.thread == worker_.get() || entry.thread == network_.get()) {
        applyQoSToThread(entry);
      }
    }
  }

  ~ThreadsImpl() override {
    std::unique_lock<std::mutex> lock(registryMutex());
    auto &threads = registry();
    for (auto *thread : {media_.get(), worker_.get(), network_.get()}) {
      threads.erase(std::remove_if(threads.begin(), threads.end(), [thread](const RegisteredThread &entry) {
        return entry.thread == thread;
      }), threads.end());
    }
  }

  rtc::Thread *getNetworkThread() override {
    return network_.get();
  }
  rtc::Thread *getMediaThread() override {
    return media_.get();
  }
  rtc::Thread *getWorkerThread() override {
    return worker_.get();
  }

private:
  Thread network_;
  Thread media_;
  Thread worker_;

  static Thread create(const std::string &name) {
    return init(std::unique_ptr<rtc::Thread>(rtc::Thread::Create()), name);
  }
  static Thread create_network(const std::string &name) {
    return init(std::unique_ptr<rtc::Thread>(rtc::Thread::CreateWithSocketServer()), name);
  }

  static Thread init(Thread value, const std::string &name) {
    value->SetName(name, nullptr);
    value->Start();
    return value;
  }
};

class ThreadsCreator {
public:
  std::unique_ptr<Threads> operator()(size_t i) {
   return std::make_unique<ThreadsImpl>(i);
  }
};

Pool<Threads, ThreadsCreator> &get_pool() {
  static auto pool = std::make_shared<Pool<Threads, ThreadsCreator>>(ThreadsCreator());
  return *pool;
}

void Threads::setPoolSize(size_t size){
  get_pool().set_pool_size(size);
}
std::shared_ptr<Threads> Threads::getThreads(){
  return get_pool().get();
}

namespace StaticThreads {

void applyQoS() {
  std::unique_lock<std::mutex> lock(registryMutex());
  for (auto &entry : registry()) {
    applyQoSToThread(entry);
  }
}

rtc::Thread *getNetworkThread() {
  return getThreads()->getNetworkThread();
}

rtc::Thread *getMediaThread() {
  return getThreads()->getMediaThread();
}

rtc::Thread *getWorkerThread() {
  return getThreads()->getWorkerThread();
}

std::shared_ptr<Threads> &getThreads() {
  static std::shared_ptr<Threads> threads = std::make_shared<ThreadsImpl>(0);
  return threads;
}
};

}
