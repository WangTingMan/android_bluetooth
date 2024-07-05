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

#include "../linux_generic/reactive_semaphore.h"

#include <cerrno>
#include <string.h>

#include <functional>
#include <atomic>

#include "os/linux_generic/linux.h"
#include "os/log.h"

namespace bluetooth {
namespace os {

ReactiveSemaphore::ReactiveSemaphore( unsigned int value ) : fd_( 0 ), value_( value )
{
}

ReactiveSemaphore::~ReactiveSemaphore() {
}

void ReactiveSemaphore::Decrease() {
    value_.fetch_sub( 1 );
}

void ReactiveSemaphore::Increase()
{
    value_.fetch_add( 1 );
}

int ReactiveSemaphore::GetFd() {
  return fd_;
}

}  // namespace os
}  // namespace bluetooth
