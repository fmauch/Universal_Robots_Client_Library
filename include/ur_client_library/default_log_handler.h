// -- BEGIN LICENSE BLOCK ----------------------------------------------
// Copyright 2021 Universal Robots A/S
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// -- END LICENSE BLOCK ------------------------------------------------

#pragma once

#include "ur_client_library/log.h"

namespace urcl
{
/*!
 * \brief LogHandler object for default handling of logging messages.
 * This class is used when no other LogHandler is registered
 */
class DefaultLogHandler : public LogHandler
{
public:
  /*!
   * \brief Construct a new DefaultLogHandler object
   */
  DefaultLogHandler();

  /*!
   * \brief Function to log a message
   *
   * \param file The log message comes from this file
   * \param line The log message comes from this line
   * \param loglevel Indicates the severity of the log message
   * \param log Log message
   */
  void log(const char* file, int line, LogLevel loglevel, const char* log) override;
};

}  // namespace urcl
