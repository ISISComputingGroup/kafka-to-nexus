// SPDX-License-Identifier: BSD-2-Clause
//
// This code has been produced by the European Spallation Source
// and its partner institutes under the BSD 2 Clause License.
//
// See LICENSE.md at the top level for license information.
//
// Screaming Udder!                              https://esss.se

#pragma once

#include "Kafka/BrokerSettings.h"
#include "TimeUtility.h"

namespace FileWriter {

/// Contains configuration parameters for the Streamer
struct StreamerOptions {
  // Amount of time between flushing of data to file.
  duration DataFlushInterval{10s};
  Kafka::BrokerSettings BrokerSettings;
  std::chrono::milliseconds StartTimestamp{0};
  time_point StopTimestamp{time_point::max()};
  std::chrono::milliseconds BeforeStartTime{1000};
  std::chrono::milliseconds AfterStopTime{1000};
};

} // namespace FileWriter
