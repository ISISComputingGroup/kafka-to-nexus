// SPDX-License-Identifier: BSD-2-Clause
//
// This code has been produced by the European Spallation Source
// and its partner institutes under the BSD 2 Clause License.
//
// See LICENSE.md at the top level for license information.
//
// Screaming Udder!                              https://esss.se

#pragma once

#include "InternalMetric.h"
#include "Sink.h"
#include <asio.hpp>
#include <map>
#include <memory>
#include <thread>

namespace Metrics {

class Reporter {
public:
  Reporter(std::unique_ptr<Sink> MetricSink, std::chrono::milliseconds Interval)
      : MetricSink(std::move(MetricSink)), IO(), Period(Interval),
        AsioTimer(IO, Period){};

  virtual ~Reporter() = default;
  void reportMetrics();
  virtual bool addMetric(Metric &NewMetric, std::string const &NewName);
  virtual bool tryRemoveMetric(std::string const &MetricName);
  LogTo getSinkType();
  void start();
  void waitForStop();

private:
  void run() { IO.run(); }

  std::unique_ptr<Sink> MetricSink;
  std::mutex MetricsMapMutex; // lock when accessing MetricToReportOn
  std::map<std::string, InternalMetric> MetricsToReportOn; // MetricName: Metric
  asio::io_context IO;
  std::chrono::milliseconds Period;
  asio::steady_timer AsioTimer;
  std::thread ReporterThread;
};
}
