#include "KafkaW.h"
#include <atomic>
#include <cerrno>

namespace KafkaW {

using std::unique_ptr;
using std::shared_ptr;
using std::array;
using std::vector;
using std::string;
using std::atomic;
using std::move;

std::atomic<int> g_kafka_instance_count;

using std::move;

#define KERR(rk, err)                                                          \
  if (err != 0) {                                                              \
    LOG(Sev::Error, "Kafka {}  error: {}, {}, {}", rd_kafka_name(rk), err,     \
        rd_kafka_err2name((rd_kafka_resp_err_t)err),                           \
        rd_kafka_err2str((rd_kafka_resp_err_t)err));                           \
  }

static_assert(RD_KAFKA_RESP_ERR_NO_ERROR == 0, "We rely on NO_ERROR == 0");

TopicOpt::TopicOpt() {}

void TopicOpt::apply(rd_kafka_topic_conf_t *conf) {
  std::vector<char> errstr(1024);
  for (auto &c : conf_ints) {
    auto s1 = fmt::format("{:d}", c.second);
    LOG(Sev::Debug, "use  {}: {}", c.first, s1);
    if (RD_KAFKA_CONF_OK != rd_kafka_topic_conf_set(conf, c.first.c_str(),
                                                    s1.c_str(), errstr.data(),
                                                    errstr.size())) {
      LOG(Sev::Warning, "error setting topic config: {} = {}", c.first, s1);
    }
  }
  for (auto &c : conf_strings) {
    LOG(Sev::Debug, "use  {}: {}", c.first, c.second);
    if (RD_KAFKA_CONF_OK !=
        rd_kafka_topic_conf_set(conf, c.first.c_str(), c.second.c_str(),
                                errstr.data(), errstr.size())) {
      LOG(Sev::Warning, "error setting topic config: {} = {}", c.first,
          c.second);
    }
  }
}

Msg::~Msg() { rd_kafka_message_destroy((rd_kafka_message_t *)kmsg); }

uchar *Msg::data() { return (uchar *)((rd_kafka_message_t *)kmsg)->payload; }

uint32_t Msg::size() { return ((rd_kafka_message_t *)kmsg)->len; }

char const *Msg::topic_name() {
  return rd_kafka_topic_name(((rd_kafka_message_t *)kmsg)->rkt);
}

int32_t Msg::partition() { return ((rd_kafka_message_t *)kmsg)->partition; }

int32_t Msg::offset() { return ((rd_kafka_message_t *)kmsg)->offset; }

PollStatus::~PollStatus() { reset(); }

PollStatus PollStatus::Ok() {
  PollStatus ret;
  ret.state = 0;
  return ret;
}

PollStatus PollStatus::Err() {
  PollStatus ret;
  ret.state = -1;
  return ret;
}

PollStatus PollStatus::EOP() {
  PollStatus ret;
  ret.state = -2;
  return ret;
}

PollStatus PollStatus::Empty() {
  PollStatus ret;
  ret.state = -3;
  return ret;
}

PollStatus PollStatus::make_Msg(std::unique_ptr<Msg> x) {
  PollStatus ret;
  ret.state = 1;
  ret.data = x.release();
  return ret;
}

PollStatus::PollStatus(PollStatus &&x)
    : state(std::move(x.state)), data(std::move(x.data)) {}

PollStatus &PollStatus::operator=(PollStatus &&x) {
  reset();
  std::swap(state, x.state);
  std::swap(data, x.data);
  return *this;
}

void PollStatus::reset() {
  if (state == 1) {
    if (auto x = (Msg *)data) {
      delete x;
    }
  }
  state = -1;
  data = nullptr;
}

PollStatus::PollStatus() {}

bool PollStatus::is_Ok() { return state == 0; }

bool PollStatus::is_Err() { return state == -1; }

bool PollStatus::is_EOP() { return state == -2; }

bool PollStatus::is_Empty() { return state == -3; }

std::unique_ptr<Msg> PollStatus::is_Msg() {
  if (state == 1) {
    std::unique_ptr<Msg> ret((Msg *)data);
    data = nullptr;
    return ret;
  }
  return nullptr;
}

Consumer::Consumer(BrokerSettings BrokerSettings)
    : ConsumerBrokerSettings(BrokerSettings) {
  init();
  id = g_kafka_instance_count++;
}

Consumer::~Consumer() {
  LOG(Sev::Debug, "~Consumer()");
  if (rk) {
    // commit offsets?
    if (0) {
      LOG(Sev::Debug, "rd_kafka_unsubscribe");
      rd_kafka_unsubscribe(rk);
    }
    if (0) {
      LOG(Sev::Debug, "rd_kafka_poll");
      int n1 = rd_kafka_poll(rk, 100);
      LOG(Sev::Debug, "  served {} reuests", n1);
    }
    if (1) {
      LOG(Sev::Debug, "rd_kafka_consumer_close");
      rd_kafka_consumer_close(rk);
    }
    // rd_kafka_consume_stop(rd_kafka_topic_t *, partition)  therefore low-level
    // API?
    if (1) {
      LOG(Sev::Debug, "rd_kafka_destroy");
      rd_kafka_destroy(rk);
      rk = nullptr;
    }
  }
  if (plist) {
    rd_kafka_topic_partition_list_destroy(plist);
    plist = nullptr;
  }
}

void Consumer::cb_log(rd_kafka_t const *rk, int level, char const *fac,
                      char const *buf) {
  auto self = reinterpret_cast<Consumer *>(rd_kafka_opaque(rk));
  LOG(Sev(level), "IID: {}  {}  fac: {}", self->id, buf, fac);
}

void Consumer::cb_error(rd_kafka_t *rk, int err_i, char const *msg,
                        void *opaque) {
  auto self = reinterpret_cast<Consumer *>(opaque);
  rd_kafka_resp_err_t err = (rd_kafka_resp_err_t)err_i;
  Sev ll = Sev::Debug;
  if (err == RD_KAFKA_RESP_ERR__TRANSPORT) {
    ll = Sev::Warning;
    // rd_kafka_dump(stdout, rk);
  }
  LOG(ll, "Kafka cb_error id: {}  broker: {}  errno: {}  errorname: {}  "
          "errorstring: {}  message: {}",
      self->id, self->ConsumerBrokerSettings.address, err_i,
      rd_kafka_err2name(err), rd_kafka_err2str(err), msg);
}

int Consumer::cb_stats(rd_kafka_t *rk, char *json, size_t json_size,
                       void *opaque) {
  LOG(Sev::Debug, "INFO stats_cb {}  {:.{}}", json_size, json, json_size);
  // What does Kafka want us to return from this callback?
  return 0;
}

static void print_partition_list(rd_kafka_topic_partition_list_t *plist) {
  for (int i1 = 0; i1 < plist->cnt; ++i1) {
    auto &x = plist->elems[i1];
    LOG(Sev::Debug, "   {}  {}  {}", x.topic, x.partition, x.offset);
  }
}

void Consumer::cb_rebalance(rd_kafka_t *rk, rd_kafka_resp_err_t err,
                            rd_kafka_topic_partition_list_t *plist,
                            void *opaque) {
  rd_kafka_resp_err_t err2;
  auto self = static_cast<Consumer *>(opaque);
  switch (err) {
  case RD_KAFKA_RESP_ERR__ASSIGN_PARTITIONS:
    LOG(Sev::Debug, "cb_rebalance assign {}", rd_kafka_name(rk));
    if (auto &cb = self->on_rebalance_start) {
      cb(plist);
    }
    print_partition_list(plist);
    err2 = rd_kafka_assign(rk, plist);
    if (err2 != RD_KAFKA_RESP_ERR_NO_ERROR) {
      LOG(Sev::Notice, "rebalance error: {}  {}", rd_kafka_err2name(err2),
          rd_kafka_err2str(err2));
    }
    if (auto &cb = self->on_rebalance_assign) {
      cb(plist);
    }
    break;
  case RD_KAFKA_RESP_ERR__REVOKE_PARTITIONS:
    LOG(Sev::Warning, "cb_rebalance revoke:");
    print_partition_list(plist);
    err2 = rd_kafka_assign(rk, NULL);
    if (err2 != RD_KAFKA_RESP_ERR_NO_ERROR) {
      LOG(Sev::Warning, "rebalance error: {}  {}", rd_kafka_err2name(err2),
          rd_kafka_err2str(err2));
    }
    break;
  default:
    LOG(Sev::Info, "cb_rebalance failure and revoke: {}",
        rd_kafka_err2str(err));
    err2 = rd_kafka_assign(rk, NULL);
    if (err2 != RD_KAFKA_RESP_ERR_NO_ERROR) {
      LOG(Sev::Warning, "rebalance error: {}  {}", rd_kafka_err2name(err2),
          rd_kafka_err2str(err2));
    }
    break;
  }
}

void Consumer::init() {
  // librdkafka API sometimes wants to write errors into a buffer:
  int const errstr_N = 512;
  char errstr[errstr_N];

  auto conf = rd_kafka_conf_new();
  ConsumerBrokerSettings.apply(conf);

  rd_kafka_conf_set_log_cb(conf, Consumer::cb_log);
  rd_kafka_conf_set_error_cb(conf, Consumer::cb_error);
  rd_kafka_conf_set_stats_cb(conf, Consumer::cb_stats);
  rd_kafka_conf_set_rebalance_cb(conf, Consumer::cb_rebalance);
  rd_kafka_conf_set_consume_cb(conf, nullptr);
  rd_kafka_conf_set_opaque(conf, this);

  rk = rd_kafka_new(RD_KAFKA_CONSUMER, conf, errstr, errstr_N);
  if (!rk) {
    LOG(Sev::Error, "can not create kafka handle: {}", errstr);
    throw std::runtime_error("can not create Kafka handle");
  }

  rd_kafka_set_log_level(rk, 4);

  LOG(Sev::Info, "New Kafka consumer {} with brokers: {}", rd_kafka_name(rk),
      ConsumerBrokerSettings.address.c_str());
  if (rd_kafka_brokers_add(rk, ConsumerBrokerSettings.address.c_str()) == 0) {
    LOG(Sev::Error, "could not add brokers");
    throw std::runtime_error("could not add brokers");
  }

  rd_kafka_poll_set_consumer(rk);

  // Allocate some default size.  This is not a limit.
  plist = rd_kafka_topic_partition_list_new(16);
}

void Consumer::add_topic(std::string topic) {
  LOG(Sev::Info, "Consumer::add_topic  {}", topic);
  int partition = RD_KAFKA_PARTITION_UA;
  rd_kafka_topic_partition_list_add(plist, topic.c_str(), partition);
  int err = rd_kafka_subscribe(rk, plist);
  KERR(rk, err);
  if (err) {
    LOG(Sev::Error, "could not subscribe");
    throw std::runtime_error("can not subscribe");
  }
}

void Consumer::dump_current_subscription() {
  // Dump current subscription:
  rd_kafka_topic_partition_list_t *l1 = nullptr;
  rd_kafka_subscription(rk, &l1);
  if (l1) {
    for (int i1 = 0; i1 < l1->cnt; ++i1) {
      LOG(Sev::Info, "subscribed topics: {}  {}  off {}", l1->elems[i1].topic,
          rd_kafka_err2str(l1->elems[i1].err), l1->elems[i1].offset);
    }
    rd_kafka_topic_partition_list_destroy(l1);
  }
}

PollStatus Consumer::poll() {
  if (0)
    dump_current_subscription();
  if (0)
    rd_kafka_dump(stdout, rk);

  auto ret = PollStatus::Empty();

  auto msg = rd_kafka_consumer_poll(rk, ConsumerBrokerSettings.poll_timeout_ms);

  if (msg == nullptr) {
    return PollStatus::Empty();
  }

  static_assert(sizeof(char) == 1, "Failed: sizeof(char) == 1");
  std::unique_ptr<Msg> m2(new Msg);
  m2->kmsg = msg;
  if (msg->err == RD_KAFKA_RESP_ERR_NO_ERROR) {
    return PollStatus::make_Msg(std::move(m2));
  } else if (msg->err == RD_KAFKA_RESP_ERR__PARTITION_EOF) {
    // Just an advisory.  msg contains which partition it is.
    return PollStatus::EOP();
  } else if (msg->err == RD_KAFKA_RESP_ERR__ALL_BROKERS_DOWN) {
    LOG(Sev::Error, "RD_KAFKA_RESP_ERR__ALL_BROKERS_DOWN");
  } else if (msg->err == RD_KAFKA_RESP_ERR__BAD_MSG) {
    LOG(Sev::Error, "RD_KAFKA_RESP_ERR__BAD_MSG");
  } else if (msg->err == RD_KAFKA_RESP_ERR__DESTROY) {
    LOG(Sev::Error, "RD_KAFKA_RESP_ERR__DESTROY");
    // Broker will go away soon
  } else {
    LOG(Sev::Error, "unhandled msg error: {} {}", rd_kafka_err2name(msg->err),
        rd_kafka_err2str(msg->err));
  }
  return PollStatus::Err();
}

void Producer::cb_delivered(rd_kafka_t *rk, rd_kafka_message_t const *msg,
                            void *opaque) {
  auto self = reinterpret_cast<Producer *>(opaque);
  if (!msg) {
    LOG(Sev::Error, "IID: {}  ERROR msg should never be null", self->id);
    ++self->stats.produce_cb_fail;
    return;
  }
  if (msg->err) {
    LOG(Sev::Error, "IID: {}  ERROR on delivery, {}, topic {}, {} [{}] {}",
        self->id, rd_kafka_name(rk), rd_kafka_topic_name(msg->rkt),
        rd_kafka_err2name(msg->err), msg->err, rd_kafka_err2str(msg->err));
    if (msg->err == RD_KAFKA_RESP_ERR__MSG_TIMED_OUT) {
      // TODO
    }
    if (auto &cb = self->on_delivery_failed) {
      cb(msg);
    }
    ++self->stats.produce_cb_fail;
  } else {
    if (auto &cb = self->on_delivery_ok) {
      cb(msg);
    }
    if (false) {
      LOG(Sev::Debug, "IID: {}  Ok delivered ({}, p {}, offset {}, len {})",
          self->id, rd_kafka_name(rk), msg->partition, msg->offset, msg->len);
    }
    ++self->stats.produce_cb;
  }
}

void Producer::cb_error(rd_kafka_t *rk, int err_i, char const *msg,
                        void *opaque) {
  auto self = reinterpret_cast<Producer *>(opaque);
  rd_kafka_resp_err_t err = (rd_kafka_resp_err_t)err_i;
  Sev ll = Sev::Warning;
  if (err == RD_KAFKA_RESP_ERR__TRANSPORT) {
    ll = Sev::Error;
    // rd_kafka_dump(stdout, rk);
  } else {
    if (self->on_error)
      self->on_error(self, err);
  }
  LOG(ll, "Kafka cb_error id: {}  broker: {}  errno: {}  errorname: {}  "
          "errorstring: {}  message: {}",
      self->id, self->ProducerBrokerSettings.address, err_i,
      rd_kafka_err2name(err), rd_kafka_err2str(err), msg);
}

int Producer::cb_stats(rd_kafka_t *rk, char *json, size_t json_len,
                       void *opaque) {
  auto self = reinterpret_cast<Producer *>(opaque);
  LOG(Sev::Debug, "IID: {}  INFO cb_stats {} length {}   {:.{}}", self->id,
      rd_kafka_name(rk), json_len, json, json_len);
  // What does librdkafka want us to return from this callback?
  return 0;
}

void Producer::cb_log(rd_kafka_t const *rk, int level, char const *fac,
                      char const *buf) {
  auto self = reinterpret_cast<Producer *>(rd_kafka_opaque(rk));
  LOG(Sev::Debug, "IID: {}  {}  fac: {}", self->id, buf, fac);
}

void Producer::cb_throttle(rd_kafka_t *rk, char const *broker_name,
                           int32_t broker_id, int throttle_time_ms,
                           void *opaque) {
  auto self = reinterpret_cast<Producer *>(opaque);
  LOG(Sev::Debug, "IID: {}  INFO cb_throttle  broker_id: {}  broker_name: {}  "
                  "throttle_time_ms: {}",
      self->id, broker_id, broker_name, throttle_time_ms);
}

Producer::~Producer() {
  LOG(Sev::Debug, "~Producer");
  if (rk) {
    int timeout_ms = 1;
    uint32_t outq_len = 0;
    while (true) {
      outq_len = rd_kafka_outq_len(rk);
      if (outq_len == 0) {
        break;
      }
      auto events_handled = rd_kafka_poll(rk, timeout_ms);
      if (events_handled > 0) {
        LOG(Sev::Debug,
            "rd_kafka_poll handled: {}  outq before: {}  timeout: {}",
            events_handled, outq_len, timeout_ms);
      }
      timeout_ms = timeout_ms << 1;
      if (timeout_ms > 8 * 1024) {
        break;
      }
    }
    if (outq_len > 0) {
      LOG(Sev::Notice,
          "Kafka out queue still not empty: {}  destroy producer anyway.",
          outq_len);
    }
    LOG(Sev::Debug, "rd_kafka_destroy");
    rd_kafka_destroy(rk);
    rk = nullptr;
  }
}

Producer::Producer(BrokerSettings ProducerBrokerSettings)
    : ProducerBrokerSettings(ProducerBrokerSettings) {
  id = g_kafka_instance_count++;

  // librdkafka API sometimes wants to write errors into a buffer:
  std::vector<char> errstr;
  errstr.resize(512);

  rd_kafka_conf_t *conf = 0;
  conf = rd_kafka_conf_new();
  rd_kafka_conf_set_dr_msg_cb(conf, Producer::cb_delivered);
  rd_kafka_conf_set_error_cb(conf, Producer::cb_error);
  rd_kafka_conf_set_stats_cb(conf, Producer::cb_stats);
  rd_kafka_conf_set_log_cb(conf, Producer::cb_log);
  rd_kafka_conf_set_throttle_cb(conf, Producer::cb_throttle);

  rd_kafka_conf_set_opaque(conf, this);
  LOG(Sev::Debug, "Producer opaque: {}", (void *)this);

  ProducerBrokerSettings.apply(conf);

  rk = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr.data(), errstr.size());
  if (!rk) {
    LOG(Sev::Error, "can not create kafka handle: {}", errstr.data());
    throw std::runtime_error("can not create Kafka handle");
  }

  rd_kafka_set_log_level(rk, 4);

  LOG(Sev::Info, "New Kafka {} with brokers: {}", rd_kafka_name(rk),
      ProducerBrokerSettings.address.c_str());
  if (rd_kafka_brokers_add(rk, ProducerBrokerSettings.address.c_str()) == 0) {
    LOG(Sev::Error, "could not add brokers");
    throw std::runtime_error("could not add brokers");
  }
}

Producer::Producer(Producer &&x) {
  using std::swap;
  swap(rk, x.rk);
  swap(on_delivery_ok, x.on_delivery_ok);
  swap(on_delivery_failed, x.on_delivery_failed);
  swap(on_error, x.on_error);
  swap(ProducerBrokerSettings, x.ProducerBrokerSettings);
  swap(id, x.id);
}

void Producer::poll() {
  int events_handled =
      rd_kafka_poll(rk, ProducerBrokerSettings.poll_timeout_ms);
  LOG(Sev::Debug,
      "IID: {}  broker: {}  rd_kafka_poll()  served: {}  outq_len: {}", id,
      ProducerBrokerSettings.address, events_handled, outq());
  if (log_level >= 8) {
    rd_kafka_dump(stdout, rk);
  }
  stats.poll_served += events_handled;
  stats.out_queue = outq();
}

void Producer::poll_while_outq() {
  while (outq() > 0) {
    stats.poll_served +=
        rd_kafka_poll(rk, ProducerBrokerSettings.poll_timeout_ms);
  }
}

rd_kafka_t *Producer::rd_kafka_ptr() const { return rk; }

uint64_t Producer::outq() { return rd_kafka_outq_len(rk); }

uint64_t Producer::total_produced() { return total_produced_; }

ProducerStats::ProducerStats() {}

ProducerStats::ProducerStats(ProducerStats const &x) {
  produced = x.produced.load();
  produce_fail = x.produce_fail.load();
  local_queue_full = x.local_queue_full.load();
  produce_cb = x.produce_cb.load();
  produce_cb_fail = x.produce_cb_fail.load();
  poll_served = x.poll_served.load();
  msg_too_large = x.msg_too_large.load();
  produced_bytes = x.produced_bytes.load();
  out_queue = x.out_queue.load();
}

ProducerTopic::~ProducerTopic() {
  LOG(Sev::Debug, "~ProducerTopic {}", _name);
  if (rkt) {
    LOG(Sev::Debug, "rd_kafka_topic_destroy");
    rd_kafka_topic_destroy(rkt);
    rkt = nullptr;
  }
}

ProducerTopic::ProducerTopic(std::shared_ptr<Producer> producer,
                             std::string name)
    : producer(producer), _name(name) {
  TopicOpt opt;
  rd_kafka_topic_conf_t *topic_conf = rd_kafka_topic_conf_new();
  opt.apply(topic_conf);

  // rd_kafka_msg_partitioner_random, rd_kafka_msg_partitioner_consistent,
  // rd_kafka_msg_partitioner_consistent_random
  // rd_kafka_topic_conf_set_partitioner_cb(topic_conf,
  // rd_kafka_msg_partitioner_random);

  rkt = rd_kafka_topic_new(producer->rd_kafka_ptr(), _name.c_str(), topic_conf);
  if (rkt == nullptr) {
    // Seems like Kafka uses the system error code?
    auto errstr = rd_kafka_err2str(rd_kafka_errno2err(errno));
    LOG(Sev::Error, "could not create Kafka topic: {}", errstr);
    throw std::exception();
  }
  LOG(Sev::Debug, "ctor topic: {}  producer: {}", rd_kafka_topic_name(rkt),
      rd_kafka_name(producer->rd_kafka_ptr()));
}

ProducerTopic::ProducerTopic(ProducerTopic &&x) {
  std::swap(producer, x.producer);
  std::swap(rkt, x.rkt);
  std::swap(_name, x._name);
  std::swap(_do_copy, x._do_copy);
}

struct Msg_ : public Producer::Msg {
  vector<uchar> v;
  void finalize() {
    data = v.data();
    size = v.size();
  }
};

int ProducerTopic::produce(uchar *msg_data, size_t msg_size, bool print_err) {
  auto p = new Msg_;
  std::copy(msg_data, msg_data + msg_size, std::back_inserter(p->v));
  p->finalize();
  unique_ptr<Producer::Msg> m(p);
  return produce(m);
}

int ProducerTopic::produce(unique_ptr<Producer::Msg> &msg) {
  if (not rkt) {
    throw std::runtime_error("ERROR tried to produce on uninitialized rkt");
  }
  int x;
  int32_t partition = RD_KAFKA_PARTITION_UA;
  void const *key = NULL;
  size_t key_len = 0;
  int msgflags = 0; // 0, RD_KAFKA_MSG_F_COPY, RD_KAFKA_MSG_F_FREE
  x = rd_kafka_produce(rkt, partition, msgflags, msg->data, msg->size, key,
                       key_len, msg.get());

  auto &s = producer->stats;
  if (x != 0) {
    auto err = rd_kafka_errno2err(rd_kafka_errno());
    bool print_err = true;
    if (err == RD_KAFKA_RESP_ERR__QUEUE_FULL) {
      ++s.local_queue_full;
      if (print_err) {
        LOG(Sev::Warning, "QUEUE_FULL  outq: {}",
            rd_kafka_outq_len(producer->rd_kafka_ptr()));
      }
    } else if (err == RD_KAFKA_RESP_ERR_MSG_SIZE_TOO_LARGE) {
      ++s.msg_too_large;
      if (print_err) {
        LOG(Sev::Error, "TOO_LARGE  size: {}", msg->size);
      }
    } else {
      ++s.produce_fail;
      if (print_err) {
        LOG(Sev::Debug, "produce topic {}  partition {}   error: {}  {}",
            rd_kafka_topic_name(rkt), partition, x, rd_kafka_err2str(err));
      }
    }
  } else {
    ++s.produced;
    s.produced_bytes += (uint64_t)msg->size;
    ++producer->total_produced_;
    if (log_level >= 8) {
      LOG(Sev::Debug, "sent to topic {} partition {}", rd_kafka_topic_name(rkt),
          partition);
    }
    msg.release();
  }

  return x;
}

void ProducerTopic::do_copy() { _do_copy = true; }

ProducerMsg::~ProducerMsg() {}

void ProducerMsg::delivery_ok() {}

void ProducerMsg::delivery_fail() {}
}
