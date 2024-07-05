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

#include "os/repeating_alarm.h"

#include <osi/include/alarm.h>

#include <cstring>

#include "common/bind.h"
#include "os/linux_generic/linux.h"
#include "os/log.h"
#include "os/utils.h"

namespace bluetooth {
namespace os {
using common::Closure;

RepeatingAlarm::RepeatingAlarm(Handler* handler) : handler_(handler) {
    alarm__ = alarm_new_periodic( "repeating_alarm" );
    token_ = nullptr;
}

RepeatingAlarm::~RepeatingAlarm() {
    if( alarm__ )
    {
        alarm_cancel( alarm__ );
        alarm_free( alarm__ );
        alarm__ = nullptr;
    }
}

void RepeatingAlarm::Schedule(Closure task, std::chrono::milliseconds period) {
  std::lock_guard<std::mutex> lock(mutex_);
  task_ = std::move(task);
  alarm_set( alarm__, period.count(), &RepeatingAlarm::alarm_on_fire_callback, this );
}

void RepeatingAlarm::alarm_on_fire_callback( void* data )
{
    if( !data )
    {
        return;
    }

    RepeatingAlarm* alarm_l = reinterpret_cast< RepeatingAlarm* >( data );
    alarm_l->on_fire();
}

void RepeatingAlarm::Cancel() {
  std::lock_guard<std::mutex> lock(mutex_);
  task_.Reset();
  alarm_cancel( alarm__ );
}

void RepeatingAlarm::on_fire() {
  std::unique_lock<std::mutex> lock(mutex_);
  auto task = task_;
  lock.unlock();

  if( !task.is_null() )
  {
      task.Run();
  }
}

}  // namespace os
}  // namespace bluetooth

