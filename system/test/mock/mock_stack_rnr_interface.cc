/*
 * Copyright 2024 The Android Open Source Project
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
<<<<<<< HEAD:system/osi/src/thread_scheduler.cc
#if __has_include(<sched.h>)
#include <sched.h>
#endif
#include <sys/types.h>
=======

#include "test/mock/mock_stack_rnr_interface.h"

using ::testing::StrictMock;
>>>>>>> 5323f31677:system/test/mock/mock_stack_rnr_interface.cc

#include <cutils/threads.h>
#include <utils/Timers.h>

namespace {
StrictMock<bluetooth::testing::stack::rnr::Mock> default_interface;
bluetooth::stack::rnr::Interface* interface_ = &default_interface;
}  // namespace

void bluetooth::testing::stack::rnr::reset_interface() { interface_ = &default_interface; }
void bluetooth::testing::stack::rnr::set_interface(bluetooth::stack::rnr::Interface* interface) {
  interface_ = interface;
}

bluetooth::stack::rnr::Interface& get_stack_rnr_interface() { return *interface_; }
