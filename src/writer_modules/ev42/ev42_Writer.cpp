// SPDX-License-Identifier: BSD-2-Clause
//
// This code has been produced by the European Spallation Source
// and its partner institutes under the BSD 2 Clause License.
//
// See LICENSE.md at the top level for license information.
//
// Screaming Udder!                              https://esss.se

#include <ev42_events_generated.h>

#include "HDFFile.h"
#include "ev42_Writer.h"
#include "helper.h"
#include "json.h"
#include "WriterRegistrar.h"

namespace {
template <typename DataType>
ArrayAdapter<const DataType> const
getFBVectorAsArrayAdapter(const flatbuffers::Vector<DataType> *Data) {
  return {Data->data(), Data->size()};
}
} // namespace

namespace WriterModule {
namespace ev42 {

using nlohmann::json;
using HDFWriterModule = WriterModule::Base;

struct append_ret {
  int status;
  uint64_t written_bytes;
  uint64_t ix0;

  explicit operator bool() const { return status == 0; }
};

void ev42_Writer::parse_config(std::string const &ConfigurationStream) {
  auto ConfigurationStreamJson = json::parse(ConfigurationStream);
  try {
    EventIndexInterval =
        ConfigurationStreamJson["nexus"]["indices"]["index_every_kb"]
            .get<uint64_t>() *
        1024;
    Logger->trace("Event index interval: {}", EventIndexInterval);
  } catch (...) { /* it's ok if not found */
  }
  try {
    EventIndexInterval =
        ConfigurationStreamJson["nexus"]["indices"]["index_every_mb"]
            .get<uint64_t>() *
        1024 * 1024;
    Logger->trace("Event index interval: {}", EventIndexInterval);
  } catch (...) { /* it's ok if not found */
  }
  try {
    ChunkSizeBytes =
        ConfigurationStreamJson["nexus"]["chunk"]["chunk_kb"].get<uint64_t>() *
        1024;
    Logger->trace("chunk_bytes: {}", ChunkSizeBytes);
  } catch (...) { /* it's ok if not found */
  }
  try {
    ChunkSizeBytes =
        ConfigurationStreamJson["nexus"]["chunk"]["chunk_mb"].get<uint64_t>() *
        1024 * 1024;
    Logger->trace("chunk_bytes: {}", ChunkSizeBytes);
  } catch (...) { /* it's ok if not found */
  }
  try {
    RecordAdcPulseDebugData =
        ConfigurationStreamJson["adc_pulse_debug"].get<bool>();
    Logger->trace("adc_pulse_debug: {}", RecordAdcPulseDebugData);
  } catch (...) { /* it's ok if not found */
  }
}

void ev42_Writer::createAdcDatasets(hdf5::node::Group &HDFGroup) const {
  // bytes to number of elements
  size_t ChunkSizeFor32BitTypes = ChunkSizeBytes / 4;
  size_t ChunkSizeFor64BitTypes = ChunkSizeBytes / 8;

  NeXusDataset::Amplitude(        // NOLINT(bugprone-unused-raii)
      HDFGroup,                   // NOLINT(bugprone-unused-raii)
      NeXusDataset::Mode::Create, // NOLINT(bugprone-unused-raii)
      ChunkSizeFor32BitTypes);    // NOLINT(bugprone-unused-raii)

  NeXusDataset::PeakArea(         // NOLINT(bugprone-unused-raii)
      HDFGroup,                   // NOLINT(bugprone-unused-raii)
      NeXusDataset::Mode::Create, // NOLINT(bugprone-unused-raii)
      ChunkSizeFor32BitTypes);    // NOLINT(bugprone-unused-raii)

  NeXusDataset::Background(       // NOLINT(bugprone-unused-raii)
      HDFGroup,                   // NOLINT(bugprone-unused-raii)
      NeXusDataset::Mode::Create, // NOLINT(bugprone-unused-raii)
      ChunkSizeFor32BitTypes);    // NOLINT(bugprone-unused-raii)

  NeXusDataset::ThresholdTime(    // NOLINT(bugprone-unused-raii)
      HDFGroup,                   // NOLINT(bugprone-unused-raii)
      NeXusDataset::Mode::Create, // NOLINT(bugprone-unused-raii)
      ChunkSizeFor64BitTypes);    // NOLINT(bugprone-unused-raii)

  NeXusDataset::PeakTime(         // NOLINT(bugprone-unused-raii)
      HDFGroup,                   // NOLINT(bugprone-unused-raii)
      NeXusDataset::Mode::Create, // NOLINT(bugprone-unused-raii)
      ChunkSizeFor64BitTypes);    // NOLINT(bugprone-unused-raii)
}

WriterModule::InitResult
ev42_Writer::init_hdf(hdf5::node::Group &HDFGroup,
                      std::string const &HDFAttributes) {
  auto Create = NeXusDataset::Mode::Create;
  size_t Chunk32Bit = ChunkSizeBytes / 4;
  size_t Chunk64Bit = ChunkSizeBytes / 8;
  try {

    NeXusDataset::EventTimeOffset( // NOLINT(bugprone-unused-raii)
        HDFGroup,                  // NOLINT(bugprone-unused-raii)
        Create,                    // NOLINT(bugprone-unused-raii)
        Chunk32Bit);               // NOLINT(bugprone-unused-raii)

    NeXusDataset::EventId( // NOLINT(bugprone-unused-raii)
        HDFGroup,          // NOLINT(bugprone-unused-raii)
        Create,            // NOLINT(bugprone-unused-raii)
        Chunk32Bit);       // NOLINT(bugprone-unused-raii)

    NeXusDataset::EventTimeZero( // NOLINT(bugprone-unused-raii)
        HDFGroup,                // NOLINT(bugprone-unused-raii)
        Create,                  // NOLINT(bugprone-unused-raii)
        Chunk64Bit);             // NOLINT(bugprone-unused-raii)

    NeXusDataset::EventIndex( // NOLINT(bugprone-unused-raii)
        HDFGroup,             // NOLINT(bugprone-unused-raii)
        Create,               // NOLINT(bugprone-unused-raii)
        Chunk32Bit);          // NOLINT(bugprone-unused-raii)

    NeXusDataset::CueIndex( // NOLINT(bugprone-unused-raii)
        HDFGroup,           // NOLINT(bugprone-unused-raii)
        Create,             // NOLINT(bugprone-unused-raii)
        Chunk32Bit);        // NOLINT(bugprone-unused-raii)

    NeXusDataset::CueTimestampZero( // NOLINT(bugprone-unused-raii)
        HDFGroup,                   // NOLINT(bugprone-unused-raii)
        Create,                     // NOLINT(bugprone-unused-raii)
        Chunk64Bit);                // NOLINT(bugprone-unused-raii)

    if (RecordAdcPulseDebugData) {
      createAdcDatasets(HDFGroup);
    }

    if (HDFGroup.attributes.exists("NX_class")) {
      Logger->info("NX_class already specified!");
    } else {
      auto ClassAttribute = HDFGroup.attributes.create<std::string>("NX_class");
      ClassAttribute.write("NXevent_data");
    }
    auto AttributesJson = nlohmann::json::parse(HDFAttributes);
    FileWriter::writeAttributes(HDFGroup, &AttributesJson, Logger);
  } catch (std::exception const &E) {
    auto message = hdf5::error::print_nested(E);
    Logger->error("ev42 could not init hdf_parent: {}  trace: {}",
                  static_cast<std::string>(HDFGroup.link().path()), message);
    return WriterModule::InitResult::ERROR;
  }
  return WriterModule::InitResult::OK;
}

WriterModule::InitResult ev42_Writer::reopen(hdf5::node::Group &HDFGroup) {
  auto Open = NeXusDataset::Mode::Open;
  try {
    EventTimeOffset = NeXusDataset::EventTimeOffset(HDFGroup, Open);
    EventId = NeXusDataset::EventId(HDFGroup, Open);
    EventTimeZero = NeXusDataset::EventTimeZero(HDFGroup, Open);
    EventIndex = NeXusDataset::EventIndex(HDFGroup, Open);
    CueIndex = NeXusDataset::CueIndex(HDFGroup, Open);
    CueTimestampZero = NeXusDataset::CueTimestampZero(HDFGroup, Open);
    if (RecordAdcPulseDebugData) {
      reopenAdcDatasets(HDFGroup);
    }
  } catch (std::exception &E) {
    Logger->error(
        "Failed to reopen datasets in HDF file with error message: \"{}\"",
        std::string(E.what()));
    return WriterModule::InitResult::ERROR;
  }
  return WriterModule::InitResult::OK;
}
void ev42_Writer::reopenAdcDatasets(const hdf5::node::Group &HDFGroup) {
  AmplitudeDataset =
      NeXusDataset::Amplitude(HDFGroup, NeXusDataset::Mode::Open);
  PeakAreaDataset = NeXusDataset::PeakArea(HDFGroup, NeXusDataset::Mode::Open);
  BackgroundDataset =
      NeXusDataset::Background(HDFGroup, NeXusDataset::Mode::Open);
  ThresholdTimeDataset =
      NeXusDataset::ThresholdTime(HDFGroup, NeXusDataset::Mode::Open);
  PeakTimeDataset = NeXusDataset::PeakTime(HDFGroup, NeXusDataset::Mode::Open);
}

void ev42_Writer::write(FlatbufferMessage const &Message) {
  auto EventMsgFlatbuffer = GetEventMessage(Message.data());
  EventTimeOffset.appendArray(
      getFBVectorAsArrayAdapter(EventMsgFlatbuffer->time_of_flight()));
  EventId.appendArray(
      getFBVectorAsArrayAdapter(EventMsgFlatbuffer->detector_id()));
  if (EventMsgFlatbuffer->time_of_flight()->size() !=
      EventMsgFlatbuffer->detector_id()->size()) {
    Logger->warn("written data lengths differ");
  }
  auto CurrentRefTime = EventMsgFlatbuffer->pulse_time();
  auto CurrentNumberOfEvents = EventMsgFlatbuffer->detector_id()->size();
  EventTimeZero.appendElement(CurrentRefTime);
  EventIndex.appendElement(EventsWritten);
  EventsWritten += CurrentNumberOfEvents;
  if (EventsWritten > LastEventIndex + EventIndexInterval) {
    auto LastRefTimeOffset = EventMsgFlatbuffer->time_of_flight()->operator[](
        CurrentNumberOfEvents - 1);
    CueTimestampZero.appendElement(CurrentRefTime + LastRefTimeOffset);
    CueIndex.appendElement(EventsWritten - 1);
    LastEventIndex = EventsWritten - 1;
  }

  if (RecordAdcPulseDebugData) {
    writeAdcPulseData(Message);
  }
}

void ev42_Writer::writeAdcPulseData(FlatbufferMessage const &Message) {
  auto EventMsgFlatbuffer = GetEventMessage(Message.data());
  if (EventMsgFlatbuffer->facility_specific_data_type() !=
      FacilityData::AdcPulseDebug) {
    padDatasetsWithZeroesEqualToNumberOfEvents(Message);
  } else {
    writeAdcPulseDataFromMessageToFile(Message);
  }
}

void ev42_Writer::writeAdcPulseDataFromMessageToFile(
    FlatbufferMessage const &Message) {
  auto EventMsgFlatbuffer = GetEventMessage(Message.data());
  auto AdcPulseDebugMsgFlatbuffer =
      EventMsgFlatbuffer->facility_specific_data_as_AdcPulseDebug();

  ArrayAdapter<const uint32_t> AmplitudeArray =
      getFBVectorAsArrayAdapter(AdcPulseDebugMsgFlatbuffer->amplitude());
  AmplitudeDataset.appendArray(AmplitudeArray);

  ArrayAdapter<const uint32_t> PeakAreaArray =
      getFBVectorAsArrayAdapter(AdcPulseDebugMsgFlatbuffer->peak_area());
  PeakAreaDataset.appendArray(PeakAreaArray);

  ArrayAdapter<const uint32_t> BackgroundArray =
      getFBVectorAsArrayAdapter(AdcPulseDebugMsgFlatbuffer->background());
  BackgroundDataset.appendArray(BackgroundArray);

  ArrayAdapter<const uint64_t> ThresholdTimeArray =
      getFBVectorAsArrayAdapter(AdcPulseDebugMsgFlatbuffer->threshold_time());
  ThresholdTimeDataset.appendArray(ThresholdTimeArray);

  ArrayAdapter<const uint64_t> PeakTimeArray =
      getFBVectorAsArrayAdapter(AdcPulseDebugMsgFlatbuffer->peak_time());
  PeakTimeDataset.appendArray(PeakTimeArray);
}

/// If ADC pulse data is missing from message then pad the datasets so that
/// event_index and event_time_zero datasets will still be consistent with ADC
/// datasets
void ev42_Writer::padDatasetsWithZeroesEqualToNumberOfEvents(
    FlatbufferMessage const &Message) {
  auto EventMsgFlatbuffer = GetEventMessage(Message.data());
  size_t NumberOfEventsInMessage = EventMsgFlatbuffer->time_of_flight()->size();
  std::vector<uint32_t> ZeroesUInt32(NumberOfEventsInMessage, 0);
  ArrayAdapter<const uint32_t> ZeroesUInt32ArrayAdapter(ZeroesUInt32.data(),
                                                        ZeroesUInt32.size());
  std::vector<uint64_t> ZeroesUInt64(NumberOfEventsInMessage, 0);
  ArrayAdapter<const uint64_t> ZeroesUInt64ArrayAdapter(ZeroesUInt64.data(),
                                                        ZeroesUInt64.size());

  AmplitudeDataset.appendArray(ZeroesUInt32ArrayAdapter);
  PeakAreaDataset.appendArray(ZeroesUInt32ArrayAdapter);
  BackgroundDataset.appendArray(ZeroesUInt32ArrayAdapter);
  ThresholdTimeDataset.appendArray(ZeroesUInt64ArrayAdapter);
  PeakTimeDataset.appendArray(ZeroesUInt64ArrayAdapter);
}

static WriterModule::Registry::Registrar<ev42_Writer>
    RegisterWriter("ev42", "general_event_writer");

} // namespace ev42
} // namespace WriterModule
