// SPDX-License-Identifier: BSD-2-Clause
//
// This code has been produced by the European Spallation Source
// and its partner institutes under the BSD 2 Clause License.
//
// See LICENSE.md at the top level for license information.
//
// Screaming Udder!                              https://esss.se

#include "PartitionFilter.h"
#include "Kafka/PollStatus.h"

namespace Stream {

PartitionFilter::PartitionFilter(time_point StopAtTime, duration StopTimeLeeway,
                                 duration ErrorTimeOut)
    : StopTime(StopAtTime), StopLeeway(StopTimeLeeway),
      ErrorTimeOut(ErrorTimeOut) {
  // Deal with potential overflow problem
  if (time_point::max() - StopTime <= StopTimeLeeway) {
    StopTime -= StopTimeLeeway;
  }
}

bool PartitionFilter::shouldStopPartition(Kafka::PollStatus CurrentPollStatus) {
  switch (CurrentPollStatus) {
  case Kafka::PollStatus::Empty:
  case Kafka::PollStatus::Message:
    HasError = false;
    return false;
  case Kafka::PollStatus::EndOfPartition:
    HasError = false;
    return std::chrono::system_clock::now() > StopTime + StopLeeway;
  case Kafka::PollStatus::TimedOut:
  case Kafka::PollStatus::Error:
    if (not HasError) {
      HasError = true;
      ErrorTime = std::chrono::system_clock::now();
    } else if (std::chrono::system_clock::now() > ErrorTime + ErrorTimeOut) {
      return true;
    }
    break;
  }
  return false;
}

} // namespace Stream
