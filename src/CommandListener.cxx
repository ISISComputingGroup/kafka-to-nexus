#include "CommandListener.h"
#include "KafkaW.h"
#include "helper.h"
#include "logger.h"

namespace FileWriter {

using std::vector;
using std::string;

CommandListener::CommandListener(MainOpt &config) : config(config) {}

CommandListener::~CommandListener() {}

void CommandListener::start() {
  KafkaW::BrokerSettings BrokerSettings;
  BrokerSettings.poll_timeout_ms = 500;
  BrokerSettings.address = config.command_broker_uri.host_port;
  BrokerSettings.conf_strings["group.id"] =
      fmt::format("kafka-to-nexus.CommandListener--pid-{}", getpid_wrapper());
  consumer.reset(new KafkaW::Consumer(BrokerSettings));
  consumer->on_rebalance_assign = config.on_rebalance_assign;
  consumer->add_topic(config.command_broker_uri.topic);
  if (config.start_at_command_offset >= 0) {
    int n1 = config.start_at_command_offset;
    consumer->on_rebalance_start =
        [n1](rd_kafka_topic_partition_list_t *plist) {
          for (int i1 = 0; i1 < plist->cnt; ++i1) {
            plist->elems[i1].offset = n1;
          }
        };
  }
}

KafkaW::PollStatus CommandListener::poll() { return consumer->poll(); }

} // namespace FileWriter
