#pragma once

#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <algorithm>
#include <atomic>
#include <exception>
#include <queue>
#include <thread>

#include "FileWriterTask.h"
#include "Report.hpp"
#include "logger.h"

namespace KafkaW {
class ProducerTopic;
}

namespace FileWriter {

template <typename Streamer> class StreamMaster {
  using SEC = Status::StreamerErrorCode;
  using SMEC = Status::StreamMasterErrorCode;
  using Options = typename Streamer::Options;
  friend class CommandHandler;

public:
  StreamMaster() {}

  StreamMaster(const std::string &broker,
               std::unique_ptr<FileWriterTask> file_writer_task,
               const Options &kafka_options = {},
               const Options &filewriter_options = {})
      : demux(file_writer_task->demuxers()),
        _file_writer_task(std::move(file_writer_task)) {

    for (auto &d : demux) {
      streamer.emplace(std::piecewise_construct,
                       std::forward_as_tuple(d.topic()),
                       std::forward_as_tuple(broker, d.topic(), kafka_options,
                                             filewriter_options));
      streamer[d.topic()].n_sources() = d.sources().size();
      streamer[d.topic()].set_start_time(d);
    }

  }

  StreamMaster(const StreamMaster &) = delete;
  StreamMaster(StreamMaster &&) = default;

  ~StreamMaster() {
    stop_ = true;
    if (loop.joinable()) {
      loop.join();
    }
    if (report_thread_.joinable()) {
      report_thread_.join();
    }
  }

  StreamMaster &operator=(const StreamMaster &) = delete;

  bool stop_time(const ESSTimeStamp &stop) {
    for (auto &d : demux) {
      d.stop_time() = stop;
    }
    return true;
  }

  bool start() {
    LOG(Sev::Info, "StreamMaster: start");
    do_write = true;
    stop_ = false;

    if (!loop.joinable()) {
      loop = std::thread([&] { this->run(); });
      std::this_thread::sleep_for(milliseconds(100));
    }
    return loop.joinable();
  }

  bool stop() {
    try {
      std::call_once(stop_once_guard,
                     &FileWriter::StreamMaster<Streamer>::stop_impl, this);
    } catch (std::exception &e) {
      LOG(Sev::Warning, "Error while stopping: {}", e.what());
    }
    return !(loop.joinable() || report_thread_.joinable());
  }

  bool stop(const std::string &job_id) {
    if (job_id == _file_writer_task->job_id()) {
      return stop();
    }
    return false;
  }

  void report(std::shared_ptr<KafkaW::ProducerTopic> p,
              const milliseconds &report_ms = milliseconds{1000}) {
    if (report_ms.count() < 0) {
      LOG(Sev::Warning, "Required negative delay in statistics collection: use default");
      return report(p);
    }
    if (!report_thread_.joinable()) {
      report_.reset(new Report(p, report_ms));
      report_thread_ =
          std::thread([&] { report_->report(streamer, stop_, runstatus); });
    } else {
      LOG(Sev::Debug, "Status report already started, nothing to do");
    }
  }

  FileWriterTask const &file_writer_task() const { return *_file_writer_task; }

  const SMEC status() {
    for (auto &s : streamer) {
      if (int(s.second.runstatus()) < 0) {
        runstatus = SMEC::streamer_error;
      }
    }
    return runstatus.load();
  }

  std::string job_id() const { return _file_writer_task->job_id(); }

private:
  void run() {
    using namespace std::chrono;
    runstatus = SMEC::running;

    while (!stop_ && demux.size() > 0) {
      for (auto &d : demux) {
        auto &s = streamer[d.topic()];
        if (s.runstatus() == SEC::writing) {
          auto tp = system_clock::now();
          while (do_write &&
                 ((system_clock::now() - tp) < topic_write_duration)) {
            auto value = s.write(d);
            if (value.is_STOP() &&
                (remove_source(d.topic()) != SMEC::running)) {
              break;
            }
          }
          continue;
        }
        if (s.runstatus() == SEC::has_finished) {
          if (remove_source(d.topic()) != SMEC::running) {
            break;
          }
          continue;
        }
        if (s.runstatus() == SEC::not_initialized) {
          if (streamer.size() == 1) {
            std::this_thread::sleep_for(milliseconds(500));
          }
          continue;
        }
        if (int(s.runstatus()) < 0) {
          LOG(Sev::Error, "Error in topic {} : {}", d.topic(),
              int(s.runstatus()));
          if (remove_source(d.topic()) != SMEC::running) {
            break;
          }
          continue;
        }
      }
    }

    runstatus = SMEC::has_finished;
  }

  SMEC remove_source(const std::string &topic) {
    auto &s = streamer[topic];
    if (s.n_sources() > 1) {
      s.n_sources()--;
      return SMEC::running;
    } else {
      LOG(Sev::Debug, "All sources in {} have expired, remove streamer", topic);
      s.close_stream();
      streamer.erase(topic);
      if (streamer.size() != 0) {
        return SMEC::empty_streamer;
      } else {
        stop_ = true;
        return runstatus = SMEC::has_finished;
      }
    }
  }

  void stop_impl() {
    LOG(Sev::Info, "StreamMaster: stop");
    do_write = false;
    stop_ = true;
    if (loop.joinable()) {
      loop.join();
    }
    if (report_thread_.joinable()) {
      report_thread_.join();
    }
    for (auto &s : streamer) {
      LOG(Sev::Info, "Shut down {} : {}", s.first);
      auto v = s.second.close_stream();
      if (v != SEC::has_finished) {
        LOG(Sev::Warning, "Error while stopping {} : {}", s.first,
            Status::Err2Str(v));
      } else {
        LOG(Sev::Info, "\t...done");
      }
    }
    streamer.clear();
  }

  std::map<std::string, Streamer> streamer;
  std::vector<DemuxTopic> &demux;
  std::thread loop;
  std::thread report_thread_;
  std::atomic<SMEC> runstatus{SMEC::not_started};
  std::atomic<bool> do_write{false};
  std::atomic<bool> stop_{false};
  std::unique_ptr<FileWriterTask> _file_writer_task{nullptr};
  std::once_flag stop_once_guard;
  std::unique_ptr<Report> report_{nullptr};

  milliseconds topic_write_duration{1000};
};

} // namespace FileWriter
