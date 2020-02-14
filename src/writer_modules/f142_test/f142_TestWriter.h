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
#include <NeXusDataset.h>

namespace WriterModule {
namespace f142 {
using FlatbufferMessage = FileWriter::FlatbufferMessage;
using ForwarderDebugDataset = NeXusDataset::ExtensibleDataset<std::uint64_t>;

class f142_TestWriter : public WriterModule::Base {
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

  f142_TestWriter() = default;
  ~f142_TestWriter() override = default;

protected:
  NeXusDataset::CueIndex Counter;
  int CounterValue{0};
};

} // namespace f142
} // namespace WriterModule
