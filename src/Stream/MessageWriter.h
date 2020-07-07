// SPDX-License-Identifier: BSD-2-Clause
//
// This code has been produced by the European Spallation Source
// and its partner institutes under the BSD 2 Clause License.
//
// See LICENSE.md at the top level for license information.
//
// Screaming Udder!                              https://esss.se

/// \brief Do the actual file writing.
///

#pragma once

#include "Message.h"
#include "Metrics/Metric.h"
#include "Metrics/Registrar.h"
#include <concurrentqueue/concurrentqueue.h>
#include "logger.h"
#include "TimeUtility.h"
#include <map>
#include <thread>

namespace WriterModule {
class Base;
}

namespace Stream {

class MessageWriter {
public:
  explicit MessageWriter(duration FlushIntervalTime, Metrics::Registrar const &MetricReg);

  virtual ~MessageWriter();

  virtual void addMessage(Message const &Msg);

  using ModuleHash = size_t;

  auto nrOfWritesDone() const { return int64_t(WritesDone); };
  auto nrOfWriteErrors() const { return int64_t(WriteErrors); };
  auto nrOfWriterModulesWithErrors() const {
    return ModuleErrorCounters.size();
  }

protected:
  virtual void writeMsgImpl(WriterModule::Base *ModulePtr,
                            FileWriter::FlatbufferMessage const &Msg);
  virtual void threadFunction();

  virtual void flushData() {};

  SharedLogger Log{getLogger()};
  Metrics::Metric WritesDone{"writes_done",
                             "Number of completed writes to HDF file."};
  Metrics::Metric WriteErrors{"write_errors",
                              "Number of failed HDF file writes.",
                              Metrics::Severity::ERROR};
  std::map<ModuleHash, std::unique_ptr<Metrics::Metric>> ModuleErrorCounters;
  Metrics::Registrar Registrar;

  using JobType = std::function<void()>;
  moodycamel::ConcurrentQueue<JobType> WriteJobs;
  std::thread WriterThread;
  std::atomic_bool RunThread{true};
  const duration SleepTime{10ms};
  duration FlushInterval{1s};
  const int MaxTimeCheckCounter{200};
};

} // namespace Stream
