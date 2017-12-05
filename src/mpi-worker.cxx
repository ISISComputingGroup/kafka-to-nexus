#include "MMap.h"
#include "logger.h"
#include <atomic>
#include <chrono>
#include <mpi.h>
#include <thread>
#include <vector>

// getpid()
#include <sys/types.h>
#include <unistd.h>

#include <sys/mman.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <rapidjson/document.h>
#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "CollectiveQueue.h"
#include "HDFFile.h"
#include "HDFWriterModule.h"
#include "MsgQueue.h"
#include "helper.h"
#include "json.h"
#include "logpid.h"

using std::array;
using std::vector;
using std::string;

using CLK = std::chrono::steady_clock;
using MS = std::chrono::milliseconds;

int main(int argc, char **argv) {
  int err = MPI_SUCCESS;
  err = MPI_Init(&argc, &argv);
  if (err != MPI_SUCCESS) {
    LOG(3, "fail MPI_Init");
    exit(1);
  }

  int rank_world, size_world;
  MPI_Comm_rank(MPI_COMM_WORLD, &rank_world);
  MPI_Comm_size(MPI_COMM_WORLD, &size_world);
  LOG(3, "mpi-worker  rank_world: {}  size_world: {}", rank_world, size_world);
  MPI_Comm comm_parent;
  err = MPI_Comm_get_parent(&comm_parent);
  if (err != MPI_SUCCESS) {
    LOG(3, "fail MPI_Comm_get_parent");
    exit(1);
  }

  {
    int rank, size;
    MPI_Comm_rank(comm_parent, &rank);
    MPI_Comm_size(comm_parent, &size);
    LOG(3, "comm_parent rank: {}  size: {}", rank, size);
  }

  using rapidjson::Value;
  rapidjson::Document jconf;
  {
    MPI_Status status;
    std::vector<char> buf;
    buf.resize(1024 * 1024);
    err = MPI_Recv(buf.data(), buf.size(), MPI_CHAR, MPI_ANY_SOURCE, 101,
                   comm_parent, &status);
    if (err != MPI_SUCCESS) {
      LOG(3, "fail MPI_Recv");
      exit(1);
    }
    int size = -1;
    err = MPI_Get_count(&status, MPI_CHAR, &size);
    if (err != MPI_SUCCESS) {
      LOG(3, "fail MPI_Get_count");
      exit(1);
    }
    jconf.Parse(buf.data(), buf.size());
    if (jconf.HasParseError()) {
    }
  }

  int rank_merged, size_merged;
  MPI_Comm comm_all;
  int comm_all_rank = -1;
  {
    err = MPI_Intercomm_merge(comm_parent, 1, &comm_all);
    if (err != MPI_SUCCESS) {
      LOG(3, "fail MPI_Intercomm_merge");
      exit(1);
    }
    MPI_Comm_rank(comm_all, &rank_merged);
    MPI_Comm_size(comm_all, &size_merged);
    LOG(3, "comm_all  rank_merged: {}  size_merged: {}", rank_merged,
        size_merged);
    comm_all_rank = rank_merged;
  }

  logpid(fmt::format("tmp-pid-worker-{}.txt", rank_merged).c_str());
  if (jconf.FindMember("logpid-sleep") != jconf.MemberEnd()) {
    LOG(3, "logpid sleep ...");
    sleep_ms(3000);
  }

  auto config_file = jconf["config_file"].GetObject();
  auto shm_fname = config_file["shm"]["fname"].GetString();
  auto shm_size = config_file["shm"]["size"].GetInt64();
  LOG(3, "mmap {} / {}", shm_fname, shm_size);
  auto shm = MMap::create(shm_fname, shm_size);
  LOG(3, "memory ready");

  using namespace FileWriter;

  auto queue = (MsgQueue *)jconf["queue_addr"].GetUint64();
  auto cq = (CollectiveQueue *)jconf["cq_addr"].GetUint64();
  LOG(3, "got cq at: {}", (void *)cq);
  HDFIDStore hdf_store;
  hdf_store.mpi_rank = rank_merged;
  hdf_store.cqid = cq->open(hdf_store);
  LOG(3, "rank_merged: {}  cqid: {}", rank_merged, hdf_store.cqid);

  auto hdf_fname = jconf["hdf"]["fname"].GetString();
  auto hdf_file = std::unique_ptr<HDFFile>(new HDFFile);
  // no need to set the cq ptr on hdffile here, it is just the resource owner in
  // main process.
  LOG(7, "hdf_file->reopen()  {}", hdf_fname);
  hdf_file->reopen(hdf_fname, Value());
  hdf_store.h5file = hdf_file->h5file;

  auto module = jconf["stream"]["module"].GetString();

  LOG(7, "HDFWriterModuleRegistry::find(module)  {}", module);
  auto module_factory = HDFWriterModuleRegistry::find(module);
  if (!module_factory) {
    LOG(5, "Module '{}' is not available", module);
    exit(1);
  }

  LOG(7, "module_factory()");
  auto hdf_writer_module = module_factory();
  if (!hdf_writer_module) {
    LOG(5, "Can not create a HDFWriterModule for '{}'", module);
    exit(1);
  }

  LOG(7, "hdf_writer_module->parse_config()");
  hdf_writer_module->parse_config(jconf["stream"], nullptr);
  LOG(7, "hdf_writer_module->reopen()");
  hdf_writer_module->reopen(hdf_file->h5file,
                            jconf["stream"]["hdf_parent_name"].GetString(), cq,
                            &hdf_store);
  // jconf["stream"]["hdf_parent_name"].GetString()

  LOG(7, "hdf_writer_module->enable_cq()");
  hdf_writer_module->enable_cq(cq, &hdf_store, rank_merged);

  LOG(3, "Barrier 1 BEFORE");
  sleep_ms(2000);
  MPI_Barrier(comm_all);
  LOG(3, "Barrier 1 AFTER");

  auto t_last = CLK::now();

  size_t empties = 0;
  // NOTE
  // This loop will prevent it from running a long time on idle;
  for (int i1 = 0; i1 < 10000; ++i1) {
    std::vector<Msg> all;
    queue->all(all, size_merged);
    if (all.size() > 0) {
      // reset idle counter
      i1 = 0;
      for (auto &m : all) {
        auto t_now = CLK::now();
        // execute all pending commands before the next message
        if (true || t_now - t_last > MS(100)) {
          t_last = t_now;
          cq->execute_for(hdf_store, 0);
        }
        // LOG(3, "writing msg  type: {:2}  size: {:5}  data: {}", m.type,
        // m._size, (void*)m.data());
        hdf_writer_module->write(m);
      }
    } else {
      if (queue->open != 1) {
        LOG(7, "queue closed");
        break;
      } else {
        if (empties % 1000 == 0) {
          LOG(7, "empty {}", empties);
        }
        empties += 1;
        sleep_ms(1);
      }
    }
  }

  auto barrier = [&cq, &hdf_store](size_t id, size_t queue, std::string name) {
    LOG(3, "...............................  cqid: {}  wait   {}  {}",
        hdf_store.cqid, id, name);
    cq->barriers[id]++;
    cq->wait_for_barrier(&hdf_store, id, queue);
    LOG(3, "===============================  cqid: {}  after  {}  {}",
        hdf_store.cqid, id, name);
  };

  barrier(0, 0, "MODULE RESET");
  hdf_writer_module.reset();
  cq->close_for(hdf_store);

  barrier(1, 0, "CQ EXEC");

  barrier(2, 1, "CQ EXEC 2");

  barrier(5, 2, "CQ EXEC 3");

  LOG(6, "check_all_empty");
  hdf_store.check_all_empty();

  LOG(6, "hdf_file.reset()");
  hdf_file.reset();

  barrier(3, 2, "MPI Barrier");
  err = MPI_Barrier(comm_all);
  if (err != MPI_SUCCESS) {
    LOG(3, "fail MPI_Barrier");
    exit(1);
  }

  LOG(6, "ask for disconnect  cqid: {}", hdf_store.cqid);
  // MPI_Comm_disconnect(&comm_parent);
  err = MPI_Comm_disconnect(&comm_all);
  if (err != MPI_SUCCESS) {
    LOG(3, "fail MPI_Comm_disconnect");
    exit(1);
  }

  barrier(4, -1, "Last CQ barrier");
  if (false) {
    LOG(6, "finalizing {}", rank_merged);
    MPI_Finalize();
    LOG(6, "after finalize {}", rank_merged);
  }
  LOG(6, "return");
  return 42;

  LOG(3, "wait for parent mmap");
  MPI_Barrier(comm_all);

  MPI_Barrier(comm_all);

  auto m1 = (std::atomic<uint32_t> *)shm->addr();
  while (m1->load() < 110) {
    while (m1->load() % 2 == 0) {
    }
    LOG(3, "store");
    m1->store(m1->load() + 1);
  }
  LOG(3, "final: value: {}", m1->load());

  MPI_Barrier(comm_all);
  LOG(3, "alloc init");

  MPI_Barrier(comm_all);
  LOG(3, "ask for disconnect");
  MPI_Comm_disconnect(&comm_parent);
  MPI_Comm_disconnect(&comm_all);
  LOG(3, "finalizing {}", rank_merged);
  MPI_Finalize();
  LOG(3, "after finalize {}", rank_merged);
  return 42;
}
