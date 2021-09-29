#include "judge/programming.hpp"

#include <signal.h>

#include <boost/algorithm/algorithm.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/assign/list_of.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/range/algorithm.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <fstream>
#include <iostream>
#include <sstream>

#include "common/defer.hpp"
#include "common/net_utils.hpp"
#include "common/stl_utils.hpp"
#include "common/utils.hpp"
#include "config.hpp"
#include "logging.hpp"
#include "runguard.hpp"
#include "server/judge_server.hpp"

namespace judge {
using namespace std;

extern vector<unique_ptr<monitor>> monitors;

test_case_data::test_case_data() {}

test_case_data::test_case_data(test_case_data &&other)
    : inputs(move(other.inputs)), outputs(move(other.outputs)) {}

judge_task_result::judge_task_result() : judge_task_result("", 0) {}
judge_task_result::judge_task_result(const std::string &tag, size_t id)
    : tag(tag), id(id), score(0), run_time(0), memory_used(0) {}

static filesystem::path get_run_path(const unique_ptr<executable> &ptr) {
    return !ptr ? filesystem::path{} : ptr->get_run_path();
}

static filesystem::path get_work_dir(const programming_submission &submit) {
    filesystem::path workdir = RUN_DIR / submit.category / submit.sub_id;
    filesystem::create_directories(workdir);
    return workdir;
}

static filesystem::path get_cache_dir(const programming_submission &submit) {
    filesystem::path workdir = get_work_dir(submit);  // 本提交的工作文件夹
    filesystem::path cachedir = submit.prob_id.empty()
                                    ? workdir / "cache"                              // prob_id 不存在时当做 playground，不缓存题目数据，将题目缓存放进提交工作文件夹里即可
                                    : CACHE_DIR / submit.category / submit.prob_id;  // 题目的缓存文件夹
    filesystem::create_directories(cachedir);
    return cachedir;
}

bool generate_random_data(const filesystem::path &datadir, const filesystem::path &cachedir, int number, programming_submission &submit, judge_task &task, judge_task_result &result, const string &execcpuset) {
    elapsed_time random_time;

    auto &exec_mgr = submit.judge_server->get_executable_manager();
    auto run_script = exec_mgr.get_run_script(task.run_script);
    run_script->fetch(execcpuset, CHROOT_DIR, exec_mgr);

    filesystem::path errorpath = datadir / ".error";  // 文件存在表示该组测试数据生成失败
    // 随机生成器和标准程序已经在编译阶段完成下载和编译
    filesystem::path randomdir = cachedir / "random";
    filesystem::path standarddir = cachedir / "standard";
    task.subcase_id = number;  // 标记当前测试点使用了哪个随机测试点
    // random_generator.sh <random_case> <random_gen_compile> <random_gen> <std_program_compile> <std_program> <timelimit> <chrootdir> <datadir> <run> <std_program run_args...>
    int ret = process_builder().run(EXEC_DIR / "random_generator.sh",
                                    "-n", execcpuset, "--",
                                    task.testcase_id,
                                    get_run_path(submit.random->get_compile_script(exec_mgr)), submit.random->get_run_path(randomdir),
                                    get_run_path(submit.standard->get_compile_script(exec_mgr)), submit.standard->get_run_path(standarddir),
                                    task.time_limit, CHROOT_DIR, datadir, run_script->get_run_path(), task.run_args);
    switch (ret) {
        case E_SUCCESS: {
            filesystem::remove(errorpath);
            // 随机数据已经准备好
            LOG_INFO << "Generated random data case [" << submit.category << "-" << submit.prob_id << "-" << submit.sub_id << "-" << number << "] in " << random_time.template duration<chrono::milliseconds>().count() << "ms";
            return true;
        } break;
        case E_RANDOM_GEN_ERROR: {
            // 随机数据生成器出错，返回 RANDOM_GEN_ERROR 并携带错误信息
            result.status = status::RANDOM_GEN_ERROR;
            result.error_log = read_file_content(datadir / "system.out", "No information", judge::MAX_IO_SIZE);
            ofstream to_be_created(errorpath);  // 标记该组随机测试数据生成失败
            return false;
        } break;
        default: {  // INTERNAL_ERROR
            // 随机数据生成器出错，返回 SYSTEM_ERROR 并携带错误信息
            result.status = status::SYSTEM_ERROR;
            result.error_log = read_file_content(datadir / "system.out", "No information", judge::MAX_IO_SIZE);
            ofstream to_be_created(errorpath);  // 标记该组随机测试数据生成失败
            return false;
        } break;
    }
    return false;
}

/**
 * @brief 执行程序评测任务
 * @param client_task 当前评测任务信息
 * @param submit 当前评测任务归属的选手提交信息
 * @param task 当前评测任务数据点的信息
 * @param execcpuset 当前评测任务能允许运行在那些 cpu 核心上
 * @param awake_callback 获取评测任务中途评测部分结果后的 callback，用于返回评测报告
 */
static judge_task_result judge_impl(const message::client_task &client_task, programming_submission &submit, judge_task &task, const string &execcpuset, function<void()> awake_callback) {
    LOG_INFO << "in the function judge_impl";  // debug
    // 获取一个类似 5-random_check 的任务名，方便查找提交文件夹
    string taskid = boost::lexical_cast<string>(client_task.id);
    string taskname = task.tag;
    boost::remove_if(taskname, boost::is_any_of("/\\"));
    boost::replace_all(taskname, " ", "_");
    boost::replace_all(taskname, ":", "_");
    if (taskname.empty())
        taskname = taskid + "-" + boost::lexical_cast<string>(boost::uuids::random_generator()());
    else
        taskname = taskid + "-" + taskname;

    filesystem::path cachedir = get_cache_dir(submit);
    filesystem::path workdir = get_work_dir(submit);          // 本提交的工作文件夹
    filesystem::path rundir = workdir / ("run-" + taskname);  // 本测试点的运行文件夹
    filesystem::create_directories(rundir);

    judge_task_result result{task.tag, client_task.id};
    result.run_dir = rundir;

    auto &exec_mgr = submit.judge_server->get_executable_manager();

    auto check_script = exec_mgr.get_check_script(task.check_script);
    check_script->fetch(execcpuset, CHROOT_DIR, exec_mgr);
    auto check_script_lock = check_script->shared_lock();

    auto run_script = exec_mgr.get_run_script(task.run_script);
    run_script->fetch(execcpuset, CHROOT_DIR, exec_mgr);
    auto run_script_lock = run_script->shared_lock();

    unique_ptr<judge::program> exec_compare_script = exec_mgr.get_compare_script(task.compare_script);
    auto &compare_script = task.compare_script.empty() && submit.compare ? submit.compare : exec_compare_script;
    compare_script->fetch(execcpuset, cachedir / "compare", CHROOT_DIR, exec_mgr);
    auto compare_script_lock = compare_script->shared_lock();

    filesystem::path datadir;

    int depends_on = task.depends_on;
    judge_task *father = nullptr;
    if (depends_on >= 0) father = &submit.judge_tasks[depends_on];

    // 获得输入输出数据，提交发生更新时，将直接清理整个文件夹内所有内容
    if (task.is_random) {
        // 生成随机测试数据
        filesystem::path random_data_dir = cachedir / "random_data";

        if (father && father->is_random) {  // 如果父测试也是随机测试，那么使用同一个测试数据组
            task.testcase_id = father->testcase_id;
            task.subcase_id = father->subcase_id;
            if (task.testcase_id < 0 || task.subcase_id < 0) LOG_FATAL << "Unknown test case";
            datadir = random_data_dir / to_string(task.testcase_id) / to_string(task.subcase_id);
        } else {
            // 创建一组随机数据
            random_data_dir /= to_string(task.testcase_id);

            scoped_file_lock lock = lock_directory(random_data_dir, false);
            // 检查已经产生了多少组随机测试数据
            int number = count_directories_in_directory(random_data_dir);
            if (number < MAX_RANDOM_DATA_NUM) {  // 如果没有达到创建上限，则生成随机测试数据
                datadir = random_data_dir / to_string(number);
                scoped_file_lock case_lock = lock_directory(datadir, false);  // 随机目录的写入必须加锁
                lock.release();

                if (!generate_random_data(datadir, cachedir, number, submit, task, result, execcpuset))
                    return result;
            } else {
                int number = random(0, MAX_RANDOM_DATA_NUM - 1);
                task.subcase_id = number;  // 标记当前测试点使用了哪个随机测试
                datadir = random_data_dir / to_string(number);
                // 加锁：可能出现某个进程正在生成随机数据的情况，此时需要等待数据生成完成。
                scoped_file_lock case_lock = lock_directory(datadir, false);
                lock.release();
                filesystem::path errorpath = datadir / ".error";  // 文件存在表示该组测试数据生成失败
                if (filesystem::exists(errorpath)) {              // 该组测试数据生成失败则重试，如果仍然失败返回
                    if (!generate_random_data(datadir, cachedir, number, submit, task, result, execcpuset))
                        return result;
                }
            }
        }
    } else {
        // 下载标准测试数据
        filesystem::path standard_data_dir = cachedir / "standard_data";

        if (father && !father->is_random && father->testcase_id >= 0) {  // 如果父测试也是标准测试，那么使用同一个测试数据组
            int number = father->testcase_id;
            if (number < 0) LOG_FATAL << "Unknown test case";
            datadir = standard_data_dir / to_string(number);
        } else if (task.testcase_id >= 0) {  // 使用对应的标准测试数据
            datadir = standard_data_dir / to_string(task.testcase_id);
            scoped_file_lock lock = lock_directory(standard_data_dir, false);
            auto &test_data = submit.test_data[task.testcase_id];
            if (!filesystem::exists(datadir)) {
                filesystem::create_directories(datadir / "input");
                filesystem::create_directories(datadir / "output");
                for (auto &asset : test_data.inputs)
                    asset->fetch(datadir / "input");
                for (auto &asset : test_data.outputs)
                    asset->fetch(datadir / "output");
            }
        } else {  // 该数据点不需要测试数据
            datadir = standard_data_dir / "-1";
            // 创建一个空的数据文件夹提供给测试点使用
            filesystem::create_directories(datadir);
            filesystem::create_directories(datadir / "input");
            filesystem::create_directories(datadir / "output");
        }
    }

    if (USE_DATA_DIR) {  // 如果要拷贝测试数据，我们随机 UUID 并创建文件夹拷贝数据
        filesystem::path newdir = DATA_DIR / taskname;
        filesystem::copy(datadir, newdir, filesystem::copy_options::recursive);
        datadir = newdir;
    }
    result.data_dir = datadir;

    defer {  // 评测结束后删除拷贝的评测数据
        if (USE_DATA_DIR) {
            filesystem::remove(datadir);
        }
    };

    process_builder pb;
    pb.directory(rundir);
    if (task.file_limit > 0) pb.environment("FILELIMIT", task.file_limit);
    if (task.memory_limit > 0) pb.environment("MEMLIMIT", task.memory_limit);
    if (task.proc_limit > 0) pb.environment("PROCLIMIT", task.proc_limit);

    if (task.actions.size() && task.action_delay > 0) {
        LOG_INFO << "task.action_delay = " << task.action_delay;  // debug
        pb.awake_period(task.action_delay, [&]() {
            result.actions.clear();
            for (auto &action : task.actions) {
                action_result res;
                res.tag = action.tag;
                res.success = action.act(submit, task, result, res.result);
                result.actions.push_back(res);
            }

            awake_callback();
        });
    }

    vector<string> basedirs;
    for (int taskid = task.file_depends_on < 0 ? task.depends_on : task.file_depends_on;
         taskid >= 0 && taskid < (int)submit.results.size();
         taskid = submit.judge_tasks[taskid].file_depends_on < 0 ? submit.judge_tasks[taskid].depends_on : submit.judge_tasks[taskid].file_depends_on) {
        // TODO: 暂时未静默跳过未完成测试的运行环境依赖
        if (submit.results[taskid].status == status::PENDING) continue;
        basedirs.push_back(submit.results[taskid].run_dir.string());
    }
    reverse(basedirs.begin(), basedirs.end());

    optional<string> walltime;
    if (execcpuset.find(",") != string::npos || execcpuset.find("-") != string::npos)
        walltime = "-w";

    LOG_INFO << "in the function judge_impl: before pb.run";  // debug

    // 调用 check script 来执行真正的评测，这里会调用 run script 运行选手程序，调用 compare script 运行比较器，并返回评测结果
    // <check-script> <datadir> <timelimit> <chrootdir> <workdir> <basedir> <run-uuid> <compile-script> <run-script> <compare-script> <source files> <assist files> <run args>
    int ret = pb.run(check_script->get_run_path() / "run",
                     "-n", execcpuset, "--",
                     walltime,
                     datadir, task.time_limit, CHROOT_DIR, workdir,
                     boost::algorithm::join(basedirs, ":"),
                     taskname,
                     get_run_path(submit.submission->get_compile_script(exec_mgr)),
                     run_script->get_run_path(),
                     compare_script->get_run_path(cachedir / "compare"),
                     boost::algorithm::join(submit.submission->source_files | boost::adaptors::transformed([](auto &a) { return a->name; }), ":"),
                     boost::algorithm::join(submit.submission->assist_files | boost::adaptors::transformed([](auto &a) { return a->name; }), ":"),
                     task.run_args);
    result.report = read_file_content(rundir / "feedback" / "report.txt", "");
    result.error_log = read_file_content(rundir / "system.out", "No detailed information", judge::MAX_IO_SIZE);
    switch (ret) {
        case E_INTERNAL_ERROR:
            result.status = status::SYSTEM_ERROR;
            break;
        case E_ACCEPTED:
            result.status = status::ACCEPTED;
            result.score = 1;
            break;
        case E_WRONG_ANSWER:
            result.status = status::WRONG_ANSWER;
            break;
        case E_PARTIAL_CORRECT:
            result.status = status::PARTIAL_CORRECT;
            {
                // 比较器会将评分（0~1 的分数）存到 score.txt 中。
                // 不会出现文件不存在的情况，否则 check script 将返回 COMPARE_ERROR
                // feedback 文件夹的内容参考 check script
                ifstream fin(rundir / "feedback" / "score.txt");
                int numerator, denominator;
                fin >> numerator >> denominator;  // 文件中第一个数字是分子，第二个数字是分母
                result.score = {numerator, denominator};
            }
            break;
        case E_PRESENTATION_ERROR:
            result.status = status::PRESENTATION_ERROR;
            break;
        case E_COMPARE_ERROR:
            result.status = status::COMPARE_ERROR;
            break;
        case E_RUNTIME_ERROR:
            result.status = status::RUNTIME_ERROR;
            break;
        case E_FLOATING_POINT:
            result.status = status::FLOATING_POINT_ERROR;
            break;
        case E_SEG_FAULT:
            result.status = status::SEGMENTATION_FAULT;
            break;
        case E_OUTPUT_LIMIT:
            result.status = status::OUTPUT_LIMIT_EXCEEDED;
            break;
        case E_TIME_LIMIT:
            result.status = status::TIME_LIMIT_EXCEEDED;
            break;
        case E_MEM_LIMIT:
            result.status = status::MEMORY_LIMIT_EXCEEDED;
            break;
        case E_RESTRICT_FUNCTION:
            result.status = status::RESTRICT_FUNCTION;
            break;
        default:
            result.status = status::SYSTEM_ERROR;
            break;
    }

    auto metadata = read_runguard_result(rundir / "program.meta");
    result.run_time = metadata.wall_time;  // TODO: 支持题目选择 cpu_time 或者 wall_time 进行时间
    result.memory_used = metadata.memory;

    result.actions.clear();
    for (auto &action : task.actions) {
        action_result res;
        res.tag = action.tag;
        res.success = action.act(submit, task, result, res.result);
        result.actions.push_back(res);
    }

    return result;
}

static void compile(judge::program &program, const filesystem::path &workdir, const string &execcpuset, const executable_manager &exec_mgr, const program_limit &limit, judge_task_result &task_result, bool executable) {
    try {
        // 将程序存放在 workdir 下，program.fetch 会自行组织 workdir 内的文件存储结构
        // 并编译程序，编译需要的运行环境就是全局的 CHROOT_DIR，这样可以获得比较完整的环境
        program.fetch(execcpuset, workdir, CHROOT_DIR, exec_mgr, limit);
        task_result.status = status::ACCEPTED;
    } catch (executable_compilation_error &ex) {
        task_result.status = status::EXECUTABLE_COMPILATION_ERROR;
        string what = ex.what();
        task_result.report = program.get_compilation_log(workdir);
        task_result.error_log = (what.empty() ? "" : what + "\n") + program.get_compilation_details(workdir);
    } catch (compilation_error &ex) {
        LOG_DEBUG << "Compilation_error.";  // debug

        task_result.status = executable ? status::EXECUTABLE_COMPILATION_ERROR : status::COMPILATION_ERROR;
        string what = ex.what();
        task_result.report = program.get_compilation_log(workdir);
        task_result.error_log = (what.empty() ? "" : what + "\n") + program.get_compilation_details(workdir);
    } catch (exception &ex) {
        task_result.status = status::SYSTEM_ERROR;
        task_result.error_log = ex.what();
    }
}

/**
 * @brief 执行程序编译任务
 * @param client_task 当前评测任务信息
 * @param submit 当前评测任务归属的选手提交信息
 * @param task 当前评测任务的编译信息
 * @param execcpuset 当前评测任务能允许运行在那些 cpu 核心上
 * @param result_queue 评测结果队列
 */
static judge_task_result compile(const message::client_task &client_task, programming_submission &submit, judge_task &task, const string &execcpuset) {
    filesystem::path cachedir = get_cache_dir(submit);

    LOG_DEBUG << "cachedir = " << cachedir;  //debug

    auto &exec_mgr = submit.judge_server->get_executable_manager();

    judge_task_result result{task.tag, client_task.id};

    // 编译选手程序，submit.submission 都为非空，否则在 server.cpp 中的 fetch_submission 会阻止该提交的评测
    if (submit.submission) {
        LOG_DEBUG << "Compile submission";

        filesystem::path workdir = get_work_dir(submit);
        result.run_dir = workdir / "compile";
        compile(*submit.submission, workdir, execcpuset, exec_mgr, task, result, false);

        auto metadata = read_runguard_result(result.run_dir / "compile.meta");
        result.run_time = metadata.wall_time;
        result.memory_used = metadata.memory;
        if (result.status != status::ACCEPTED) return result;
    }

    // 编译随机数据生成器
    if (submit.random) {
        LOG_DEBUG << "Compile random";

        filesystem::path randomdir = cachedir / "random";
        compile(*submit.random, randomdir, execcpuset, exec_mgr, task, result, true);
        if (result.status == status::COMPILATION_ERROR)
            result.status = status::EXECUTABLE_COMPILATION_ERROR;
        if (result.status != status::ACCEPTED) return result;
    }

    // 编译标准程序
    if (submit.standard) {
        LOG_DEBUG << "Compile standard";

        filesystem::path standarddir = cachedir / "standard";
        compile(*submit.standard, standarddir, execcpuset, exec_mgr, task, result, true);
        if (result.status == status::COMPILATION_ERROR)
            result.status = status::EXECUTABLE_COMPILATION_ERROR;
        if (result.status != status::ACCEPTED) return result;
    }

    // 编译比较器
    if (submit.compare) {
        LOG_DEBUG << "Compile compare";

        filesystem::path comparedir = cachedir / "compare";
        compile(*submit.compare, comparedir, execcpuset, exec_mgr, task, result, true);
        if (result.status == status::COMPILATION_ERROR)
            result.status = status::EXECUTABLE_COMPILATION_ERROR;
        if (result.status != status::ACCEPTED) return result;
    }
    return result;
}

string programming_judger::type() const {
    return "programming";
}

/**
 * @brief 确认题目的缓存文件夹是最新的，如果不是，阻塞到旧题目评测完成之后再分发评测。
 * 因为 fetch_submission 在评测队列空缺时被调用，此时该 worker 已经没有可以评测的
 * 评测任务才会尝试拉取提交，因此该 worker 阻塞不会影响已经在评测的提交的评测状态。
 * 也就是说阻塞是安全的。
 * @param submit 要检查对应题目是否是最新的提交
 */
static void verify_timeliness(programming_submission &submit) {
    filesystem::path cachedir = get_cache_dir(submit);
    filesystem::create_directories(cachedir);

    filesystem::path time_file = cachedir / ".time";
    if (!filesystem::exists(time_file)) {
        ofstream fout(time_file);
    }

    time_t modified_time = judge::last_write_time(time_file);
    if (submit.updated_at > modified_time) {
        scoped_file_lock lock = lock_directory(cachedir, false);

        // 再次检查文件夹更新时间，因为如果有两个提交同时竞争该文件夹锁，某个提交获得了
        // 锁并完成文件夹清理后，另一个提交就会拿到文件夹锁，此时不再需要清理文件夹
        modified_time = judge::last_write_time(time_file);
        if (submit.updated_at > modified_time) {
            LOG_INFO << "Clean outdated cache directory " << cachedir;
            // 清理文件夹，因为删除文件夹意味着锁必须被释放（锁文件也会被清除）。
            // 如果有两个提交一起竞争该文件夹，会导致删除该文件夹两次，可能导致已经下载的部分文件丢失。
            clean_locked_directory(cachedir);
            ofstream fout(time_file);
        }
    }

    submit.problem_lock = lock_directory(cachedir, true);
    judge::last_write_time(time_file, submit.updated_at);
}

bool programming_judger::verify(submission &submit) const {
    auto sub = dynamic_cast<programming_submission *>(&submit);
    if (!sub) {
        LOG_DEBUG << "dynamic_cast failed";
        return false;
    }

    // 检查 judge_server 获取的 sub 是否包含编译任务，且确保至多一个编译任务
    bool has_random_case = false;
    for (size_t i = 0; i < sub->judge_tasks.size(); ++i) {
        auto &judge_task = sub->judge_tasks[i];

        if (judge_task.depends_on >= (int)i) {  // 如果每个任务都只依赖前面的任务，那么这个图将是森林，确保不会出现环
            LOG_WARN << "Submission from [" << sub->category << "-" << sub->prob_id << "-" << sub->sub_id << "] may contains circular dependency.";
            return false;
        }

        // 对于 C++ 内存检查，由于需要单独编译一个带 AddressSanitizer 的二进制
        // 因此允许这个编译任务有依赖，并且允许提交中有多个编译任务

        if (judge_task.is_random) {
            has_random_case = true;
        }
    }

    if (!sub->submission) {
        LOG_DEBUG << "missing submission";
        return false;
    }

    if (has_random_case && (!sub->standard || !sub->random)) {
        // 随机测试要求提供标准程序和随机数据生成器
        LOG_DEBUG << "has random case but missing standard or random";
        return false;
    }

    // 检查是否存在可以直接评测的测试点，如果不存在则直接返回
    bool sent_testcase = false;
    for (size_t i = 0; i < sub->judge_tasks.size(); ++i) {
        if (sub->judge_tasks[i].depends_on < 0) {
            sent_testcase = true;
            break;
        }
    }

    if (!sent_testcase) {
        // 如果不存在评测任务，直接返回
        LOG_WARN << "Submission from [" << sub->category << "-" << sub->prob_id << "-" << sub->sub_id << "] does not have entry test task.";
        return false;
    }

    return true;
}

bool programming_judger::distribute(concurrent_queue<message::client_task> &task_queue, submission &submit) const {
    LOG_DEBUG << "Programming judger start to distribute.";

    auto &sub = dynamic_cast<programming_submission &>(submit);

    std::stringstream info;
    for (auto &judge_task : sub.judge_tasks) {
        info << "judge task: tag = " << judge_task.tag << ", check_script = " << judge_task.check_script << ", run_script = " << judge_task.run_script << ", compare_script = " << judge_task.compare_script << ", is_random = " << judge_task.is_random << ", testcase_id = " << judge_task.testcase_id << ", subcase_id = " << judge_task.subcase_id << ", depends_on = " << judge_task.depends_on << std::endl;
    }
    LOG_DEBUG << "Submission's judge tasks: " << std::endl
              << info.str();

    filesystem::path workdir = get_work_dir(sub);
    sub.submission_lock = lock_directory(workdir, false);
    clean_locked_directory(workdir);

    verify_timeliness(sub);

    // 初始化当前提交的所有评测任务状态为 PENDING
    sub.results.resize(sub.judge_tasks.size());
    for (size_t i = 0; i < sub.results.size(); ++i) {
        sub.results[i].status = status::PENDING;
        sub.results[i].id = i;
    }

    // 寻找没有依赖的评测点，并发送评测消息
    for (size_t i = 0; i < sub.judge_tasks.size(); ++i) {
        if (sub.judge_tasks[i].depends_on < 0) {  // 不依赖任何任务的任务可以直接开始评测
            sub.results[i].status = status::RUNNING;
            judge::message::client_task client_task = {
                .submit = &submit,
                .id = i,
                .name = sub.judge_tasks[i].tag,
                .cores = sub.judge_tasks[i].cores,
                .expect_runtime = sub.judge_tasks[i].time_limit * 5};
            task_queue.push(client_task);
        }
    }
    return true;
}

static void call_monitor(function<void(monitor &)> callback) {
    try {
        for (auto &monitor : monitors) callback(*monitor);
    } catch (std::exception &ex) {
        LOG_ERROR << " Crash happened when reporting monitoring information, " << ex.what();
    }
}

static void summarize(programming_submission &submit) {
    LOG_INFO << "Submission finished in " << submit.judge_time.template duration<chrono::milliseconds>().count() << "ms";
    call_monitor([&](monitor &m) { m.get_judge_time(submit); });

    submit.judge_server->summarize(submit);

    filesystem::path workdir = get_work_dir(submit);
    try {
        bool removedir = true;
        for (auto &result : submit.results) {
            if (result.status == status::SYSTEM_ERROR) {
                removedir = false;
                break;
            }
        }
        if (!getenv("RESERVE_SUBMISSION") && removedir) filesystem::remove_all(workdir);
    } catch (exception &e) {
        LOG_ERROR << "Unable to delete directory " << workdir << ":" << e.what();
    }
}

/**
 * @brief 完成评测结果的统计，如果统计的是编译任务，则会分发具体的评测任务
 * 在评测完成后，通过调用 process 函数来完成数据点的统计，如果发现评测完了一个提交，则立刻返回。
 * 因此大部分情况下评测队列不会过长：只会拉取适量的评测，确保评测队列不会过长。
 * 
 * @param result 评测结果
 */
template <typename DurationT>
void process(const programming_judger &judger, concurrent_queue<message::client_task> &testcase_queue, programming_submission &submit, const judge_task_result &result, DurationT dur, bool is_summarize = true) {
    // 记录测试信息
    submit.results[result.id] = result;

    LOG_DEBUG << "Process: result.error_log = " << result.error_log;

    LOG_INFO << "Testcase finished in " << chrono::duration_cast<chrono::milliseconds>(dur).count() << "ms"
             << ", status: " << get_display_message(result.status) << ", runtime/s: " << result.run_time
             << ", memory/KB: " << result.memory_used / 1024 << ", run_dir: " << result.run_dir
             << ", data_dir: " << result.data_dir;
    if (result.status == status::SYSTEM_ERROR)
        LOG_ERROR << "Testcase error: " << result.error_log;

    for (size_t i = 0; i < submit.judge_tasks.size(); ++i) {
        judge_task &kase = submit.judge_tasks[i];
        // 寻找依赖当前评测任务的评测任务
        if (kase.depends_on == (int)result.id) {
            bool satisfied;
            switch (kase.depends_cond) {
                case judge_task::dependency_condition::ACCEPTED:
                    satisfied = result.status == status::ACCEPTED;
                    break;
                case judge_task::dependency_condition::PARTIAL_CORRECT:
                    satisfied = result.status == status::PARTIAL_CORRECT ||
                                result.status == status::ACCEPTED;
                    break;
                case judge_task::dependency_condition::NON_TIME_LIMIT:
                    satisfied = result.status != status::SYSTEM_ERROR &&
                                result.status != status::COMPARE_ERROR &&
                                result.status != status::COMPILATION_ERROR &&
                                result.status != status::DEPENDENCY_NOT_SATISFIED &&
                                result.status != status::TIME_LIMIT_EXCEEDED &&
                                result.status != status::EXECUTABLE_COMPILATION_ERROR &&
                                result.status != status::OUT_OF_CONTEST_TIME &&
                                result.status != status::RANDOM_GEN_ERROR;
                    break;
            }

            if (satisfied) {
                // 评测任务 i 的依赖关系满足予以评测
                submit.results[i].status = status::RUNNING;
                judge::message::client_task client_task = {
                    .submit = &submit,
                    .id = i,
                    .name = kase.tag,
                    .cores = kase.cores,
                    .expect_runtime = submit.judge_tasks[i].time_limit * 5};
                testcase_queue.push(client_task);
            } else {
                // 评测任务 i 的依赖关系不满足，由于依赖关系是树，因此将子树全部设置为 DEPENDENCY_NOT_SATISFIED
                judge_task_result next_result;
                next_result.status = status::DEPENDENCY_NOT_SATISFIED;
                next_result.tag = kase.tag;
                next_result.id = i;
                next_result.score = 0;
                next_result.run_time = 0;
                next_result.memory_used = 0;
                next_result.error_log = "";
                next_result.report = "";
                next_result.actions.clear();
                process(judger, testcase_queue, submit, next_result, DurationT(), false);
            }
        }
    }

    ++submit.finished;

    LOG_INFO << "in function process: submit.finished = " << submit.finished << " submit.judge_tasks.size() = " << submit.judge_tasks.size();  // debug

    if (!is_summarize) return;
    if (submit.finished == submit.judge_tasks.size()) {
        // 如果当前提交的所有测试点都完成测试，则返回评测结果
        summarize(submit);
        judger.fire_judge_finished(submit);
    } else if (submit.finished > submit.judge_tasks.size()) {
        LOG_ERROR << "Test case exceeded";
    } else {
        // 未完成评测，检查是否存在正在评测的评测任务
        bool has_running = false;
        for (size_t i = 0; i < submit.judge_tasks.size(); ++i) {
            if (submit.results[i].status == status::RUNNING) {
                has_running = true;
                break;
            }
        }

        // 如果当前提交不存在正在评测的任务，则说明有 bug
        if (!has_running) {
            LOG_FATAL << "No succ judge task";
        }

        // 发送中途的评测报告，不做 ACK
        submit.judge_server->summarize(submit, false);
    }
}

void programming_judger::judge(const message::client_task &client_task, concurrent_queue<message::client_task> &task_queue, const string &execcpuset) const {
    auto submit = dynamic_cast<programming_submission *>(client_task.submit);
    judge_task &task = submit->judge_tasks[client_task.id];
    judge_task_result result;

    auto begin = chrono::system_clock::now();

    LOG_DEBUG << "Judge: task.check_script = " << task.check_script;

    try {
        if (task.check_script == "compile")
            result = compile(client_task, *submit, task, execcpuset);
        else
            result = judge_impl(client_task, *submit, task, execcpuset, [&]() {
                auto end = chrono::system_clock::now();

                scoped_lock guard(submit->mut);
                process(*this, task_queue, *submit, result, end - begin);
            });
    } catch (exception &ex) {
        result = {task.tag, client_task.id};
        result.status = status::SYSTEM_ERROR;
        result.error_log = ex.what();
    }

    auto end = chrono::system_clock::now();

    scoped_lock guard(submit->mut);
    process(*this, task_queue, *submit, result, end - begin);
}

bool action::act(submission &, judge_task &, judge_task_result &task_result, string &) const {
    switch (cond) {
        case condition::ACCEPTED:
            return task_result.status == status::ACCEPTED;
        case condition::NON_ACCEPTED:
            return task_result.status != status::ACCEPTED;
        case condition::PARTIAL_CORRECT:
            return task_result.status == status::ACCEPTED || task_result.status == status::PARTIAL_CORRECT;
        case condition::NON_PARTIAL_CORRECT:
            return task_result.status != status::ACCEPTED && task_result.status != status::PARTIAL_CORRECT;
        default:
            return true;
    }
}

bool read_action::act(submission &submit, judge_task &task, judge_task_result &task_result, string &result) const {
    if (!action::act(submit, task, task_result, result)) {
        return false;
    }
    string path = this->path;
    boost::replace_all(path, string("$DATADIR"), task_result.data_dir.string());
    boost::replace_all(path, string("$RUNDIR"), (task_result.run_dir / "run").string());
    filesystem::path p = filesystem::absolute(filesystem::path(path));
    if (!boost::starts_with(p.string(), task_result.data_dir.string()) &&
        !boost::starts_with(p.string(), (task_result.run_dir / "run").string()))
        return false;
    if (!filesystem::exists(p))
        return false;

    if (action == "text") {
        result = read_file_content(path, file_limit);
        return true;
    } else if (action == "upload") {
        // 文件上传类型的读取操作结果设定为空
        // TODO: 是否要截获这里的 network_error?
        net::upload_file(url, p);
        return true;
    } else if (action == "both") {
        uintmax_t file_size = filesystem::file_size(path);
        if (file_size <= static_cast<uintmax_t>(file_limit)) {
            result = read_file_content(path, file_limit);
        } else {
            net::upload_file(url, p);
        }
        return true;
    } else {
        return false;
    }
}

}  // namespace judge
