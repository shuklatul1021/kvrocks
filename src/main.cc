#include <getopt.h>
#include <event2/thread.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <csignal>

#include "worker.h"
#include "storage.h"
#include "version.h"
#include "config.h"
#include "server.h"

const char *kDefaultConfPath = "../kvrocks.conf";

std::function<void()> hup_handler;

struct Options {
  std::string conf_file = kDefaultConfPath;
  bool show_usage = false;
};

extern "C" void signal_handler(int sig) {
  if (hup_handler) hup_handler();
}

static void usage(const char* program) {
  std::cout << program << " implements the Redis protocol based on rocksdb\n"
            << "\t-c config file, default is " << kDefaultConfPath << "\n"
            << "\t-h help\n";
  exit(0);
}

static Options *parseCommandLineOptions(int argc, char **argv) {
  int ch;
  auto opts = new Options();
  while((ch = ::getopt(argc, argv, "c:h")) != -1) {
    switch (ch) {
      case 'c': opts->conf_file = optarg; break;
      case 'h': opts->show_usage = true; break;
      default: break;
    }
  }
  return opts;
}

static void initGoogleLog(const Config *config) {
  google::InitGoogleLogging("kvrocks");
  FLAGS_minloglevel = config->loglevel;
  FLAGS_max_log_size = 100;
  FLAGS_logbufsecs = 0;
  std::string log_filename = config->dir+"/kvrocks";
  google::SetLogDestination(google::INFO, log_filename.c_str());
  google::SetLogDestination(google::WARNING, log_filename.c_str());
  google::SetLogDestination(google::ERROR, log_filename.c_str());
  google::SetLogFilenameExtension(".LOG.");
}

int main(int argc, char* argv[]) {
  gflags::SetUsageMessage("kvrocks");
  evthread_use_pthreads();

  signal(SIGPIPE, SIG_IGN);
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  LOG(INFO) << "Version: " << VERSION << " @" << GIT_COMMIT;
  auto opts = parseCommandLineOptions(argc, argv);
  if (opts->show_usage) usage(argv[0]);
  std::string config_file_path = std::move(opts->conf_file);
  delete opts;

  Config config;
  Status s = config.Load(config_file_path);
  if (!s.IsOK()) {
    LOG(ERROR) << "Failed to load config, err: " << s.Msg();
    exit(1);
  }
  initGoogleLog(&config);
  Engine::Storage storage(&config);
  s = storage.Open();
  if (!s.IsOK()) {
    LOG(ERROR) << "failed to open: " << s.Msg();
    exit(1);
  }
  Server svr(&storage, &config);
  hup_handler = [&svr]() {
    LOG(INFO) << "bye bye";
    svr.Stop();
  };
  svr.Start();
  svr.Join();
  return 0;
}
