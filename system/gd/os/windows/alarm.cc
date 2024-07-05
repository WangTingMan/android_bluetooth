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

#include "os/alarm.h"

#include <cstring>

#include "common/bind.h"
#include "os/linux_generic/linux.h"
#include "os/log.h"
#include "os/utils.h"

#include <osi/include/alarm.h>

namespace bluetooth {
namespace os {
using common::Closure;
using common::OnceClosure;

Alarm::Alarm( Handler* handler )
    : handler_( handler )
    , alarm_( nullptr )
    , token_( nullptr )
{
    alarm_ = alarm_new( "gd_alarm" );
}

Alarm::~Alarm() {
    alarm_free( alarm_ );
    alarm_ = nullptr;
}

void Alarm::Schedule(OnceClosure task, std::chrono::milliseconds delay)
{
  std::unique_lock<std::mutex> lock(mutex_);
  task_ = std::move(task);
  lock.unlock();

  alarm_set( alarm_, delay.count(), &Alarm::alarm_on_fire_callback, this );
}

void Alarm::alarm_on_fire_callback( void* data )
{
    if( data )
    {
        Alarm* instance_ = reinterpret_cast< Alarm* >( data );
        instance_->on_fire();
    }
}

void Alarm::Cancel()
{
  alarm_cancel( alarm_ );
  std::lock_guard<std::mutex> lock(mutex_);
  task_.Reset();
}

void Alarm::on_fire() {
  std::unique_lock<std::mutex> lock(mutex_);
  auto task = std::move(task_);
  lock.unlock();

  if( task.is_null() )
  {
      return;
  }

  handler_->Post(std::move(task));
}

}  // namespace os
}  // namespace bluetooth

