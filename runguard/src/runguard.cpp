#include <boost/lexical_cast.hpp>
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
#include <boost/stacktrace.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "run.hpp"
#include "system.hpp"
#include "utils.hpp"

using namespace std;

// for debug
std::ostream& operator<<(std::ostream& out, runguard_options& opt) {
    out << "croupname = " << opt.cgroupname << " chroot_dir = " << opt.chroot_dir << " work_dir = " << opt.work_dir << " preexecute = " << opt.preexecute << " user = " << opt.user << " group = " << opt.group << " cpuset = " << opt.cpuset << " metafile_path = " << opt.metafile_path;
    return out;
}

void validate(boost::any& v, const vector<string>& values, size_t*, int) {
    using namespace boost::program_options;
    validators::check_first_occurrence(v);

    string const& s = validators::get_single_string(values);
    if (s[0] == '-') {
        throw validation_error(validation_error::invalid_option_value);
    }

    v = boost::lexical_cast<size_t>(s);
}

void validate(boost::any& v, const vector<string>& values, struct time_limit*, int) {
    using namespace boost::program_options;
    validators::check_first_occurrence(v);

    struct time_limit result;
    string const& s = validators::get_single_string(values);
    auto colon = s.find(':');
    string left = s.substr(0, colon);
    string right = colon < s.size() ? s.substr(colon + 1) : "";

    result.soft = boost::lexical_cast<double>(left);
    if (right.size())
        result.hard = boost::lexical_cast<double>(right);
    else
        result.hard = result.soft;

    if (result.hard < result.soft ||
        !finite(result.hard) || !finite(result.soft) ||
        result.hard < 0 || result.soft < 0)
        throw validation_error(validation_error::invalid_option_value);

    v = result;
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
        boost::log::keywords::file_name = "runguard_%d_%m_%Y.%N.log",
        boost::log::keywords::rotation_size = 1 * 1024 * 1024,  // 每1M滚动一次日志
        boost::log::keywords::target = filesystem::path(getenv("BOOST_log_dir")) / "runguard",
        boost::log::keywords::min_free_space = 30 * 1024 * 1024, // 磁盘至少有30M空间
        boost::log::keywords::max_size = 20 * 1024 * 1024, // 最多存储20M日志
        boost::log::keywords::time_based_rotation = boost::log::sinks::file::rotation_at_time_point(boost::gregorian::greg_day(1)), // 或者每月1号滚动日志
        boost::log::keywords::scan_method = boost::log::sinks::file::scan_matching,
        boost::log::keywords::format = log_format,  // "[%TimeStamp%] [%Severity%]: %Message%",
        boost::log::keywords::auto_flush = true);
    // clang-format on

    boost::log::add_console_log(std::cout, boost::log::keywords::format = log_format);
}

int main(int argc, const char* argv[]) {
    ofstream out("/var/log/judge-system/runguard/label", std::ofstream::out);
    string s = "BOOST_log_dir = " + string{filesystem::path(getenv("BOOST_log_dir")).u8string()};
    out.write(s.c_str(), 100);

    init_boost_log();

    namespace po = boost::program_options;
    po::options_description desc("runguard options");
    po::positional_options_description pos;
    po::variables_map vm;

    struct runguard_options opt;

    // clang-format off
    desc.add_options()
        ("root,r", po::value<string>(), "run command with root directory set to root. If this option is provided, running command is executed relative to the chroot.")
        ("user,u", po::value<string>(), "run command as user with username or user id")
        ("work", po::value<string>(), "work directory for command")
        ("group,g", po::value<string>(), "run command under group with groupname or group id. If only 'user' is set, this defaults to the same")
        ("netns", po::value<string>(), "run command in specified network namespace, if not specified, runguard will create a new network namespace every time")
        ("wall-time,T", po::value<time_limit>(), "kill command after wall time clock seconds (floating point is acceptable)")
        ("cpu-time,t", po::value<time_limit>(), "set maximum CPU time (floating point is acceptable) consumption of the command in seconds")
        ("memory-limit,m", po::value<size_t>(), "set maximum memory consumption of the command in KB")
        ("file-limit,f", po::value<size_t>(), "set maximum created file size of the command in KB")
        ("nproc,p", po::value<size_t>(), "set maximum process living simutanously")
        ("cpuset,P", po::value<string>(), "set the processor IDs that can only be used (e.g. \"0,2-3\")")
        ("allowed-syscall", po::value<string>(), "set the limited syscall numbers in file separated by spaces")
        ("no-core-dumps,c", "disable core dumps")
        ("preexecute", po::value<string>(), "run command in new mount namespace before user program execution")
        ("standard-input-file,i", po::value<string>(), "redirect command standard input fd to file")
        ("standard-output-file,o", po::value<string>(), "redirect command standard output fd to file")
        ("standard-error-file,e", po::value<string>(), "redirect command standard error fd to file")
        ("environment,E", "preseve system environment variables (or only PATH is loaded)")
        ("variable,V", po::value<vector<string>>(), "add additional environment variables (e.g. -Vkey1=value1 -Vkey2=value2)")
        ("out-meta,M", po::value<string>(), "write runguard monitor results (run time, exitcode, memory usage, ...) to file")
        ("cmd", po::value<vector<string>>()->composing()->required(), "commands")
        ("help", "display this help text")
        ("version", "display version of this application");
    // clang-format on

    pos.add("cmd", -1);

    try {
        po::store(po::command_line_parser(argc, argv)
                      .options(desc)
                      .positional(pos)
                      .allow_unregistered()
                      .run(),
                  vm);
        po::notify(vm);
    } catch (po::error& e) {
        cerr << e.what() << endl
             << endl;
        cerr << desc << endl;
        return 1;
    }

    if (vm.count("help")) {
        cout << "Runguard: Running user program in protected mode with system resource access limitations." << endl
             << "This app requires root privilege if either 'root' or 'user' option is provided." << endl
             << "Usage: " << argv[0] << " [options] -- [command]";
        cout << desc << endl;
        return 0;
    }

    if (vm.count("version")) {
        cout << "runguard" << endl;
        return 0;
    }

    if (vm.count("root")) {
        opt.chroot_dir = vm["root"].as<string>();
    }

    if (vm.count("user")) {
        opt.user = vm["user"].as<string>();
        opt.user_id = get_userid(opt.user.c_str());
        opt.group_id = get_groupid(opt.user.c_str());  // debug: default the same
    }

    // if (vm.count("group") || vm.count("user")) { // origin
    if (vm.count("group")) {  // debug
        opt.group = vm["group"].as<string>();
        opt.group_id = get_groupid(opt.group.c_str());
    }

    if (vm.count("netns")) opt.netns = vm["netns"].as<string>();
    if (vm.count("preexecute")) opt.preexecute = vm["preexecute"].as<string>();
    if (vm.count("work")) opt.work_dir = vm["work"].as<string>();

    if (vm.count("variable")) {
        opt.env = vm["variable"].as<vector<string>>();
    }

    if (vm.count("wall-time")) opt.use_wall_limit = true, opt.wall_limit = vm["wall-time"].as<time_limit>();
    if (vm.count("cpu-time")) {
        opt.use_cpu_limit = true, opt.cpu_limit = vm["cpu-time"].as<time_limit>();
        if (!opt.use_wall_limit) {
            // wall limit must be taken
            opt.use_wall_limit = true;
            opt.wall_limit = opt.cpu_limit;
            opt.wall_limit.hard *= 3;
            opt.wall_limit.soft *= 3;
        }
    }
    if (vm.count("memory-limit")) {
        opt.memory_limit = vm["memory-limit"].as<size_t>();
        if (opt.memory_limit != (opt.memory_limit * 1024) / 1024)
            opt.memory_limit = -1;
        else
            opt.memory_limit *= 1024;
    }
    if (vm.count("file-limit")) {
        opt.file_limit = vm["file-limit"].as<size_t>();
        if (opt.file_limit != (opt.file_limit * 1024) / 1024)
            opt.file_limit = -1;
        else
            opt.file_limit *= 1024;
    }
    if (vm.count("nproc")) opt.nproc = vm["nproc"].as<size_t>();
    if (vm.count("no-core-dumps")) opt.no_core_dumps = true;
    if (vm.count("allowed-syscall")) {
        filesystem::path p = vm["allowed-syscall"].as<string>();
        if (filesystem::exists(p)) {
            ifstream fin(p);
            for (int no; fin >> no;) opt.syscalls.push_back(no);
        }
    }
    if (vm.count("cpuset")) opt.cpuset = vm["cpuset"].as<string>();
    if (vm.count("standard-input-file")) opt.stdin_filename = vm["standard-input-file"].as<string>();
    if (vm.count("standard-output-file")) opt.stdout_filename = vm["standard-output-file"].as<string>();
    if (vm.count("standard-error-file")) opt.stderr_filename = vm["standard-error-file"].as<string>();
    if (vm.count("environment")) opt.preserve_sys_env = true;
    if (vm.count("out-meta")) opt.metafile_path = vm["out-meta"].as<string>();
    opt.command = vm["cmd"].as<vector<string>>();

    BOOST_LOG_TRIVIAL(debug) << "opt: " << opt;

    return runit(opt);
}