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
#include <NeXusDataset/NeXusDataset.h>
#include <array>
#include <chrono>
#include <memory>
#include <nlohmann/json.hpp>
#include <nonstd/optional.hpp>
#include <vector>
#include <NeXusDataset/EpicsAlarmDatasets.h>

namespace WriterModule {
namespace f142 {
using FlatbufferMessage = FileWriter::FlatbufferMessage;

class f142_Writer : public WriterModule::Base {
public:
  /// Implements writer module interface.
  InitResult init_hdf(hdf5::node::Group &HDFGroup,
                      std::string const &HDFAttributes) override;
  /// Implements writer module interface.
  void parse_config(std::string const &ConfigurationStream) override;
  /// Implements writer module interface.
  WriterModule::InitResult reopen(hdf5::node::Group &HDFGroup) override;

  /// Write an incoming message which should contain a flatbuffer.
  void write(FlatbufferMessage const &Message) override;

  f142_Writer() = default;
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

  // set by default to a large value:
  uint64_t ValueIndexInterval = std::numeric_limits<uint64_t>::max();
  size_t ArraySize{1};
  size_t ChunkSize{64 * 1024};
  nonstd::optional<std::string> ValueUnits;
};

} // namespace f142
} // namespace WriterModule
