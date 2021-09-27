#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#include <boost/algorithm/string.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/filesystem.hpp>
#include <boost/log/attributes.hpp>
#include <boost/log/common.hpp>
#include <boost/log/core.hpp>
#include <boost/log/expressions.hpp>
#include <boost/log/sinks.hpp>
#include <boost/log/support/date_time.hpp>
#include <boost/log/trivial.hpp>
#include <boost/log/utility/setup/common_attributes.hpp>
#include <boost/log/utility/setup/console.hpp>
#include <boost/log/utility/setup/file.hpp>
#include <boost/program_options.hpp>
#include <iostream>
#include <regex>
#include <set>
#include <thread>

#include "common/concurrent_queue.hpp"
#include "common/messages.hpp"
#include "common/system.hpp"
#include "common/utils.hpp"
#include "config.hpp"
#include "env.hpp"
#include "judge/choice.hpp"
#include "judge/program_output.hpp"
#include "judge/programming.hpp"
#include "logging.hpp"
#include "metrics.hpp"
#include "monitor/interrupt_monitor.hpp"
#include "monitor/prometheus.hpp"
#include "server/forth/forth.hpp"
#include "server/mcourse/mcourse.hpp"
#include "server/sicily/sicily.hpp"
#include "worker.hpp"
using namespace std;

namespace logging = boost::log;

judge::concurrent_queue<judge::message::client_task> testcase_queue;
judge::concurrent_queue<judge::message::core_request> core_acq_queue;

struct cpuset {
    string literal;
    set<unsigned> ids;
    cpu_set_t cpuset;
};

cpuset parse_cpuset(const string s) {
    using namespace boost::program_options;
    static regex matcher("^([0-9]+)(-([0-9]+))?$");
    cpuset result;
    CPU_ZERO(&result.cpuset);

    result.literal = s;
    vector<string> splitted;
    boost::split(splitted, s, boost::is_any_of(","));
    for (auto& token : splitted) {
        smatch matches;
        if (regex_search(token, matches, matcher)) {
            if (matches[3].str().empty()) {
                int i = boost::lexical_cast<int>(matches[1].str());
                result.ids.insert(i);
                CPU_SET(i, &result.cpuset);
            } else {
                int begin = boost::lexical_cast<int>(matches[1].str());
                int end = boost::lexical_cast<int>(matches[3].str());
                if (begin > end || begin < 0 || end < 0)
                    throw validation_error(validation_error::invalid_option_value);
                for (unsigned i = (unsigned)begin; i <= (unsigned)end; ++i) {
                    result.ids.insert(i);
                    CPU_SET(i, &result.cpuset);
                }
            }
        } else {
            throw validation_error(validation_error::invalid_option_value);
        }
    }
    return result;
}

void validate(boost::any& v, const vector<string>& values, cpuset*, int) {
    using namespace boost::program_options;
    validators::check_first_occurrence(v);

    string const& s = validators::get_single_string(values);
    v = parse_cpuset(s);
}

int sigint = 0;

void sigintHandler(int signum) {
    if (signum == SIGINT) {
        if (sigint == 0) {
            LOG_ERROR << "Received SIGINT, stopping workers (Press Ctrl+C again to stop judging)";
        } else if (sigint == 1) {
            LOG_ERROR << "Received SIGINT, stopping judging (Press Ctrl+C again to terminate this app)";
        } else {
            LOG_ERROR << "Received SIGINT, terminating";
        }
    } else if (signum == SIGTERM) {
        LOG_ERROR << "Received SIGTERM, stopping workers";
    }

    if (sigint == 0)
        judge::stop_workers();
    else if (sigint == 1)
        judge::stop_judging();
    else
        exit(130);

    sigint++;
}

void init_boost_log() {
    boost::log::add_common_attributes();
    auto core = boost::log::core::get();

    core->add_global_attribute("UTCTimeStamp", boost::log::attributes::utc_clock());

    // clang-format off
    auto log_format(
        boost::log::expressions::stream << 
            "[" << boost::log::expressions::format_date_time<boost::posix_time::ptime>("TimeStamp", "%Y-%m-%d %H:%M:%S") << 
            "] [" << boost::log::expressions::attr<boost::log::attributes::current_thread_id::value_type>("ThreadID") << 
            "] [" << std::left << std::setw(7) << std::setfill(' ') << boost::log::trivial::severity << 
            "] " << boost::log::expressions::smessage 
        );
    
    boost::log::add_file_log(
        boost::log::keywords::file_name = "judge_%d_%m_%Y.%N.log",
        boost::log::keywords::rotation_size = 1 * 1024 * 1024,  // 每1M滚动一次日志
        boost::log::keywords::target = filesystem::path(getenv("BOOST_log_dir")),
        boost::log::keywords::min_free_space = 30 * 1024 * 1024, // 磁盘至少有30M空间
        boost::log::keywords::max_size = 20 * 1024 * 1024, // 最多存储20M日志
        boost::log::keywords::time_based_rotation = boost::log::sinks::file::rotation_at_time_point(boost::gregorian::greg_day(1)), // 或者每月1号滚动日志
        boost::log::keywords::scan_method = boost::log::sinks::file::scan_matching,
        boost::log::keywords::format = log_format,  // "[%TimeStamp%] [%Severity%]: %Message%",
        boost::log::keywords::auto_flush = true);
    // clang-format on

    boost::log::add_console_log(std::cout, boost::log::keywords::format = log_format);
}

int main(int argc, char* argv[]) {
    if (!getenv("BOOST_log_dir")) {
        cerr << "No BOOST_log_dir provided." << endl;
        return EXIT_FAILURE;
    }
    init_boost_log();

    filesystem::path current(argv[0]);
    filesystem::path repo_dir(filesystem::weakly_canonical(current).parent_path().parent_path());

    signal(SIGINT, sigintHandler);
    signal(SIGTERM, sigintHandler);

    // 默认情况下，假设运行环境是拉取代码直接编译的环境，此时我们可以假定 runguard 的运行路径
    if (!getenv("RUNGUARD")) {
        filesystem::path runguard(repo_dir / "runguard" / "bin" / "runguard");
        if (filesystem::exists(runguard)) {
            set_env("RUNGUARD", filesystem::weakly_canonical(runguard).string());
            LOG_INFO << "Set env RUNGUARD successfully.";
        }
    }

    if (!getenv("RUNGUARD")) {  // RUNGUARD 环境变量将传给 exec/check/standard/run 评测脚本使用
        LOG_FATAL << "RUNGUARD environment variable should be specified. This env points out where the runguard executable locates in.";
        exit(1);
    }

    judge::put_error_codes();

    /*** handle options ***/

    namespace po = boost::program_options;
    po::options_description desc("judge-system options");
    po::variables_map vm;

    // clang-format off
    desc.add_options()
        ("enable-sicily", po::value<vector<string>>(), "run Sicily Online Judge submission fetcher, with configuration file path.")
        ("enable", po::value<vector<string>>(), "load fetcher configurations in given directory with extension .json")
        ("enable-4", po::value<vector<string>>(), "run Matrix Judge System 4.0 submission fetcher, with configuration file path.")
        ("enable-3", po::value<vector<string>>(), "run Matrix Judge System 3.0 submission fetcher, with configuration file path.")
        ("enable-2", po::value<vector<string>>(), "run Matrix Judge System 2.0 submission fetcher, with configuration file path.")
        ("monitor", po::value<string>(), "set monitor diagnostics to monitor system")
        ("cores", po::value<cpuset>(), "set the cores the judge-system can make use of. You can either pass it from environ CORES")
        ("exec-dir", po::value<string>(), "set the default predefined executables for falling back. You can either pass it from environ EXECDIR")
        ("script-dir", po::value<string>(), "set the directory with required scripts stored. You can either pass it from environ SCRIPTDIR")
        ("cache-dir", po::value<string>(), "set the directory to store cached test data, compiled spj, random test generator, compiled executables. You can either pass it from environ CACHEDIR")
        ("data-dir", po::value<string>(), "set the directory to store test data to be judged, for ramdisk to speed up IO performance of user program. You can either pass it from environ DATADIR")
        ("run-dir", po::value<string>(), "set the directory to run user programs, store compiled user program. You can either pass it from environ RUNDIR")
        ("chroot-dir", po::value<string>(), "set the chroot directory. You can either pass it from environ CHROOTDIR")
        ("script-mem-limit", po::value<unsigned>(), "set memory limit in KB for random data generator, scripts, default to 262144(256MB). You can either pass it from environ SCRIPTMEMLIMIT")
        ("script-time-limit", po::value<unsigned>(), "set time limit in seconds for random data generator, scripts, default to 10(10 second). You can either pass it from environ SCRIPTTIMELIMIT")
        ("script-file-limit", po::value<unsigned>(), "set file limit in KB for random data generator, scripts, default to 524288(512MB). You can either pass it from environ SCRIPTFILELIMIT")
        ("run-user", po::value<string>(), "set run user. You can either pass it from environ RUNUSER")
        ("run-group", po::value<string>(), "set run group. You can either pass it from environ RUNGROUP")
        ("cache-random-data", po::value<size_t>(), "set the maximum number of cached generated random data, default to 100. You can either pass it from environ CACHERANDOMDATA")
        ("max-io-size", po::value<size_t>(), "set the maximum bytes to be read from a file, default to unlimited. You can either pass it from environ MAXIOSIZE")
        ("debug", "turn on the debug mode to disable checking whether it is in privileged mode, and not to delete submission directory to check the validity of result files. You can either pass it from environ DEBUG")
        ("help", "display this help text")
        ("version", "display version of this application")
        ("metric-addr", po::value<string>(), "set the address that Prometheus-metrics exposed. Fomat x.x.x.x:x");
    // clang-format on

    try {
        po::store(po::command_line_parser(argc, argv)
                      .options(desc)
                      .run(),
                  vm);
        po::notify(vm);
    } catch (po::error& e) {
        cerr << e.what() << endl
             << endl;
        cerr << desc << endl;
        return EXIT_FAILURE;
    }

    if (vm.count("help")) {
        cout << "JudgeSystem: Fetch submissions from server, judge them" << endl
             << "This app requires root privilege" << endl
             << "Required Environment Variables:" << endl
             << "\tRUNGUARD: location of runguard" << endl
             << "Usage: " << argv[0] << " [options]" << endl;
        cout << desc << endl;
        return EXIT_SUCCESS;
    }

    if (vm.count("version")) {
        cout << "judge-system 4.0" << endl;
        return EXIT_SUCCESS;
    }

    if (vm.count("debug")) {
        judge::DEBUG = true;
    } else if (getenv("DEBUG")) {
        judge::DEBUG = true;
    }

    if (getuid() != 0) {
        cerr << "You should run this program in privileged mode" << endl;
        if (!judge::DEBUG) return EXIT_FAILURE;
    }

    if (vm.count("exec-dir")) {
        judge::EXEC_DIR = filesystem::path(vm.at("exec-dir").as<string>());
    } else if (getenv("EXECDIR")) {
        judge::EXEC_DIR = filesystem::path(getenv("EXECDIR"));
    } else {
        filesystem::path execdir(repo_dir / "exec");
        if (filesystem::exists(execdir)) {
            judge::EXEC_DIR = execdir;
        }
    }

    LOG_DEBUG << "EXEC_DIR = " << judge::EXEC_DIR;

    set_env("JUDGE_UTILS", judge::EXEC_DIR / "utils", true);
    if (!filesystem::is_directory(judge::EXEC_DIR)) {
        LOG_FATAL << "Executables directory " << judge::EXEC_DIR << " does not exist";
        exit(1);
    }

    for (auto& p : filesystem::directory_iterator(judge::EXEC_DIR))
        if (filesystem::is_regular_file(p))
            filesystem::permissions(p,
                                    filesystem::perms::group_exec | filesystem::perms::others_exec | filesystem::perms::owner_exec,
                                    filesystem::perm_options::add);

    if (vm.count("script-dir")) {
        judge::SCRIPT_DIR = filesystem::path(vm.at("script-dir").as<string>());
    } else if (getenv("SCRIPTDIR")) {
        judge::SCRIPT_DIR = filesystem::path(getenv("SCRIPTDIR"));
    } else {
        filesystem::path scriptdir(repo_dir / "script");
        if (filesystem::exists(scriptdir)) {
            judge::SCRIPT_DIR = scriptdir;
        }
    }

    if (vm.count("cache-dir")) {
        judge::CACHE_DIR = filesystem::path(vm.at("cache-dir").as<string>());
    } else if (getenv("CACHEDIR")) {
        judge::CACHE_DIR = filesystem::path(getenv("CACHEDIR"));
    }
    if (!filesystem::exists(judge::CACHE_DIR) && !filesystem::create_directories(judge::CACHE_DIR))
        LOG_FATAL << "Cache directory " << judge::CACHE_DIR << " cannot be created";

    if (vm.count("data-dir")) {
        judge::DATA_DIR = filesystem::path(vm.at("data-dir").as<string>());
        judge::USE_DATA_DIR = true;
    } else if (getenv("DATADIR")) {
        judge::DATA_DIR = filesystem::path(getenv("DATADIR"));
        judge::USE_DATA_DIR = true;
    }
    if (judge::USE_DATA_DIR) {
        if (!filesystem::exists(judge::DATA_DIR) && !filesystem::create_directories(judge::DATA_DIR))
            LOG_FATAL << "Data directory " << judge::DATA_DIR << " cannot be created";
    }

    if (vm.count("run-dir")) {
        judge::RUN_DIR = filesystem::path(vm.at("run-dir").as<string>());
    } else if (getenv("RUNDIR")) {
        judge::RUN_DIR = filesystem::path(getenv("RUNDIR"));
    }
    if (!filesystem::exists(judge::RUN_DIR) && !filesystem::create_directories(judge::RUN_DIR))
        LOG_FATAL << "Run directory " << judge::RUN_DIR << " cannot be created";

    if (vm.count("chroot-dir")) {
        judge::CHROOT_DIR = filesystem::path(vm.at("chroot-dir").as<string>());
    } else if (getenv("CHROOTDIR")) {
        judge::CHROOT_DIR = filesystem::path(getenv("CHROOTDIR"));
    }
    if (!filesystem::is_directory(judge::CHROOT_DIR)) {
        LOG_FATAL << "Chroot directory " << judge::CHROOT_DIR << " does not exist";
        exit(1);
    }

    if (vm.count("script-mem-limit")) {
        judge::SCRIPT_MEM_LIMIT = vm["script-mem-limit"].as<unsigned>();
    } else if (getenv("SCRIPTMEMLIMIT")) {
        judge::SCRIPT_MEM_LIMIT = boost::lexical_cast<unsigned>(getenv("SCRIPTMEMLIMIT"));
    }
    set_env("SCRIPTMEMLIMIT", to_string(judge::SCRIPT_MEM_LIMIT), false);

    if (vm.count("script-time-limit")) {
        judge::SCRIPT_TIME_LIMIT = vm["script-time-limit"].as<unsigned>();
    } else if (getenv("SCRIPTTIMELIMIT")) {
        judge::SCRIPT_TIME_LIMIT = boost::lexical_cast<unsigned>(getenv("SCRIPTTIMELIMIT"));
    }
    set_env("SCRIPTTIMELIMIT", to_string(judge::SCRIPT_TIME_LIMIT), false);

    if (vm.count("script-file-limit")) {
        judge::SCRIPT_FILE_LIMIT = vm["script-file-limit"].as<unsigned>();
    } else if (getenv("SCRIPTFILELIMIT")) {
        judge::SCRIPT_FILE_LIMIT = boost::lexical_cast<unsigned>(getenv("SCRIPTFILELIMIT"));
    }
    set_env("SCRIPTFILELIMIT", to_string(judge::SCRIPT_FILE_LIMIT), false);

    if (vm.count("run-user")) {
        string runuser = vm["run-user"].as<string>();
        set_env("RUNUSER", runuser);
    }

    if (vm.count("run-group")) {
        string rungroup = vm["run-group"].as<string>();
        set_env("RUNGROUP", rungroup);
    }

    // 让评测系统写入的数据只允许当前用户写入
    umask(0022);

    if (vm.count("cache-random-data")) {
        judge::MAX_RANDOM_DATA_NUM = vm["cache-random-data"].as<size_t>();
    } else if (getenv("CACHERANDOMDATA")) {
        judge::MAX_RANDOM_DATA_NUM = boost::lexical_cast<unsigned>(getenv("CACHERANDOMDATA"));
    }

    if (vm.count("max-io-size")) {
        judge::MAX_IO_SIZE = vm["max-io-size"].as<long>();
    } else if (getenv("MAXIOSIZE")) {
        judge::MAX_IO_SIZE = boost::lexical_cast<unsigned>(getenv("MAXIOSIZE"));
    }

    if (vm.count("enable-sicily")) {
        auto sicily_servers = vm.at("enable-scicily").as<vector<string>>();
        for (auto& sicily_server : sicily_servers) {
            if (!filesystem::is_regular_file(sicily_server)) {
                LOG_FATAL << "Configuration file " << sicily_server << " does not exist";
                exit(1);
            }

            auto sicily_judger = make_unique<judge::server::sicily::configuration>();
            sicily_judger->init(sicily_server);
            judge::register_judge_server(move(sicily_judger));
        }
    }

    if (vm.count("enable-4")) {
        auto forth_servers = vm.at("enable-4").as<vector<string>>();
        for (auto& forth_server : forth_servers) {
            if (!filesystem::is_regular_file(forth_server)) {
                LOG_FATAL << "Configuration file " << forth_server << " does not exist";
                exit(1);
            }

            LOG_INFO << "Find Configuration file " << forth_server;

            auto forth_judger = make_unique<judge::server::forth::configuration>();
            forth_judger->init(forth_server);
            judge::register_judge_server(move(forth_judger));
        }
    }

    if (vm.count("enable-2")) {
        auto second_servers = vm.at("enable-2").as<vector<string>>();
        for (auto& second_server : second_servers) {
            if (!filesystem::is_regular_file(second_server)) {
                LOG_FATAL << "Configuration file " << second_server << " does not exist";
                exit(1);
            }

            auto second_judger = make_unique<judge::server::mcourse::configuration>();
            second_judger->init(second_server);
            judge::register_judge_server(move(second_judger));
        }
    }

    vector<string> servers;
    if (vm.count("enable")) {
        servers = vm.at("enable").as<vector<string>>();
    }

    if (getenv("CONFIGDIR")) {
        string configdir = getenv("CONFIGDIR");
        boost::split(servers, configdir, boost::is_any_of(":"));
    }

    vector<filesystem::path> config;
    for (auto& server : servers) {
        if (filesystem::is_directory(server)) {
            for (const auto& p : filesystem::directory_iterator(server)) {
                auto& path = p.path();
                if (path.extension() == ".json")
                    config.push_back(path);
            }
        } else if (filesystem::is_regular_file(server)) {
            config.emplace_back(server);
        } else {
            LOG_FATAL << "Configuration file " << server << " does not exist";
        }
    }

    for (const auto& p : config) {
        try {
            LOG_INFO << "Enable config file " << p;
            nlohmann::json j = nlohmann::json::parse(judge::read_file_content(p));
            string type = j.at("type").get<string>();
            if (type == "mcourse") {
                auto second_judger = make_unique<judge::server::mcourse::configuration>();
                second_judger->init(p);
                judge::register_judge_server(move(second_judger));
            } else if (type == "sicily") {
                auto sicily_judger = make_unique<judge::server::sicily::configuration>();
                sicily_judger->init(p);
                judge::register_judge_server(move(sicily_judger));
            } else if (type == "forth") {
                auto forth_judger = make_unique<judge::server::forth::configuration>();
                forth_judger->init(p);
                judge::register_judge_server(move(forth_judger));
            } else {
                LOG_FATAL << "Unrecognized configuration type " << type << " in file " << p;
            }
        } catch (std::exception& e) {
            LOG_WARN << "Configuration file " << p << " is malformed: " << e.what();
        }
    }

    /*** judger ***/

    judge::register_judger(make_unique<judge::programming_judger>());
    judge::register_judger(make_unique<judge::choice_judger>());
    judge::register_judger(make_unique<judge::program_output_judger>());

    /*** monitor ***/

    judge::register_monitor(make_unique<judge::interrupt_monitor>());

    /*** metrics ***/

    std::string metric_addr = "0.0.0.0:9090";
    if (getenv("METRIC_ADDR")) {
        metric_addr = getenv("METRIC_ADDR");
    }
    if (vm.count("metric-addr")) {
        metric_addr = vm["metric-addr"].as<string>();
        set_env("METRIC_ADDR", metric_addr);
    }

    prometheus::Exposer exposer{metric_addr, 2};

    auto registry = judge::metrics::global_registry();
    auto& up_gauge = prometheus::BuildGauge()
                         .Name("judge_system_up")
                         .Help("Whether the judge system is up")
                         .Register(*registry)
                         .Add({});
    auto& worker_gauge = prometheus::BuildGauge()
                             .Name("judge_system_workers")
                             .Help("How many workers are running")
                             .Register(*registry)
                             .Add({});
    judge::register_monitor(make_unique<judge::prometheus_monitor>(registry));
    exposer.RegisterCollectable(registry);

    /*** worker ***/

    LOG_DEBUG << "Start working on workers";

    vector<thread> worker_threads;

    // 我们为每个注册的 CPU 核心 都生成一个 worker
    cpuset set;
    if (vm.count("cores")) {
        set = vm["cores"].as<cpuset>();
    } else {
        if (!getenv("CORES")) {
            LOG_FATAL << "you must specifiy usable CPU cores by adding option --cores or environ CORES";
            exit(1);
        }
        set = parse_cpuset(getenv("CORES"));
    }

    LOG_DEBUG << "cpuset: cpuset.literal = " << set.literal;

    judge::set_running_workers(set.ids);

    for (unsigned i : set.ids) {
        worker_threads.push_back(move(judge::start_worker(i, testcase_queue, core_acq_queue)));
    }

    LOG_INFO << "Started " << set.ids.size() << " workers";
    worker_gauge.Set(set.ids.size());
    up_gauge.Set(1);

    for (auto& th : worker_threads)
        th.join();

    return 0;
}
