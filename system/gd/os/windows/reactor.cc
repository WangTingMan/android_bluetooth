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

#include "os/reactor.h"

#include <fcntl.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cinttypes>
#include <cstring>
#include <vector>

#include "os/log.h"
#include "bluetooth/log.h"

#include <base/bind.h>

#define READ_READY 0x01
#define WRITE_READY 0x02
#define READ_WRITE_READY 0x03

namespace {

// Use at most sizeof(epoll_event) * kEpollMaxEvents kernel memory
constexpr int kEpollMaxEvents = 64;
constexpr uint64_t kStopReactor = 1 << 0;
constexpr uint64_t kWaitForIdle = 1 << 1;

}  // namespace

static void empty_function() {}

namespace bluetooth {
namespace os {
using common::Closure;

static std::atomic_int32_t event_impl_count = 1;

struct Reactor::Event::impl {

  impl() {
  }

  ~impl() {
  }

  int event_id = 0;
  std::mutex mutex_;
  std::vector<int> received_values_;
};

Reactor::Event::Event() : pimpl_(new impl()) {}

Reactor::Event::~Event() {
  delete pimpl_;
  pimpl_ = nullptr;
}

bool Reactor::Event::Read() {
  int val = 0;
  std::lock_guard<std::mutex> locker(pimpl_->mutex_);
  if( !pimpl_->received_values_.empty() ) {
    val = pimpl_->received_values_.front();
    pimpl_->received_values_.erase(pimpl_->received_values_.begin());
    return true;
  }
  return false;
}

int Reactor::Event::Id() const
{
  return pimpl_->event_id;
}

void Reactor::Event::Clear() {
  std::lock_guard<std::mutex> locker(pimpl_->mutex_);
  pimpl_->received_values_.clear();
}

void Reactor::Event::Close() {
    Clear();
}

void Reactor::Event::Notify() {
  std::lock_guard<std::mutex> locker(pimpl_->mutex_);
  pimpl_->received_values_.push_back(1);
}

class Reactor::Reactable {
 public:
  Reactable(Closure on_read_ready, Closure on_write_ready)
      : on_read_ready_(std::move(on_read_ready)),
        on_write_ready_(std::move(on_write_ready)),
        is_executing_(false),
        removed_(false) {}

  Reactable() {}

  Closure on_read_ready_;
  Closure on_write_ready_;
  bool is_executing_;
  bool removed_;
  uint32_t poll_event_type_ = 0;
  bool stop_reactor = false;
  std::mutex mutex_;
  std::unique_ptr<std::promise<void>> finished_promise_;
};

Reactor::Reactor() : epoll_fd_(0), control_fd_(0), is_running_(false) {
}

Reactor::~Reactor() {
}

void Reactor::PostTask(common::OnceClosure closure)
{
    std::lock_guard<std::mutex> locker(task_mutex);
    tasks_to_do.emplace_back(std::move(closure));
    task_condition_variable.notify_all();
}

void Reactor::Run()
{
    bool already_running = is_running_.exchange( true );
    log::assert_that( !already_running, "assert failed: !already_running" );

    std::list<common::OnceClosure> tasks_to_execute;
    std::unique_lock<std::mutex> locker(task_mutex, std::defer_lock);

    while (true) {
      locker.lock();

      if( to_quit_ ) {
        is_running_ = false;
        return;
      }

      tasks_to_execute = std::move(tasks_to_do);
      if( tasks_to_execute.empty()) {
        task_condition_variable.wait(
            locker, [this]()->bool { return !tasks_to_do.empty(); });
        if(tasks_to_do.empty()) {
            locker.unlock();
            continue;
        }
        else {
            tasks_to_execute = std::move(tasks_to_do);
        }
      }

      locker.unlock();

      for (auto it = tasks_to_execute.begin(); it != tasks_to_execute.end(); ++it)
      {
         if( to_quit_ ) {
            is_running_ = false;
            return;
         }

         std::move(*it).Run();
      }
    }
}

void Reactor::Stop() {
  if (!is_running_) {
    log::warn( "not running, will stop once it's started" );
  }

  to_quit_.store(true);
  PostTask(base::BindOnce(&empty_function));
}

std::unique_ptr<Reactor::Event> Reactor::NewEvent() const {
  return std::make_unique<Reactor::Event>();
}

Reactor::Reactable* Reactor::Register( int fd, Closure on_read_ready, Closure on_write_ready) {

  std::shared_ptr<Reactable> react = std::make_shared<Reactable>( on_read_ready, on_write_ready );

  uint32_t poll_event_type = 0;
  if (!on_read_ready.is_null()) {
    poll_event_type |= READ_READY;
  }
  if (!on_write_ready.is_null()) {
    poll_event_type |= WRITE_READY;
  }
  react->poll_event_type_ = poll_event_type;

  std::lock_guard<std::recursive_mutex> locker( mutex_ );
  validation_list_.push_back( react );
  condition_vairable_.notify_all();
  return react.get();
}

void Reactor::Unregister( Reactor::Reactable* reactable) {
  log::assert_that( reactable != nullptr, "assert failed: reactable != nullptr" );

  std::unique_lock<std::recursive_mutex> lock(mutex_);

  for( auto it = validation_list_.begin(); it != validation_list_.end(); )
  {
      if( (*it).get() == reactable ) {
          invalidation_list_.push_back((*it));
          it = validation_list_.erase( it );
      }
      else
      {
          ++it;
      }
  }

  lock.unlock();

  {
    std::lock_guard<std::mutex> reactable_lock( reactable->mutex_ );
    // If we are unregistering during the callback event from this reactable, we delete it after the callback is
    // executed. reactable->is_executing_ is protected by reactable->mutex_, so it's thread safe.
    if (reactable->is_executing_) {
      reactable->removed_ = true;
      reactable->finished_promise_ = std::make_unique<std::promise<void>>();
      executing_reactable_finished_ = std::make_shared<std::future<void>>(reactable->finished_promise_->get_future());
    }
  }

}

bool Reactor::WaitForUnregisteredReactable(std::chrono::milliseconds timeout) {
  std::lock_guard<std::recursive_mutex> lock(mutex_);
  if (executing_reactable_finished_ == nullptr) {
    return true;
  }
  auto stop_status = executing_reactable_finished_->wait_for(timeout);
  if (stop_status != std::future_status::ready) {
    log::error( "Unregister reactable timed out" );
  }
  return stop_status == std::future_status::ready;
}

bool Reactor::WaitForIdle(std::chrono::milliseconds timeout) {
  auto promise = std::make_shared<std::promise<void>>();
  auto future = std::make_unique<std::future<void>>(promise->get_future());
  {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    idle_promise_ = promise;
  }

  auto idle_status = future->wait_for(timeout);
  return idle_status == std::future_status::ready;
}

void Reactor::ModifyRegistration(Reactor::Reactable* reactable, common::Closure on_read_ready, common::Closure on_write_ready) {
  log::assert_that( reactable != nullptr, "assert failed: reactable != nullptr" );

  uint32_t poll_event_type = 0;
  if (!on_read_ready.is_null()) {
    poll_event_type |= READ_READY;
  }
  if (!on_write_ready.is_null()) {
    poll_event_type |= WRITE_READY;
  }

  std::unique_lock<std::recursive_mutex> lock( mutex_ );
  for( auto it = validation_list_.begin(); it != validation_list_.end(); ++it )
  {
      if( (*it).get() == reactable )
      {
          reactable->on_read_ready_ = std::move( on_read_ready );
          reactable->on_write_ready_ = std::move( on_write_ready );
          reactable->poll_event_type_ = poll_event_type;
          break;
      }
  }
}

}  // namespace os
}  // namespace bluetooth
