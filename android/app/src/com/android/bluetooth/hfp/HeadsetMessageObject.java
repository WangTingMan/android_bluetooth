/*
 * Copyright 2018 The Android Open Source Project
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
package com.android.bluetooth.hfp;

import android.os.Message;

/** An object passed through {@link Message#obj} on state machines */
public abstract class HeadsetMessageObject {
    /**
     * Build a representation of this object in a {@link StringBuilder}
     *
     * @param builder {@link StringBuilder} provided to build the string
     */
    public abstract void buildString(StringBuilder builder);

    @Override
    public String toString() {
        StringBuilder builder = new StringBuilder();
        buildString(builder);
        return builder.toString();
    }
}
