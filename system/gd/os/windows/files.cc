/*
 * Copyright 2020 The Android Open Source Project
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

#include "os/files.h"

#include <base/files/file_util.h>
#include <base/files/file_path.h>

#include <cerrno>
#include <cstring>
#include <fstream>
#include <streambuf>
#include <string>
#include <cstdio>

#include "os/log.h"
#include "bluetooth/log.h"

namespace bluetooth {
namespace os {

bool FileExists(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  return input.good();
}

bool RenameFile(const std::string& from, const std::string& to) {
  if( std::rename( from.c_str(), to.c_str() ) != 0 )
  {
    log::error( "unable to rename file from '{}' to '{}', error: {}", from, to, strerror( errno ) );
    return false;
  }
  return true;
}

std::optional<std::string> ReadSmallFile(const std::string& path) {
  std::ifstream input(path, std::ios::binary | std::ios::ate);
  if (!input) {
    log::warn( "Failed to open file '{}', error: {}", path, strerror( errno ) );
    return std::nullopt;
  }
  auto file_size = input.tellg();
  if (file_size < 0) {
    log::warn( "Failed to get file size for '{}', error: {}", path, strerror( errno ) );
    return std::nullopt;
  }
  std::string result(file_size, '\0');
  if (!input.seekg(0)) {
    log::warn( "Failed to go back to the beginning of file '{}', error: {}", path, strerror( errno ) );
    return std::nullopt;
  }
  if (!input.read(result.data(), result.size())) {
    log::warn( "Failed to read file '{}', error: {}", path, strerror( errno ) );
    return std::nullopt;
  }
  input.close();
  return result;
}

bool WriteToFile(const std::string& path, const std::string& data) {
  log::assert_that( !path.empty(), "assert failed: !path.empty()" );
  // Steps to ensure content of data gets to disk:
  //
  // 1) Open and write to temp file (e.g. bt_config.conf.new).
  // 2) Flush the stream buffer to the temp file.
  // 3) Sync the temp file to disk with fsync().
  // 4) Rename temp file to actual config file (e.g. bt_config.conf).
  //    This ensures atomic update.
  // 5) Sync directory that has the conf file with fsync().
  //    This ensures directory entries are up-to-date.
  //
  // We are using traditional C type file methods because C++ std::filesystem and std::ofstream do not support:
  // - Operation on directories
  // - fsync() to ensure content is written to disk

  // Build temp config file based on config file (e.g. bt_config.conf.new).
  const std::string temp_path = path + ".new";

  // Extract directory from file path (e.g. /data/misc/bluedroid).
  // libc++fs is not supported in APEX yet and hence cannot use std::filesystem::path::parent_path
  std::string directory_path;
  {
    // Make a temporary variable as inputs to dirname() will be modified and return value points to input char array
    // temp_path_for_dir must not be destroyed until results from dirname is appended to directory_path
    base::FilePath temp_path_for_dir(path);
    directory_path = temp_path_for_dir.DirName().StdStringValue();
  }

  if (directory_path.empty()) {
    log::error( "error extracting directory from '{}', error: {}", path, strerror( errno ) );
    return false;
  }

  base::WriteFile( temp_path, data.c_str(), data.size() );

  base::DeleteFile( path, false );

  base::File::Error error;
  bool r = base::ReplaceFile( base::FilePath( temp_path ), path, &error );

  if( !r )
  {
    log::error(
      "unable to commit file from '{}' to '{}', error: {}", temp_path, path, strerror( errno ) );
      return false;
  }

  return true;
}

bool RemoveFile(const std::string& path) {
  bool r = base::DeleteFile( path, false );
  if( !r )
  {
    log::error( "unable to remove file '{}', error: {}", path, strerror( errno ) );
    return false;
  }
  return true;
}

std::optional<std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>> FileCreatedTime(
    const std::string& path) {
  base::File::Info info;
  base::FilePath path_(path);
  base::GetFileInfo(path_, &info);

  using namespace std::chrono;
  using namespace std::chrono_literals;
  auto d = milliseconds{info.creation_time.ToInternalValue()};
  return time_point<system_clock>(duration_cast<system_clock::duration>(d));
}

}  // namespace os
}  // namespace bluetooth
