#include "DemuxTopic.h"
#include <stdexcept>
#include "logger.h"



namespace BrightnESS {
namespace FileWriter {

ProcessMessageResult ProcessMessageResult::OK(int64_t ts) {
	if (ts < 0) {
		throw std::runtime_error("Invalid timestamp");
	}
	ProcessMessageResult ret;
	ret._ts = ts;
	return ret;
}

ProcessMessageResult ProcessMessageResult::ERR() {
	ProcessMessageResult ret;
	ret._ts = -1;
	return ret;
}

ProcessMessageResult ProcessMessageResult::ALL_SOURCES_FULL() {
	ProcessMessageResult ret;
	ret._ts = -2;
	return ret;
}

DemuxTopic::DemuxTopic(std::string topic) : _topic(topic) {
}

DemuxTopic::DT DemuxTopic::time_difference_from_message(char * msg_data, int msg_size) {
  std::string _tmp_dummy;
  auto reader = FBSchemaReader::create(msg_data, msg_size);
  if (!reader) {
    LOG(3, "ERROR unknown schema id?");
    return DT::ERR();
  }
  auto srcn = reader->sourcename(msg_data);
  std::unique_ptr<FBSchemaReader> _schema_reader;
  LOG(0, "Msg is for sourcename: {}", srcn);
  for (auto & s : sources()) {
    if (s.source() == srcn) {
       _schema_reader = FBSchemaReader::create(msg_data, msg_size);
    }
  }
  return DT(srcn, _schema_reader->ts(msg_data));
}

std::string const & DemuxTopic::topic() const {
	return _topic;
}

ProcessMessageResult DemuxTopic::process_message(char * msg_data, int msg_size) {
	auto reader = FBSchemaReader::create(msg_data, msg_size);
	if (!reader) {
		LOG(3, "ERROR unknown schema id?");
		return ProcessMessageResult::ERR();
	}
	auto srcn = reader->sourcename(msg_data);
	LOG(0, "Msg is for sourcename: {}", srcn);
	for (auto & s : sources()) {
		if (s.source() == srcn) {
			auto ret = s.process_message(msg_data, msg_size);
			if (ret.ts() < 0) return ProcessMessageResult::ERR();
			return ProcessMessageResult::OK(ret.ts());
		}
	}
	return ProcessMessageResult::ERR();
}

std::vector<Source> & DemuxTopic::sources() {
	return _sources;
}

std::string DemuxTopic::to_str() const {
	return json_to_string(to_json());
}

rapidjson::Document DemuxTopic::to_json(rapidjson::MemoryPoolAllocator<> * _a) const {
	using namespace rapidjson;
	Document jd;
	if (_a) jd = Document(_a);
	auto & a = jd.GetAllocator();
	jd.SetObject();
	auto & v = jd;
	v.AddMember("__KLASS__", "DemuxTopic", a);
	v.AddMember("topic", Value(topic().data(), a), a);
	Value kl;
	kl.SetArray();
	for (auto & s : _sources) {
		kl.PushBack(s.to_json(&a), a);
	}
	v.AddMember("sources", kl, a);
	return jd;
}


}
}
