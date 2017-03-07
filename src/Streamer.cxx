#include "Streamer.hpp"
#include <librdkafka/rdkafkacpp.h>
#include "logger.h"

/// TODO:
///   - reconnect if consumer return broker error
///   - search backward at connection setup

int64_t BrightnESS::FileWriter::Streamer::step_back_amount = 1000;
milliseconds BrightnESS::FileWriter::Streamer::consumer_timeout = milliseconds(1000);

BrightnESS::FileWriter::Streamer::Streamer(const std::string &broker,
                                           const std::string &topic_name,
                                           const RdKafkaOffset &offset,
                                           const RdKafkaPartition &partition)
    : _offset(offset), _partition(partition) {
 
  std::string errstr;
  {
    RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
    std::string debug;
    if (conf->set("metadata.broker.list", broker, errstr) !=
	RdKafka::Conf::CONF_OK) {
      throw std::runtime_error("Failed to initialise configuration: " + errstr);
    }
    if (conf->set("fetch.message.max.bytes", "1000000000", errstr) !=
	RdKafka::Conf::CONF_OK) {
      throw std::runtime_error("Failed to initialise configuration: " + errstr);
    }
    if (conf->set("receive.message.max.bytes", "1000000000", errstr) !=
	RdKafka::Conf::CONF_OK) {
      throw std::runtime_error("Failed to initialise configuration: " + errstr);
    }
    if (!debug.empty()) {
      if (conf->set("debug", debug, errstr) != RdKafka::Conf::CONF_OK) {
	throw std::runtime_error("Failed to initialise configuration: " + errstr);
      }
    }
    if (!(_consumer = RdKafka::Consumer::create(conf, errstr))) {
      throw std::runtime_error("Failed to create consumer: " + errstr);
    }
    delete conf;
  }

  {
    RdKafka::Conf *tconf = RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC);
    if (topic_name.empty()) {
      throw std::runtime_error("Topic required");
    }
    _topic = RdKafka::Topic::create(_consumer, topic_name, tconf, errstr);
    if (!_topic) {
      throw std::runtime_error("Failed to create topic: " + errstr);
    }
    delete tconf;
  }
  
  // Start consumer for topic+partition at start offset
  {
    RdKafka::ErrorCode resp = _consumer->start(_topic,
					       _partition.value(),
					       RdKafka::Topic::OFFSET_BEGINNING);
    if (resp != RdKafka::ERR_NO_ERROR) {
      throw std::runtime_error("Failed to start consumer: " +
			       RdKafka::err2str(resp));
    }
    RdKafka::Message *msg = _consumer->consume(_topic, _partition.value(), 100);
    _begin_offset = RdKafkaOffset(msg->offset());
    
    if ( _offset.value() != _begin_offset.value()) {
      _consumer->seek(_topic, _partition.value(),
		      RdKafka::Consumer::OffsetTail(1), 1000);
      RdKafka::Message *msg = _consumer->consume(_topic, _partition.value(), 1000);
      _offset = RdKafkaOffset(msg->offset());
    }
    else {
      _offset = _begin_offset;
    }
    delete msg;
  }

}

BrightnESS::FileWriter::Streamer::Streamer(const Streamer &other)
  : _topic(other._topic), _consumer(other._consumer), _offset(other._offset),
    _partition(other._partition) {}

int BrightnESS::FileWriter::Streamer::disconnect() {
  int return_code = _consumer->stop(_topic, _partition.value());
  delete _topic;
  delete _consumer;
  return return_code;
}

int BrightnESS::FileWriter::Streamer::closeStream() {
  return _consumer->stop(_topic, _partition.value());
}

int BrightnESS::FileWriter::Streamer::connect(const std::string &broker,
                                              const std::string &topic_name) {
  RdKafka::Conf *conf = RdKafka::Conf::create(RdKafka::Conf::CONF_GLOBAL);
  RdKafka::Conf *tconf = RdKafka::Conf::create(RdKafka::Conf::CONF_TOPIC);

  std::string debug, errstr;
  conf->set("metadata.broker.list", broker, errstr);
  if (!debug.empty()) {
    if (conf->set("debug", debug, errstr) != RdKafka::Conf::CONF_OK) {
      throw std::runtime_error("Failed to initialise configuration: " + errstr);
    }
  }

  conf->set("fetch.message.max.bytes", "1000000000", errstr);
  conf->set("receive.message.max.bytes", "1000000000", errstr);

  if (topic_name.empty()) {
    throw std::runtime_error("Topic required");
  }

  if (!(_consumer = RdKafka::Consumer::create(conf, errstr))) {
    throw std::runtime_error("Failed to create consumer: " + errstr);
  }

  if (!(_topic = RdKafka::Topic::create(_consumer, topic_name, tconf, errstr))) {
    throw std::runtime_error("Failed to create topic: " + errstr);
  }

  // Start consumer for topic+partition at start offset
  RdKafka::ErrorCode resp = _consumer->start(_topic, _partition.value(), _offset.value());
  if (resp != RdKafka::ERR_NO_ERROR) {
    throw std::runtime_error("Failed to start consumer: " +
                             RdKafka::err2str(resp));
  }
  return int(RdKafka::ERR_NO_ERROR);
}

BrightnESS::FileWriter::ProcessMessageResult
BrightnESS::FileWriter::Streamer::get_offset() {
  _consumer->seek(_topic,_partition.value(),RdKafka::Consumer::OffsetTail(1),1000);
  RdKafka::Message *msg = _consumer->consume(_topic, _partition.value(), 1000);
  last_offset = RdKafkaOffset(msg->offset());
  return ProcessMessageResult::OK();
}

/// Method specialisation for a functor with signature void f(void*). The
/// method applies f to the message payload.
template <>
BrightnESS::FileWriter::ProcessMessageResult
BrightnESS::FileWriter::Streamer::write(
    std::function<ProcessMessageResult(void *, int)> &f) {
  RdKafka::Message *msg =
      _consumer->consume(_topic, _partition.value(), consumer_timeout.count());
  if (msg->err() == RdKafka::ERR__PARTITION_EOF) {
    std::cout << "eof reached" << std::endl;
    return ProcessMessageResult::OK();
  }
  if (msg->err() != RdKafka::ERR_NO_ERROR) {
    std::cout << "Failed to consume message: " + RdKafka::err2str(msg->err())
              << std::endl;
    return ProcessMessageResult::ERR();;
  }
  message_length = msg->len();
  last_offset = RdKafkaOffset(msg->offset());
  std::cout << "msg->offset() = " << msg->offset() << "\n";
  
  return f(msg->payload(), msg->len());
}

template <>
BrightnESS::FileWriter::ProcessMessageResult
BrightnESS::FileWriter::Streamer::write(
    BrightnESS::FileWriter::DemuxTopic &mp) {
  RdKafka::Message *msg =
      _consumer->consume(_topic, _partition.value(), consumer_timeout.count());
  if (msg->err() == RdKafka::ERR__PARTITION_EOF) {
    //    std::cout << "eof reached" << std::endl;
    return ProcessMessageResult::OK();
  }
  if (msg->err() != RdKafka::ERR_NO_ERROR) {
    //    std::cout << "Failed to consume message:
    //    "+RdKafka::err2str(msg->err()) << std::endl;
    return ProcessMessageResult::ERR(); // msg->err();
  }
  message_length = msg->len();
  last_offset = RdKafkaOffset(msg->offset());

  auto result = mp.process_message((char *)msg->payload(), msg->len());
  std::cout << "process_message:\t" << result.ts() << std::endl;
  return result;
}

/// Implements some algorithm in order to search in the kafka queue the first
/// message with timestamp >= the timestam of beginning of data taking
/// (assumed to be stored in Source)
template <>
BrightnESS::FileWriter::TimeDifferenceFromMessage_DT
BrightnESS::FileWriter::Streamer::jump_back<BrightnESS::FileWriter::DemuxTopic>(
										BrightnESS::FileWriter::DemuxTopic &td, const int amount) {

  if (last_offset.value() == 0 ) {
    if (get_offset().is_ERR()) {
      return BrightnESS::FileWriter::TimeDifferenceFromMessage_DT::ERR();
    }
  }

  std::cout << "before seek : \t" << last_offset.value() << std::endl;
  auto err = _consumer->seek(_topic,
			    _partition.value(),
			    last_offset.value()-amount,
			    1000);
  if( err != RdKafka::ERR_NO_ERROR) {
    std::cout << RdKafka::err2str(err) << std::endl;
    return TimeDifferenceFromMessage_DT::ERR();
  }
  RdKafka::Message *msg =
      _consumer->consume(_topic, _partition.value(), consumer_timeout.count());
  std::cout << "after seek : \t" << last_offset.value() << std::endl;
  if (msg->err() != RdKafka::ERR_NO_ERROR) {
    std::cout << RdKafka::err2str(msg->err()) << std::endl;
    return TimeDifferenceFromMessage_DT::ERR(); // msg->err();
  }
  
  return td.time_difference_from_message((char *)msg->payload(), msg->len());
}


template <>
BrightnESS::FileWriter::TimeDifferenceFromMessage_DT
BrightnESS::FileWriter::Streamer::jump_back<std::function<
    BrightnESS::FileWriter::TimeDifferenceFromMessage_DT(void *, int)>>(
    std::function<
    BrightnESS::FileWriter::TimeDifferenceFromMessage_DT(void *, int)> &f,const int amount) {

  if (last_offset.value() == 0) {
    if (get_offset().is_ERR()) {
      return BrightnESS::FileWriter::TimeDifferenceFromMessage_DT::ERR();
    }
  }
  auto err = _consumer->seek(_topic,
			    _partition.value(),
			    last_offset.value()-amount,
			    1000);
  if( err != RdKafka::ERR_NO_ERROR) {
    std::cout << RdKafka::err2str(err) << std::endl;
    return TimeDifferenceFromMessage_DT::ERR();
  }
  RdKafka::Message *msg =
      _consumer->consume(_topic, _partition.value(), consumer_timeout.count());
  if (msg->err() != RdKafka::ERR_NO_ERROR) {
    std::cout << "Failed to consume message: " + RdKafka::err2str(msg->err())
              << std::endl;
    return BrightnESS::FileWriter::TimeDifferenceFromMessage_DT::ERR();
  }
  return f((char *)msg->payload(), msg->len());
}

template <>
std::map<std::string, int64_t> &&
BrightnESS::FileWriter::Streamer::scan_timestamps<
    BrightnESS::FileWriter::DemuxTopic>(
    BrightnESS::FileWriter::DemuxTopic &demux) {

  std::map<std::string, int64_t> timestamp;
  for (auto &s : demux.sources()) {
    timestamp[s.source()] = -1;
  }
  int n_sources = demux.sources().size();

  do {
    RdKafka::Message *msg =
        _consumer->consume(_topic, _partition.value(), consumer_timeout.count());
    if (msg->err() != RdKafka::ERR__PARTITION_EOF)
      break;
    if (msg->err() != RdKafka::ERR_NO_ERROR) {
      std::cout << "Failed to consume message: " + RdKafka::err2str(msg->err())
                << std::endl;
      continue;
    }
    DemuxTopic::DT t =
        demux.time_difference_from_message((char *)msg->payload(), msg->len());
    // HP: messages within the same source are ordered
    if (timestamp[t.sourcename] == -1) {
      n_sources--;
      timestamp[t.sourcename] = t.dt;
    }

  } while (n_sources < 1);

  return std::move(timestamp);
}
