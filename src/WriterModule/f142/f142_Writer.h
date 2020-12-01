// SPDX-License-Identifier: BSD-2-Clause
//
// This code has been produced by the European Spallation Source
// and its partner institutes under the BSD 2 Clause License.
//
// See LICENSE.md at the top level for license information.
//
// Screaming Udder!                              https://esss.se

#pragma once

#include "FlatbufferMessage.h"
#include "WriterModuleBase.h"
#include "WriterModuleConfig/Field.h"
#include <NeXusDataset/EpicsAlarmDatasets.h>
#include <NeXusDataset/NeXusDataset.h>
#include <array>
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <vector>

namespace WriterModule {
namespace f142 {
using FlatbufferMessage = FileWriter::FlatbufferMessage;

class f142_Writer : public WriterModule::Base {
public:
  /// Implements writer module interface.
  InitResult init_hdf(hdf5::node::Group &HDFGroup,
                      std::string const &HDFAttributes) override;
  /// Implements writer module interface.
  void process_config() override;
  /// Implements writer module interface.
  WriterModule::InitResult reopen(hdf5::node::Group &HDFGroup) override;

  /// Write an incoming message which should contain a flatbuffer.
  void write(FlatbufferMessage const &Message) override;

  f142_Writer() : WriterModule::Base(false) {}
  ~f142_Writer() override = default;

  enum class Type {
    int8,
    uint8,
    int16,
    uint16,
    int32,
    uint32,
    int64,
    uint64,
    float32,
    float64,
  };

protected:
  SharedLogger Logger = spdlog::get("filewriterlogger");
  std::string findDataType(nlohmann::basic_json<> const &Attribute);

  Type ElementType{Type::float64};

  NeXusDataset::MultiDimDatasetBase Values;

  /// Timestamps of the f142 updates.
  NeXusDataset::Time Timestamp;

  /// Index into DatasetTimestamp.
  NeXusDataset::CueTimestampZero CueTimestampZero;

  /// Index into the f142 values.
  NeXusDataset::CueIndex CueIndex;

  /// Timestamps of changes in EPICS alarm status
  NeXusDataset::AlarmTime AlarmTime;

  /// Changes in EPICS alarm status
  NeXusDataset::AlarmStatus AlarmStatus;

  /// Severity corresponding to EPICS alarm status
  NeXusDataset::AlarmSeverity AlarmSeverity;


  WriterModuleConfig::Field<uint64_t> ValueIndexInterval{this, "cue_interval", std::numeric_limits<uint64_t>::max()};
  WriterModuleConfig::Field<size_t> ArraySize{this, "array_size", 1};
  WriterModuleConfig::Field<size_t> ChunkSize{this, "chunk_size", 64*1024};
  WriterModuleConfig::Field<std::string> DataType{this, {"type", "dtype"}, "double"};
};

} // namespace f142
} // namespace WriterModule
