/*
 * Copyright 2019 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <bluetooth/log.h>
#if __has_include(<unistd.h>)
#include <unistd.h>
#endif

#include <functional>
#include <mutex>
#include <queue>

#include "common/bind.h"
#include "common/callback.h"
#include "os/handler.h"
#include "os/linux_generic/reactive_semaphore.h"
#include "os/log.h"

namespace bluetooth {
namespace os {

// See documentation for |Queue|
template <typename T>
class IQueueEnqueue {
 public:
  using EnqueueCallback = common::Callback<std::unique_ptr<T>()>;
  virtual ~IQueueEnqueue() = default;
  virtual void RegisterEnqueue(Handler* handler, EnqueueCallback callback) = 0;
  virtual void UnregisterEnqueue() = 0;
};

// See documentation for |Queue|
template <typename T>
class IQueueDequeue {
 public:
  using DequeueCallback = common::Callback<void()>;
  virtual ~IQueueDequeue() = default;
  virtual void RegisterDequeue(Handler* handler, DequeueCallback callback) = 0;
  virtual void UnregisterDequeue() = 0;
  virtual std::unique_ptr<T> TryDequeue() = 0;
};

template <typename T>
class Queue : public IQueueEnqueue<T>, public IQueueDequeue<T> {
 public:
  // A function moving data from enqueue end buffer to queue, it will be continually be invoked until queue
  // is full. Enqueue end should make sure buffer isn't empty and UnregisterEnqueue when buffer become empty.
  using EnqueueCallback = common::Callback<std::unique_ptr<T>()>;
  // A function moving data form queue to dequeue end buffer, it will be continually be invoked until queue
  // is empty. TryDequeue should be use in this function to get data from queue.
  using DequeueCallback = common::Callback<void()>;
  // Create a queue with |capacity| is the maximum number of messages a queue can contain
  explicit Queue(size_t capacity);
  ~Queue();
  // Register |callback| that will be called on |handler| when the queue is able to enqueue one piece of data.
  // This will cause a crash if handler or callback has already been registered before.
  void RegisterEnqueue(Handler* handler, EnqueueCallback callback) override;
  // Unregister current EnqueueCallback from this queue, this will cause a crash if not registered yet.
  void UnregisterEnqueue() override;
  // Register |callback| that will be called on |handler| when the queue has at least one piece of data ready
  // for dequeue. This will cause a crash if handler or callback has already been registered before.
  void RegisterDequeue(Handler* handler, DequeueCallback callback) override;
  // Unregister current DequeueCallback from this queue, this will cause a crash if not registered yet.
  void UnregisterDequeue() override;

  // Try to dequeue an item from this queue. Return nullptr when there is nothing in the queue.
  std::unique_ptr<T> TryDequeue() override;

 private:
  void EnqueueCallbackInternal(EnqueueCallback callback);
#ifdef _MSC_VER
  void DequeueCallbackInternal(DequeueCallback callback);
  void HandleQueueIfNeed();
#endif
  // An internal queue that holds at most |capacity| pieces of data
  std::queue<std::unique_ptr<T>> queue_;
#ifdef _MSC_VER
  // This is the max number which queue_ can contains
  uint32_t capacity = 0;
  std::recursive_mutex mutex_;
  std::vector<EnqueueCallback> enqueue_callback_;
  DequeueCallback dequeue_callback_;
#else
  // A mutex that guards data in this queue
  std::mutex mutex_;
#endif

  class QueueEndpoint {
   public:
    explicit QueueEndpoint(unsigned int initial_value)
        : reactive_semaphore_(initial_value), handler_(nullptr), reactable_(nullptr) {}
    ReactiveSemaphore reactive_semaphore_;
    Handler* handler_;
    Reactor::Reactable* reactable_;
#ifdef _MSC_VER
    void Clear() {
      handler_ = nullptr;
    }
    explicit QueueEndpoint()
      : handler_(nullptr){}
#endif
  };

  QueueEndpoint enqueue_;
  QueueEndpoint dequeue_;
};

template <typename T>
class EnqueueBuffer {
 public:
  EnqueueBuffer(IQueueEnqueue<T>* queue) : queue_(queue) {}

  ~EnqueueBuffer() {
#ifdef _MSC_VER
    queue_->UnregisterEnqueue();
#else
    if (enqueue_registered_.exchange(false)) {
      queue_->UnregisterEnqueue();
    }
#endif
  }

  void Enqueue(std::unique_ptr<T> t, os::Handler* handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.push(std::move(t));
#ifdef _MSC_VER
    queue_->RegisterEnqueue(handler, common::Bind(&EnqueueBuffer<T>::enqueue_callback, common::Unretained(this)));
#else
    if (!enqueue_registered_.exchange(true)) {
      queue_->RegisterEnqueue(handler, common::Bind(&EnqueueBuffer<T>::enqueue_callback, common::Unretained(this)));
    }
#endif
  }

  void Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _MSC_VER
    queue_->UnregisterEnqueue();
    std::queue<std::unique_ptr<T>> empty;
    std::swap(buffer_, empty);
#else
    if (enqueue_registered_.exchange(false)) {
      queue_->UnregisterEnqueue();
      std::queue<std::unique_ptr<T>> empty;
      std::swap(buffer_, empty);
    }
#endif
  }

  auto Size() const {
    return buffer_.size();
  }

  void NotifyOnEmpty(common::OnceClosure callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    log::assert_that(callback_on_empty_.is_null(), "assert failed: callback_on_empty_.is_null()");
    callback_on_empty_ = std::move(callback);
  }

 private:
  std::unique_ptr<T> enqueue_callback() {
    std::lock_guard<std::mutex> lock(mutex_);
#ifdef _MSC_VER
    if (buffer_.empty()) {
      return nullptr;
    }
#endif
    std::unique_ptr<T> enqueued_t = std::move(buffer_.front());
    buffer_.pop();
#ifdef _MSC_VER
    if (buffer_.empty()) {
#else
    if (buffer_.empty() && enqueue_registered_.exchange(false)) {
#endif
      queue_->UnregisterEnqueue();
      if (!callback_on_empty_.is_null()) {
        std::move(callback_on_empty_).Run();
      }
    }
    return enqueued_t;
  }

  mutable std::mutex mutex_;
  IQueueEnqueue<T>* queue_;
#ifndef _MSC_VER
  std::atomic_bool enqueue_registered_ = false;
#endif
  std::queue<std::unique_ptr<T>> buffer_;
  common::OnceClosure callback_on_empty_;
};

template <typename T>
Queue<T>::Queue(size_t capacity) : enqueue_(capacity), dequeue_(0){};

template <typename T>
Queue<T>::~Queue() {
  log::assert_that(enqueue_.handler_ == nullptr, "Enqueue is not unregistered");
  log::assert_that(dequeue_.handler_ == nullptr, "Dequeue is not unregistered");
};

template <typename T>
void Queue<T>::RegisterEnqueue(Handler* handler, EnqueueCallback callback) {
#ifdef _MSC_VER
  if (handler == nullptr) {
    log::fatal("handler is null!");
    return;
  }

  if (handler->thread_ == nullptr) {
    log::fatal("handler's thread is null!");
    return;
  }

  ::bluetooth::os::Reactor* reactor_ = nullptr;
  reactor_ = handler->thread_->GetReactor();
  if (reactor_ == nullptr)
  {
    log::fatal("handler's thread's reactor is null!");
    return;
  }

  reactor_->PostTask(
    base::Bind(&Queue<T>::EnqueueCallbackInternal,
      base::Unretained(this), std::move(callback)));

  std::lock_guard lock(mutex_);
  enqueue_.handler_ = handler;
#else
  std::lock_guard<std::mutex> lock(mutex_);
  log::assert_that(enqueue_.handler_ == nullptr, "assert failed: enqueue_.handler_ == nullptr");
  log::assert_that(enqueue_.reactable_ == nullptr, "assert failed: enqueue_.reactable_ == nullptr");
  enqueue_.handler_ = handler;
  enqueue_.reactable_ = enqueue_.handler_->thread_->GetReactor()->Register(
      enqueue_.reactive_semaphore_.GetFd(),
      base::Bind(&Queue<T>::EnqueueCallbackInternal, base::Unretained(this), std::move(callback)),
      base::Closure());
#endif
}

template <typename T>
void Queue<T>::UnregisterEnqueue() {
#ifdef _MSC_VER
  std::lock_guard lock(mutex_);
  enqueue_.Clear();
  enqueue_callback_.clear();
#else
  Reactor* reactor = nullptr;
  Reactor::Reactable* to_unregister = nullptr;
  bool wait_for_unregister = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    log::assert_that(
        enqueue_.reactable_ != nullptr, "assert failed: enqueue_.reactable_ != nullptr");
    reactor = enqueue_.handler_->thread_->GetReactor();
    wait_for_unregister = (!enqueue_.handler_->thread_->IsSameThread());
    to_unregister = enqueue_.reactable_;
    enqueue_.reactable_ = nullptr;
    enqueue_.handler_ = nullptr;
  }
  reactor->Unregister(to_unregister);
  if (wait_for_unregister) {
    reactor->WaitForUnregisteredReactable(std::chrono::milliseconds(1000));
  }
#endif
}

template <typename T>
void Queue<T>::RegisterDequeue(Handler* handler, DequeueCallback callback) {
#ifdef _MSC_VER
  if (handler == nullptr) {
    log::fatal("handler is null!");
    return;
  }
  std::lock_guard lock(mutex_);
  dequeue_.handler_ = handler;
  dequeue_callback_ = callback;
#else
  std::lock_guard<std::mutex> lock(mutex_);
  log::assert_that(dequeue_.handler_ == nullptr, "assert failed: dequeue_.handler_ == nullptr");
  log::assert_that(dequeue_.reactable_ == nullptr, "assert failed: dequeue_.reactable_ == nullptr");
  dequeue_.handler_ = handler;
  dequeue_.reactable_ = dequeue_.handler_->thread_->GetReactor()->Register(
      dequeue_.reactive_semaphore_.GetFd(), callback, base::Closure());
#endif
}

template <typename T>
void Queue<T>::UnregisterDequeue() {
#ifdef _MSC_VER
  std::lock_guard lock(mutex_);
  dequeue_.Clear();
  dequeue_callback_.Reset();
#else
  Reactor* reactor = nullptr;
  Reactor::Reactable* to_unregister = nullptr;
  bool wait_for_unregister = false;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    log::assert_that(
        dequeue_.reactable_ != nullptr, "assert failed: dequeue_.reactable_ != nullptr");
    reactor = dequeue_.handler_->thread_->GetReactor();
    wait_for_unregister = (!dequeue_.handler_->thread_->IsSameThread());
    to_unregister = dequeue_.reactable_;
    dequeue_.reactable_ = nullptr;
    dequeue_.handler_ = nullptr;
  }
  reactor->Unregister(to_unregister);
  if (wait_for_unregister) {
    reactor->WaitForUnregisteredReactable(std::chrono::milliseconds(1000));
  }
#endif
}

template <typename T>
std::unique_ptr<T> Queue<T>::TryDequeue() {
#ifdef _MSC_VER
  std::lock_guard lock(mutex_);
  if (queue_.empty()) {
    return nullptr;
  }
  auto data = std::move(queue_.front());
  queue_.pop();
  HandleQueueIfNeed();
  return data;
#else
  std::lock_guard<std::mutex> lock(mutex_);

  if (queue_.empty()) {
    return nullptr;
  }

  dequeue_.reactive_semaphore_.Decrease();

  std::unique_ptr<T> data = std::move(queue_.front());
  queue_.pop();

  enqueue_.reactive_semaphore_.Increase();

  return data;
#endif
}

#ifdef _MSC_VER
template <typename T>
void Queue<T>::HandleQueueIfNeed()
{
  std::lock_guard lock(mutex_);
  if (dequeue_callback_)
  {
    dequeue_.handler_->thread_->GetReactor()->PostTask(
      base::Bind(&Queue<T>::DequeueCallbackInternal,
        base::Unretained(this), dequeue_callback_));
  }

  if (!enqueue_callback_.empty())
  {
    auto callback = std::move(enqueue_callback_.front());
    enqueue_callback_.erase(enqueue_callback_.begin());
    enqueue_.handler_->thread_->GetReactor()->PostTask(
      base::Bind(&Queue<T>::EnqueueCallbackInternal,
        base::Unretained(this), std::move(callback)));
  }
}
#endif

template <typename T>
void Queue<T>::EnqueueCallbackInternal(EnqueueCallback callback) {
#ifdef _MSC_VER
  auto data = callback.Run();
  if (!data) {
    return;
  }

  std::lock_guard lock(mutex_);
  queue_.push(std::move(data));
  if (dequeue_.handler_ == nullptr) {
    log::fatal("did no register dequeue handler!");
    return;
  }

  if (dequeue_callback_.is_null()) {
    log::fatal("did no register dequeue callback!");
    return;
  }
  dequeue_.handler_->thread_->GetReactor()->PostTask(dequeue_callback_);
#else
  std::unique_ptr<T> data = callback.Run();
  log::assert_that(data != nullptr, "assert failed: data != nullptr");
  std::lock_guard<std::mutex> lock(mutex_);
  enqueue_.reactive_semaphore_.Decrease();
  queue_.push(std::move(data));
  dequeue_.reactive_semaphore_.Increase();
#endif
}

#ifdef _MSC_VER
template <typename T>
void Queue<T>::DequeueCallbackInternal(DequeueCallback callback)
{
  std::lock_guard lock(mutex_);
  if (!queue_.empty()) {
    callback.Run();
  }
}
#endif

}  // namespace os
}  // namespace bluetooth
