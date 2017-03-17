#include "graph_store.h"

#include <sched.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <random>
#include <ctime>
#include <chrono>
#include <random>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cassert>
#include <vector>
#include <string>
#include <atomic>
#include <thread>
#include <iostream>
#include <algorithm>

#include "cmd_parse.h"

using namespace graphstore;

#define LOOP_OPS(op, gs, num_ops, limit) \
  num_ops = 0;\
  while (num_ops < limit) {\
    op(gs);\
    num_ops++;\
  }

#define SYNC_THREADS(cnt, nthreads)\
  cnt++;\
  while (cnt != nthreads)

#ifdef _GNU_SOURCE
#define SET_CORE_AFFINITY(thread, core_id)\
  fprintf(stderr, "Pinning thread to core %zu\n", core_id);
cpu_set_t cpuset;
CPU_ZERO(&cpuset);
CPU_SET(core_id, &cpuset);
int rc = pthread_setaffinity_np(thread.native_handle(), sizeof(cpu_set_t),
    &cpuset);
if (rc != 0)
fprintf(stderr, "Error calling pthread_setaffinity_np: %d\n", rc);
#else
#define SET_CORE_AFFINITY(thread, core_id)
#endif

#define BENCH_OP(op, n_threads)\
  std::string output_file = output_dir_ + "/throughput-" + #op + "-"\
     + std::to_string(n_threads) + ".txt";\
  bench_thput(op, n_threads, output_file);

#define DEFINE_BENCH(op)\
  void bench_##op(size_t nthreads) {\
    BENCH_OP(op, nthreads)\
  }

#define PREAMBLE_RNG\
  static thread_local std::mt19937 generator;\
  std::uniform_int_distribution<int64_t> distribution(0, INIT_NODES)

struct constants {
  static const uint64_t WARMUP_OPS = 50000;
  static const uint64_t MEASURE_OPS = 100000;
  static const uint64_t COOLDOWN_OPS = 50000;
};

typedef std::chrono::high_resolution_clock timer;

template<typename GraphOp>
static void bench_thput_thread(GraphOp&& op, graph_store& gs, size_t nthreads,
                               size_t i, std::vector<double>& thput) {
  size_t num_ops;

  fprintf(stderr, "Running warmup for %llu ops\n", constants::WARMUP_OPS);
  LOOP_OPS(op, gs, num_ops, constants::WARMUP_OPS);

  fprintf(stderr, "Warmup complete; running measure for %llu ops\n",
          constants::MEASURE_OPS);
  auto start = timer::now();
  LOOP_OPS(op, gs, num_ops, constants::MEASURE_OPS);
  auto end = timer::now();

  auto usecs = std::chrono::duration_cast<std::chrono::microseconds>(
      end - start).count();
  assert(usecs > 0);
  thput[i] = num_ops * 1e6 / usecs;

  fprintf(stderr, "Measure complete; running cooldown for %llu ops\n",
          constants::COOLDOWN_OPS);
  LOOP_OPS(op, gs, num_ops, constants::COOLDOWN_OPS);

  fprintf(stderr, "Thread completed benchmark at %lf ops/s\n", thput[i]);
}

class graph_store_perf {
 public:
  static const uint64_t NODE_TYPE = 1;
  static const uint64_t NUM_LINK_TYPES = 2;
  static const uint64_t INIT_NODES = 1000000;
  static const uint64_t INIT_DEGREE = 80;
  static const std::string DATA;

  graph_store_perf(const std::string& output_dir) {
    output_dir_ = output_dir;

    struct stat st = { 0 };
    if (stat(output_dir.c_str(), &st) == -1)
      mkdir(output_dir.c_str(), 0777);

    // Pre-load data
    fprintf(stderr, "Loading nodes...\n");
    node_op op = create_node_op();
    for (size_t i = 0; i < INIT_NODES; i++) {
      gs_.add_node(op);
    }
    fprintf(stderr, "Finished loading %llu nodes\n", INIT_NODES);

    fprintf(stderr, "Loading links...\n");
    uint64_t num_links = 0;
    for (uint64_t id1 = 0; id1 < INIT_NODES; id1++) {
      size_t degree = rand_int64(INIT_DEGREE);
      for (size_t j = 0; j < degree; j++) {
        int64_t link_type = rand_int64(NUM_LINK_TYPES);
        int64_t id2 = rand_int64(INIT_NODES);
        link_op op = create_link_op(id1, link_type, id2);
        gs_.add_link(op);
        num_links++;
        if (num_links % INIT_NODES == 0) {
          fprintf(stderr, "Finished loading %llu links\n", num_links);
        }
      }
    }
    fprintf(stderr, "Finished loading %llu links\n", num_links);
  }

  static void add_node(graph_store& gs) {
    node_op op = graph_store_perf::create_node_op();
    gs.add_node(op);
  }

  static void update_node(graph_store& gs) {
    PREAMBLE_RNG;
    int64_t id = distribution(generator);
    node_op op = create_node_op(id);
    gs.update_node(op);
  }

  static void delete_node(graph_store& gs) {
    PREAMBLE_RNG;
    int64_t id = distribution(generator);
    gs.delete_node(NODE_TYPE, id);
  };

  static void get_node(graph_store& gs) {
    PREAMBLE_RNG;
    int64_t id = distribution(generator);
    gs.get_node(NODE_TYPE, id);
  };

  static void add_link(graph_store& gs) {
    PREAMBLE_RNG;
    int64_t id1 = distribution(generator);
    int64_t id2 = distribution(generator);
    link_op op = create_link_op(id1, 0, id2);
    gs.add_link(op);
  };

  static void update_link(graph_store& gs) {
    PREAMBLE_RNG;
    int64_t id1 = distribution(generator);
    int64_t id2 = distribution(generator);
    link_op op = create_link_op(id1, 0, id2);
    gs.add_link(op);
  };

  static void delete_link(graph_store& gs) {
    PREAMBLE_RNG;
    int64_t id1 = distribution(generator);
    int64_t id2 = distribution(generator);
    gs.delete_link(id1, 0, id2);
  };

  static void get_link(graph_store& gs) {
    PREAMBLE_RNG;
    int64_t id1 = distribution(generator);
    int64_t id2 = distribution(generator);
    gs.get_link(id1, 0, id2);
  };

  static void get_link_list(graph_store& gs) {
    PREAMBLE_RNG;
    int64_t id1 = distribution(generator);
    gs.get_link_list(id1, 0);
  };

  static void count_links(graph_store& gs) {
    PREAMBLE_RNG;
    int64_t id1 = distribution(generator);
    gs.count_links(id1, 0);
  };

  static node_op create_node_op(uint64_t id = 0) {
    node_op op;
    op.data = DATA;
    op.type = NODE_TYPE;
    op.id = id;
    return op;
  }

  static link_op create_link_op(int64_t id1, int64_t link_type, int64_t id2) {
    link_op op;
    op.data = DATA;
    op.id1 = id1;
    op.link_type = link_type;
    op.id2 = id2;
    op.time = std::chrono::duration_cast<std::chrono::microseconds>(
        timer::now().time_since_epoch()).count();
    return op;
  }

  static int64_t rand_int64(const int64_t& max) {
    return rand_int64(0, max);
  }

  static int64_t rand_int64(const int64_t& min, const int64_t& max) {
    static thread_local std::mt19937 generator;
    std::uniform_int_distribution<int64_t> distribution(min, max);
    return distribution(generator);
  }

  template<typename GraphOp>
  void bench_thput(GraphOp&& op, const size_t n_threads,
      const std::string& output_file) {
    std::vector<std::thread> workers(n_threads);
    std::vector<double> thput(n_threads);
    for (size_t i = 0; i < n_threads; i++) {
      workers[i] = std::thread(bench_thput_thread<GraphOp>, std::ref(op), std::ref(gs_), n_threads, i, std::ref(thput));
      SET_CORE_AFFINITY(workers[i], i);
    }

    for (std::thread& worker : workers)
    worker.join();

    double thput_tot = std::accumulate(thput.begin(), thput.end(), 0.0);
    fprintf(stderr, "Completed benchmark at %lf ops/s\n", thput_tot);

    std::ofstream out(output_file);
    out << thput_tot << "\n";
    out.close();
  }

  DEFINE_BENCH(add_node)

  DEFINE_BENCH(update_node)

  DEFINE_BENCH(delete_node)

  DEFINE_BENCH(get_node)

  DEFINE_BENCH(add_link)

  DEFINE_BENCH(update_link)

  DEFINE_BENCH(delete_link)

  DEFINE_BENCH(get_link)

  DEFINE_BENCH(get_link_list)

  DEFINE_BENCH(count_links)

private:
  std::string output_dir_;
  graph_store gs_;
};

const std::string graph_store_perf::DATA = "123random123alphanumeric123text123";

int main(int argc, char** argv) {
  cmd_options opts;
  opts.add(
      cmd_option("num-threads", 't', false).set_default("1").set_description(
          "Number of benchmark threads"));
  opts.add(
      cmd_option("bench-type", 'b', false).set_default("get_node")
          .set_description("Benchmark type"));
  opts.add(
      cmd_option("output-dir", 'o', false).set_default(".").set_description(
          "Output directory"));

  cmd_parser parser(argc, argv, opts);
  if (parser.get_flag("help")) {
    fprintf(stderr, "%s\n", parser.help_msg().c_str());
    return 0;
  }

  int num_threads;
  std::string bench_type;
  std::string output_dir;
  try {
    num_threads = parser.get_int("num-threads");
    bench_type = parser.get("bench-type");
    output_dir = parser.get("output-dir");
  } catch (std::exception& e) {
    fprintf(stderr, "could not parse cmdline args: %s\n", e.what());
    fprintf(stderr, "%s\n", parser.help_msg().c_str());
    return 0;
  }

  fprintf(stderr, "Running benchmark %s with %d threads; output path %s\n",
          bench_type.c_str(), num_threads, output_dir.c_str());

  graph_store_perf perf(output_dir);
  if (bench_type == "add_node")
    perf.bench_add_node(num_threads);
  else if (bench_type == "update_node")
    perf.bench_update_node(num_threads);
  else if (bench_type == "delete_node")
    perf.bench_delete_node(num_threads);
  else if (bench_type == "get_node")
    perf.bench_get_node(num_threads);
  else if (bench_type == "add_link")
    perf.bench_add_link(num_threads);
  else if (bench_type == "update_link")
    perf.bench_update_link(num_threads);
  else if (bench_type == "delete_link")
    perf.bench_delete_link(num_threads);
  else if (bench_type == "get_link")
    perf.bench_get_link(num_threads);
  else if (bench_type == "get_link_list")
    perf.bench_get_link_list(num_threads);
  else if (bench_type == "count_links")
    perf.bench_count_links(num_threads);
  else
    fprintf(stderr, "Unknown bench_type: %s", bench_type.c_str());

  return 0;
}
