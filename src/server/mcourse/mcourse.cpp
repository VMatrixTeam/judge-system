#include "server/mcourse/mcourse.hpp"

#include <stdio.h>

#include <boost/algorithm/string.hpp>
#include <boost/assign.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>

#include "common/io_utils.hpp"
#include "common/json_utils.hpp"
#include "common/messages.hpp"
#include "common/net_utils.hpp"
#include "common/stl_utils.hpp"
#include "common/utils.hpp"
#include "config.hpp"
#include "judge/choice.hpp"
#include "judge/program_output.hpp"
#include "judge/programming.hpp"
#include "logging.hpp"
#include "server/mcourse/feedback.hpp"

namespace judge::server::mcourse {
using namespace std;
using namespace nlohmann;
using namespace ormpp;

const string COMPILE_CHECK_TYPE = "CompileCheck";
const string MEMORY_CHECK_TYPE = "MemoryCheck";
const string RANDOM_CHECK_TYPE = "RandomCheck";
const string STANDARD_CHECK_TYPE = "StandardCheck";
const string STATIC_CHECK_TYPE = "StaticCheck";
const string GTEST_CHECK_TYPE = "GTestCheck";

// clang-format off
const unordered_map<status, const char *> status_string = boost::assign::map_list_of
    (status::PENDING, "")
    (status::RUNNING, "")
    (status::ACCEPTED, "OK")
    (status::COMPILATION_ERROR, "CE")
    (status::EXECUTABLE_COMPILATION_ERROR, "CE")
    (status::WRONG_ANSWER, "WA")
    (status::RUNTIME_ERROR, "RE")
    (status::TIME_LIMIT_EXCEEDED, "TL")
    (status::MEMORY_LIMIT_EXCEEDED, "ML")
    (status::OUTPUT_LIMIT_EXCEEDED, "OL")
    (status::PRESENTATION_ERROR, "PE")
    (status::RESTRICT_FUNCTION, "RF")
    (status::OUT_OF_CONTEST_TIME, "") // Too Late
    (status::COMPILING, "")
    (status::SEGMENTATION_FAULT, "SF")
    (status::FLOATING_POINT_ERROR, "FPE")
    (status::RANDOM_GEN_ERROR, "RG")
    (status::COMPARE_ERROR, "CP")
    (status::SYSTEM_ERROR, "IE");
// clang-format on

configuration::configuration()
    : exec_mgr(CACHE_DIR, EXEC_DIR) {}

static void connect_database(dbng<mysql> &db, const database &dbcfg) {
    db.connect(dbcfg.host.c_str(), dbcfg.username.c_str(), dbcfg.password.c_str(), dbcfg.database.c_str());
}

void configuration::init(const filesystem::path &config_path) {
    if (!filesystem::exists(config_path))
        throw runtime_error("Unable to find configuration file");
    ifstream fin(config_path);
    json config;
    fin >> config;

    config.at("matrix_db").get_to(matrix_dbcfg);
    matrix_db.connect(
        matrix_dbcfg.host.c_str(),
        matrix_dbcfg.username.c_str(),
        matrix_dbcfg.password.c_str(),
        matrix_dbcfg.database.c_str(),
        matrix_dbcfg.port);

    config.at("submission_queue").get_to(sub_queue);
    config.at("systemConfig").get_to(system);
    assign_optional(config, m_category, "category");

    assign_optional(config, git_service, "git");

    sub_fetcher = make_unique<rabbitmq_channel>(sub_queue);
}

string configuration::category() const {
    return m_category;
}

const executable_manager &configuration::get_executable_manager() const {
    return exec_mgr;
}

static function<asset_uptr(const string &)> moj_url_to_remote_file(configuration &config, const string &subpath) {
    return [&](const string &name) {
        return make_unique<remote_asset>(name, config.system.file_api + "/" + subpath + "/" + name);
    };
}

/**
 * @brief 导入 Matrix 课程系统题目的 config json
 * @code{json}
 * {
 *   "limits": {
 *     "time": 1000,
 *     "memory": 32
 *   },
 *   "random": {
 *     "run_times": 20,
 *     "compile_command": "g++ SOURCE -w -std=c++14 -o random"
 *   },
 *   "grading": {
 *     "google tests": 0,
 *     "memory check": 0,
 *     "random tests": 50,
 *     "static check": 0,
 *     "compile check": 10,
 *     "standard tests": 40,
 *     "google tests detail": {}
 *   },
 *   "language": [
 *     "c++"
 *   ],
 *   "standard": {
 *     "support": [],
 *     "random_source": [
 *       "random.c"
 *     ],
 *     "hidden_support": [],
 *     "standard_input": [
 *       "standard_input0",
 *       "standard_input1",
 *       "standard_input2",
 *       "standard_input3",
 *       "standard_input4",
 *       "standard_input5",
 *       "standard_input6",
 *       "standard_input7",
 *       "standard_input8",
 *       "standard_input9"
 *     ],
 *     "standard_output": [
 *       "standard_output0",
 *       "standard_output1",
 *       "standard_output2",
 *       "standard_output3",
 *       "standard_output4",
 *       "standard_output5",
 *       "standard_output6",
 *       "standard_output7",
 *       "standard_output8",
 *       "standard_output9"
 *     ]
 *   },
 *   "compilers": {
 *     "c": {
 *       "command": "gcc CODE_FILES -g -w -lm -o OUTPUT_PROGRAM",
 *       "version": "default"
 *     },
 *     "c++": {
 *       "command": "g++ CODE_FILES -g -w -lm -std=c++14 -o OUTPUT_PROGRAM",
 *       "version": "default"
 *     },
 *     "clang": {
 *       "command": "clang CODE_FILES -g -w -lm -o OUTPUT_PROGRAM",
 *       "version": "default"
 *     },
 *     "clang++": {
 *       "command": "clang++ CODE_FILES -g -w -lm -std=c++11 -o OUTPUT_PROGRAM",
 *       "version": "default"
 *     }
 *   },
 *   "exec_flag": "--gtest_output=xml:/tmp/Result.xml",
 *   "submission": [
 *     "source.cpp"
 *   ],
 *   "entry_point": "standard_main.exe",
 *   "output_program": "main.exe",
 *   "standard_score": 100,
 *   "google tests info": {},
 *   "standard_language": "c++"
 * }
 * @endcode
 * 
 * detail:
 * {"language": "c++"} 或者是 {"is_standard": 1} 或者是 {"git":"http://git_repository/prob/submission.git"}
 */
void from_json_programming(const json &config, const json &detail, judge_request::submission_type sub_type, configuration &server, programming_submission &submit) {
    unique_ptr<source_code> submission = make_unique<source_code>();
    unique_ptr<source_code> standard = make_unique<source_code>();
    unique_ptr<git_repository> git = make_unique<git_repository>();

    // compiler 项因为我们忽略课程系统的编译参数，因此也忽略掉
    // standard_score 看起来像满分，但我们可以根据 grading 把满分算出来，所以不知道这个有啥用
    // FIXME: exec_flag, entry_point, output_program, standard_score 项暂时忽略掉

    // 对于老师提交，我们通过将 submission 和 standard 设置完全一样来进行测试
    // 这样标准测试可以正常测试数据，随机测试也可以因为 RANDOM_GEN_ERROR 返回错误

    bool is_git = detail.count("git_path") && detail.count("git_protocol") && sub_type == judge_request::submission_type::student;

    standard->language = get_value<string>(config, "standard_language");
    if (standard->language == "c++")
        standard->language = "cpp";

    // 对于学生提交，我们从 detail 中获得当前学生提交的编程语言
    if (sub_type == judge_request::submission_type::student) {
        if (is_git) {
            string protocol = get_value<string>(detail, "git_protocol");
            string path = get_value<string>(detail, "git_path");
            git->username = server.git_service.username;
            git->password = server.git_service.password;
            git->url = fmt::format("{}://{}", protocol, path);
            assign_optional(detail, git->commit, "git_commit");
        } else if (detail.count("language")) {
            submission->language = get_value<string>(detail, "language");
        } else {
            auto language = get_value<vector<string>>(config, "language");
            if (language.empty()) throw invalid_argument("submission.config.language is empty");
            submission->language = language[0];
        }

        if (submission->language == "c++")
            submission->language = "cpp";
    } else {
        submission->language = standard->language;
    }

    if (exists(config, "entry_point")) {
        string entry_point = get_value<string>(config, "entry_point");
        submission->entry_point = standard->entry_point = entry_point;
    }

    auto &standard_json = config.at("standard");

    if (standard_json.count("support")) {
        auto support = standard_json.at("support").get<vector<string>>();
        // 依赖文件的下载地址：FILE_API/problem/<prob_id>/support/<filename>
        append(submission->source_files, support, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
        append(standard->source_files, support, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
        append(git->overrides, support, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
        append(git->source_files, support, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
    }

    if (standard_json.count("hidden_support")) {
        auto hidden_support = standard_json.at("hidden_support").get<vector<string>>();
        // 依赖文件的下载地址：FILE_API/problem/<prob_id>/support/<filename>
        append(submission->source_files, hidden_support, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
        append(standard->source_files, hidden_support, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
        append(git->overrides, hidden_support, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
        append(git->source_files, hidden_support, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
    }

    {  // submission
        auto src_url = config.at("submission").get<vector<string>>();
        // 选手提交的下载地址：FILE_API/submission/<sub_id>/<filename>
        if (sub_type == judge_request::submission_type::student) {
            append(submission->source_files, src_url, moj_url_to_remote_file(server, fmt::format("submission/{}", submit.sub_id)));
            append(git->source_files, src_url, moj_url_to_remote_file(server, fmt::format("submission/{}", submit.sub_id)));
        } else {
            append(submission->source_files, src_url, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
            append(git->source_files, src_url, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
        }
        // 标准程序的下载地址：FILE_API/problem/<prob_id>/support/<filename>
        append(standard->source_files, src_url, moj_url_to_remote_file(server, fmt::format("problem/{}/support", submit.prob_id)));
    }

    if (is_git)
        submit.submission = move(git);
    else
        submit.submission = move(submission);
    submit.standard = move(standard);

    if (standard_json.count("standard_input") && standard_json.count("standard_output")) {
        auto input_url = standard_json.at("standard_input").get<vector<string>>();
        auto output_url = standard_json.at("standard_output").get<vector<string>>();
        for (size_t i = 0; i < input_url.size() && i < output_url.size(); ++i) {
            test_case_data datacase;
            // 标准输入的的下载地址：FILE_API/problem/<prob_id>/standard_input/<filename>
            datacase.inputs.push_back(moj_url_to_remote_file(server, fmt::format("problem/{}/standard_input", submit.prob_id))(input_url[i]));
            datacase.inputs[0]->name = "testdata.in";  // 我们要求输入数据文件名必须为 testdata.in
            // 标准输出的的下载地址：FILE_API/problem/<prob_id>/standard_output/<filename>
            datacase.outputs.push_back(moj_url_to_remote_file(server, fmt::format("problem/{}/standard_output", submit.prob_id))(output_url[i]));
            datacase.outputs[0]->name = "testdata.out";  // 我们要求输出数据文件名必须为 testdata.out
            submit.test_data.push_back(move(datacase));
        }
    }

    int memory_limit = get_value<int>(config, "limits", "memory") << 10;
    double time_limit = get_value<int>(config, "limits", "time") / 1000;
    int proc_limit = -1;
    int file_limit = 1 << 18;  // 256M

    int random_test_times = 0;

    const json &grading = config.at("grading");
    if (config.count("random") && get_value_def(grading, 0, "random tests") > 0) {  // 随机数据生成器
        auto random_ptr = make_unique<source_code>();

        auto &random_json = config.at("random");
        random_test_times = get_value<int>(random_json, "run_times");

        string compile_command;
        if (exists(random_json, "complie_command"))
            compile_command = random_json.at("complie_command").get<string>();
        else if (exists(random_json, "compile_command"))
            compile_command = random_json.at("compile_command").get<string>();
        else
            compile_command = "cpp";

        if (boost::algorithm::starts_with(compile_command, "gcc") ||
            boost::algorithm::starts_with(compile_command, "g++") ||
            boost::algorithm::starts_with(compile_command, "clang"))
            random_ptr->language = "cpp";
        else
            random_ptr->language = compile_command;

        if (exists(random_json, "entry_point"))
            random_ptr->entry_point = get_value<string>(random_json, "entry_point");

        if (standard_json.count("random_source")) {
            auto src_url = standard_json.at("random_source").get<vector<string>>();
            // 随机数据生成器的下载地址：FILE_API/problem/<prob_id>/random_source/<filename>
            append(random_ptr->source_files, src_url, moj_url_to_remote_file(server, fmt::format("problem/{}/random_source", submit.prob_id)));
            submit.random = move(random_ptr);
        }
    }

    vector<int> standard_checks;  // 标准测试的评测点编号
    vector<int> random_checks;    // 随机测试的评测点编号

    {  // 编译测试
        judge_task testcase;
        testcase.score = get_value_def(grading, 0, "compile check");
        testcase.depends_on = -1;
        testcase.depends_cond = judge_task::dependency_condition::ACCEPTED;
        testcase.tag = COMPILE_CHECK_TYPE;
        testcase.check_script = "compile";
        testcase.is_random = false;
        testcase.time_limit = server.system.time_limit.compile_time_limit;
        testcase.memory_limit = judge::SCRIPT_MEM_LIMIT;
        testcase.file_limit = judge::SCRIPT_FILE_LIMIT;
        testcase.proc_limit = proc_limit;
        testcase.testcase_id = -1;
        submit.judge_tasks.push_back(testcase);
    }

    int random_full_grade = get_value_def(grading, 0, "random tests");
    if (random_full_grade > 0 && random_test_times > 0) {  // 随机测试
        judge_task testcase;
        testcase.score = random_full_grade;
        testcase.score /= random_test_times;
        testcase.depends_on = 0;  // 依赖编译任务
        testcase.depends_cond = judge_task::dependency_condition::ACCEPTED;
        testcase.tag = RANDOM_CHECK_TYPE;
        testcase.check_script = "standard";
        testcase.run_script = "standard";
        testcase.compare_script = "diff-ign-trailing";
        testcase.is_random = true;
        testcase.time_limit = time_limit;
        testcase.memory_limit = memory_limit;
        testcase.file_limit = file_limit;
        testcase.proc_limit = proc_limit;

        for (int i = 0; i < random_test_times; ++i) {
            testcase.testcase_id = i;
            // 将当前的随机测试点编号记录下来，给内存测试依赖
            random_checks.push_back(submit.judge_tasks.size());
            submit.judge_tasks.push_back(testcase);
        }
    }

    int standard_full_grade = get_value_def(grading, 0, "standard tests");
    if (!submit.test_data.empty() && standard_full_grade > 0) {  // 标准测试
        judge_task testcase;
        testcase.score = standard_full_grade;
        testcase.score /= submit.test_data.size();
        testcase.depends_on = 0;  // 依赖编译任务
        testcase.depends_cond = judge_task::dependency_condition::ACCEPTED;
        testcase.tag = STANDARD_CHECK_TYPE;
        testcase.check_script = "standard";
        testcase.run_script = "standard";
        testcase.compare_script = "diff-ign-trailing";
        testcase.is_random = false;
        testcase.time_limit = time_limit;
        testcase.memory_limit = memory_limit;
        testcase.file_limit = file_limit;
        testcase.proc_limit = proc_limit;

        for (size_t i = 0; i < submit.test_data.size(); ++i) {
            testcase.testcase_id = i;
            // 将当前的标准测试点编号记录下来，给内存测试依赖
            standard_checks.push_back(submit.judge_tasks.size());
            submit.judge_tasks.push_back(testcase);
        }
    }

    int static_full_grade = get_value_def(grading, 0, "static check");
    if (static_full_grade > 0) {  // 静态测试
        judge_task testcase;
        testcase.score = static_full_grade;
        testcase.depends_on = 0;  // 程序通过编译后才有静态测试的必要
        testcase.depends_cond = judge_task::dependency_condition::ACCEPTED;
        testcase.tag = STATIC_CHECK_TYPE;
        testcase.check_script = "static";
        testcase.run_script = "standard";
        testcase.compare_script = "diff-ign-trailing";
        testcase.is_random = false;
        testcase.time_limit = server.system.time_limit.oclint;
        testcase.memory_limit = judge::SCRIPT_MEM_LIMIT;
        testcase.file_limit = judge::SCRIPT_FILE_LIMIT;
        testcase.proc_limit = proc_limit;
        testcase.testcase_id = -1;
        submit.judge_tasks.push_back(testcase);
    }

    int gtest_full_grade = get_value_def(grading, 0, "google tests");
    if (gtest_full_grade > 0) {  // GTest 测试
        judge_task testcase;
        testcase.score = gtest_full_grade;
        testcase.depends_on = 0;  // 依赖编译任务
        testcase.depends_cond = judge_task::dependency_condition::ACCEPTED;
        testcase.tag = GTEST_CHECK_TYPE;
        testcase.check_script = "standard";
        testcase.run_script = "gtest";
        testcase.compare_script = "gtest";
        testcase.is_random = false;
        testcase.time_limit = time_limit;
        testcase.memory_limit = memory_limit;
        testcase.file_limit = file_limit;
        testcase.proc_limit = proc_limit;
        testcase.testcase_id = -1;
        if (submit.submission) {
            submit.submission->compile_command.push_back("-lgtest_main");
            submit.submission->compile_command.push_back("-lgtest");
        }
        submit.judge_tasks.push_back(testcase);
    }

    int memory_full_grade = get_value_def(grading, 0, "memory check");
    if (memory_full_grade > 0) {  // 内存测试需要依赖标准测试或随机测试以便可以在标准或随机测试没有 AC 的情况下终止内存测试以加速评测速度
        judge_task testcase;
        testcase.score = memory_full_grade;
        testcase.tag = MEMORY_CHECK_TYPE;
        testcase.check_script = "standard";
        testcase.run_script = "valgrind";
        testcase.compare_script = "valgrind";
        if (submit.submission)
            submit.submission->compile_command.push_back("-g");
        testcase.is_random = submit.judge_tasks.empty();
        testcase.time_limit = server.system.time_limit.valgrind;
        testcase.memory_limit = memory_limit + (256 << 10);  // 原内存限制 + 256M
        testcase.file_limit = judge::SCRIPT_FILE_LIMIT;
        testcase.proc_limit = proc_limit;
        testcase.file_depends_on = 0;  // 内存测试仅需要编译测试的运行环境

        if (testcase.is_random) {
            if (!random_checks.empty()) {  // 如果存在随机测试，则依赖随机测试点的数据
                testcase.score /= random_checks.size();
                for (int &i : random_checks) {
                    testcase.testcase_id = submit.judge_tasks[i].testcase_id;
                    testcase.depends_on = i;
                    testcase.depends_cond = judge_task::dependency_condition::ACCEPTED;
                    submit.judge_tasks.push_back(testcase);
                }
            } else if (submit.random) {  // 否则只能生成 10 组随机测试数据
                testcase.score /= 10;
                for (size_t i = 0; i < 10; ++i) {
                    testcase.testcase_id = i;
                    testcase.depends_on = 0;  // 依赖编译任务
                    testcase.depends_cond = judge_task::dependency_condition::ACCEPTED;
                    submit.judge_tasks.push_back(testcase);
                }
            }
        } else if (!standard_checks.empty()) {
            testcase.score /= standard_checks.size();
            for (int &i : standard_checks) {
                testcase.testcase_id = submit.judge_tasks[i].testcase_id;
                testcase.depends_on = i;
                testcase.depends_cond = judge_task::dependency_condition::ACCEPTED;
                submit.judge_tasks.push_back(testcase);
            }
        }
    }

    submit.type = "programming";
}

void from_json_choice(const json &config, const json &detail, judge_request::submission_type, configuration &, choice_submission &submit) {
    auto &standard = config.at("questions");
    auto &answer = detail.at("answers");

    int id = 0;
    for (json::const_iterator qit = standard.begin(); qit != standard.end(); ++qit, ++id) {
        choice_question q;

        const json &qobj = *qit;

        if (qobj.at("id").get<int>() != id) throw invalid_argument("question id is not continuous");
        qobj.at("grading").at("max_grade").get_to(q.full_grade);
        if (qobj.at("grading").count("half_grade"))
            qobj.at("grading").at("half_grade").get_to(q.half_grade);
        qobj.at("choice_type").get_to(q.type);
        qobj.at("standard_answer").get_to(q.standard_answer);
        submit.questions.push_back(q);
    }

    for (json::const_iterator ait = answer.begin(); ait != answer.end(); ++ait) {
        const json &aobj = *ait;
        int qid = aobj.at("question_id").get<int>();
        if (qid < 0 || qid >= (int)submit.questions.size()) throw invalid_argument("question_id is out of range");
        choice_question &q = submit.questions[qid];
        aobj.at("choice_id").get_to(q.student_answer);
    }

    submit.type = "choice";
}

void from_json_program_output(const json &config, const json &detail, judge_request::submission_type, configuration &, program_output_submission &submit) {
    vector<string> standard_answers, student_answers;
    vector<int> scores;
    config.at("standard_answers").get_to(standard_answers);
    config.at("answer_scores").get_to(scores);
    detail.at("student_answers").get_to(student_answers);

    for (size_t i = 0; i < standard_answers.size() && i < scores.size() && i < student_answers.size(); ++i) {
        program_output_question q;
        q.full_grade = scores[i];
        q.standard_answer = standard_answers[i];
        q.student_answer = student_answers[i];
        submit.questions.push_back(q);
    }

    submit.type = "output";
}

static void post_report_to_fs(const configuration &server, const string &report, const string &sub_id) {
    // url：{file_api}/submission/{sub_id}/__matrix/report.json
    const string url = server.system.file_api + "/submission/" + sub_id + "/__matrix/report.json";

    LOG_INFO << "upload to filesystem " << sub_id;
    for (int time = server.system.post_retry_time - 1; time >= 0; time--) {
        try {
            net::upload_file_string(url, report, server.system.file_connect_timeout);
            break;  // on no error, exit retry loop
        } catch (const network_error &err) {
            LOG_WARN << "unable to upload judge report: " << err.what()
                     << ", retrying with " << time << " times remaining";
	 }
    	 LOG_DEBUG << "upload retry time: " << time;
    }
    LOG_DEBUG << "finish uploading to filesystem";
}

static void report_to_server(configuration &server, bool is_complete, const judge_report &report) {
    // 确保 JSON dump 失败不会导致写数据库失败
    // 并确保评测完成后一定可以 ack 消息以避免卡评测的情况出现
    // 消息队列必须在 ack 完消息后再取消息
    string report_string;
    int grade = report.grade;
    try {
        report_string = report.report.dump();
    } catch (std::exception &ex) {
        LOG_WARN << "unable to dump report: " << boost::diagnostic_information(ex);
        json report = {{"error", "JudgeSystem cannot dump report: " + boost::diagnostic_information(ex)}};
        report_string = report.dump();
        grade = -2;  // 将提交标记为评测失败
    }

    try {
        LOG_INFO << "updating submission " << report.sub_id;
        // server.matrix_db.execute("UPDATE submission SET report=? WHERE sub_id=?",
        //                          report_string, report.sub_id);
        // POST评测报告到文件系统
        post_report_to_fs(server, report_string, report.sub_id);
        if (is_complete) {
            LOG_INFO << "updating database record of submission " << report.sub_id;
            if (!server.matrix_db.ping()) connect_database(server.matrix_db, server.matrix_dbcfg);
            // server.matrix_db.execute("UPDATE submission SET grade=?, report=? WHERE sub_id=?",
            //                          grade, report_string, report.sub_id);
            server.matrix_db.execute("UPDATE submission SET grade=? WHERE sub_id=?",
                                     grade, report.sub_id);
            // post_report_to_fs(server, report_string, report.sub_id);
        }
        LOG_INFO << "finished updating submission " << report.sub_id;
    } catch (std::exception &ex) {
        LOG_ERROR << "unable to report to server: " << ex.what() << ", report: " << report_string;
    }
}

static void from_json_mcourse(rabbitmq_channel::envelope_type envelope, const json &j, configuration &server, unique_ptr<submission> &submit) {
    LOG_DEBUG << "Receive matrix course submission: " << j.dump(4);

    judge_request request = j;

    try {
        switch (request.prob_type) {
            case judge_request::programming: {
                auto prog_submit = make_unique<programming_submission>();
                prog_submit->category = server.category();
                prog_submit->sub_id = to_string(request.sub_id);
                prog_submit->prob_id = to_string(request.prob_id);
                prog_submit->envelope = envelope;
                prog_submit->config = request.config;
                prog_submit->updated_at = request.updated_at;

                from_json_programming(request.config, request.detail, request.sub_type, server, *prog_submit.get());
                submit = move(prog_submit);
            } break;
            case judge_request::choice: {
                auto choice_submit = make_unique<choice_submission>();
                choice_submit->category = server.category();
                choice_submit->sub_id = to_string(request.sub_id);
                choice_submit->prob_id = to_string(request.prob_id);
                choice_submit->envelope = envelope;
                choice_submit->config = request.config;
                choice_submit->updated_at = request.updated_at;

                from_json_choice(request.config, request.detail, request.sub_type, server, *choice_submit.get());
                submit = move(choice_submit);
            } break;
            case judge_request::program_blank_filling: {
                // TODO:
            } break;
            case judge_request::output: {
                auto prog_submit = make_unique<program_output_submission>();
                prog_submit->category = server.category();
                prog_submit->sub_id = to_string(request.sub_id);
                prog_submit->prob_id = to_string(request.prob_id);
                prog_submit->envelope = envelope;
                prog_submit->config = request.config;
                prog_submit->updated_at = request.updated_at;

                from_json_program_output(request.config, request.detail, request.sub_type, server, *prog_submit.get());
                submit = move(prog_submit);
            } break;
            default: {
                BOOST_THROW_EXCEPTION(judge_exception("Unrecognized problem type"));
            } break;
        }

        // 标记当前 submission 正在评测
        server.matrix_db.execute("UPDATE submission SET grade=-1 WHERE sub_id=?", request.sub_id);
    } catch (...) {
        LOG_WARN << "Submission [" << request.prob_id << "-" << request.sub_id << "] is not valid";
        judge_report report;
        report.sub_id = to_string(request.sub_id);
        report.prob_id = to_string(request.prob_id);
        report.grade = 0;
        report.report = {{"compile check",
                          {{"continue", false},
                           {"grade", 0},
                           {"compile check", "Invalid Submission"}}}};
        report_to_server(server, true, report);
        throw;
    }
}

bool configuration::fetch_submission(unique_ptr<submission> &submit) {
    rabbitmq_channel::envelope_type envelope;
    if (sub_fetcher->fetch(envelope, 0)) {
        try {
            from_json_mcourse(envelope, json::parse(envelope.body()), *this, submit);
            return true;
        } catch (...) {
            envelope.ack();
            throw;
        }
    }

    return false;
}

void configuration::summarize_invalid(submission &submit) {
    BOOST_THROW_EXCEPTION(judge_exception() << "Invalid submission " << submit);
}

static json get_error_report(const status &stat, const judge_task_result &result) {
    error_report report;
    report.result = status_string.at(stat);
    report.message = result.error_log;
    return report;
}

/**
 * 执行条件
 * 1. 配置中开启编译检测（编译检测满分大于0）
 * 2. 已存在提交代码的源文件
 */
static bool summarize_compile_check(configuration &, boost::rational<int> &total_score, programming_submission &submit, json &compile_check_json) {
    compile_check_report compile_check;
    int full_grade = (int)round(boost::rational_cast<double>(submit.judge_tasks[0].score));
    compile_check.cont = false;
    compile_check.message = submit.results[0].report;
    compile_check.error_log = submit.results[0].error_log;
    if (submit.results[0].status == status::ACCEPTED) {
        compile_check.cont = true;
        compile_check.grade = full_grade;
        compile_check.message = "pass";
        total_score += submit.judge_tasks[0].score;
        compile_check_json = compile_check;
    } else if (submit.results[0].status == status::SYSTEM_ERROR) {
        compile_check.grade = 0;
        compile_check_json = get_error_report(status::SYSTEM_ERROR, submit.results[0]);
    } else {
        compile_check.grade = 0;
        compile_check_json = compile_check;
    }
    return true;
}

/**
 * 通过提交的随机生成器产生随机输入文件，分别运行标准答案与提交的答案，比较输出是否相同。
 * 
 * 执行条件
 * 1. 配置中开启随机检测（随机检测满分大于0）
 * 2. 已存在提交代码的可执行文件
 * 3. 已存在随机输入生成器与题目答案源文件
 * 
 * 评分规则
 * 得分=（通过的测试数）/（总测试数）× 总分。
 */
static bool summarize_random_check(configuration &, boost::rational<int> &total_score, programming_submission &submit, json &random_check_json) {
    bool exists = false;
    random_check_report random_check;
    boost::rational<int> score;
    for (size_t i = 0; i < submit.results.size(); ++i) {
        auto &task_result = submit.results[i];
        if (submit.judge_tasks[i].tag == RANDOM_CHECK_TYPE) {
            if (task_result.status == status::PENDING) continue;
            if (task_result.status == status::RUNNING) continue;
            if (task_result.status == status::DEPENDENCY_NOT_SATISFIED) continue;
            exists = true;

            if (task_result.status == status::ACCEPTED) {
                score += submit.judge_tasks[i].score;
                continue;
            } else if (task_result.status == status::PARTIAL_CORRECT) {
                score += task_result.score * submit.judge_tasks[i].score;
            } else if (task_result.status == status::SYSTEM_ERROR ||
                       task_result.status == status::RANDOM_GEN_ERROR ||
                       task_result.status == status::EXECUTABLE_COMPILATION_ERROR ||
                       task_result.status == status::COMPARE_ERROR) {
                random_check_json = get_error_report(task_result.status, task_result);
                return true;
            }

            check_case_report kase;
            kase.result = status_string.at(task_result.status);
            kase.timeused = task_result.run_time * 1000;
            kase.memoryused = task_result.memory_used >> 20;
            kase.stdin = read_file_content(task_result.data_dir / "input" / "testdata.in", judge::MAX_IO_SIZE);
            kase.standard_stdout = read_file_content(task_result.data_dir / "output" / "testdata.out", judge::MAX_IO_SIZE);
            kase.stdout = read_file_content(task_result.run_dir / "run" / "testdata.out", judge::MAX_IO_SIZE);
            kase.message = read_file_content(task_result.run_dir / "program.err", judge::MAX_IO_SIZE);
            random_check.cases.push_back(kase);
        }
    }
    random_check.grade = (int)round(boost::rational_cast<double>(score));
    random_check_json = random_check;
    total_score += score;
    return exists;
}

static bool summarize_standard_check(configuration &, boost::rational<int> &total_score, programming_submission &submit, json &standard_check_json) {
    bool exists = false;
    standard_check_report standard_check;
    boost::rational<int> score;
    for (size_t i = 0; i < submit.results.size(); ++i) {
        auto &task_result = submit.results[i];
        if (submit.judge_tasks[i].tag == STANDARD_CHECK_TYPE) {
            if (task_result.status == status::PENDING) continue;
            if (task_result.status == status::RUNNING) continue;
            if (task_result.status == status::DEPENDENCY_NOT_SATISFIED) continue;
            exists = true;

            if (task_result.status == status::ACCEPTED) {
                score += submit.judge_tasks[i].score;
                continue;
            } else if (task_result.status == status::PARTIAL_CORRECT) {
                score += task_result.score * submit.judge_tasks[i].score;
            } else if (task_result.status == status::SYSTEM_ERROR ||
                       task_result.status == status::RANDOM_GEN_ERROR ||
                       task_result.status == status::EXECUTABLE_COMPILATION_ERROR ||
                       task_result.status == status::COMPARE_ERROR) {
                standard_check_json = get_error_report(task_result.status, task_result);
                return true;
            }

            check_case_report kase;
            kase.result = status_string.at(task_result.status);
            kase.timeused = task_result.run_time * 1000;
            kase.memoryused = task_result.memory_used >> 20;
            kase.stdin = read_file_content(task_result.data_dir / "input" / "testdata.in", judge::MAX_IO_SIZE);
            kase.standard_stdout = read_file_content(task_result.data_dir / "output" / "testdata.out", judge::MAX_IO_SIZE);
            kase.stdout = read_file_content(task_result.run_dir / "run" / "testdata.out", judge::MAX_IO_SIZE);
            kase.message = read_file_content(task_result.run_dir / "program.err", judge::MAX_IO_SIZE);
            standard_check.cases.push_back(kase);
        }
    }
    standard_check.grade = (int)round(boost::rational_cast<double>(score));
    standard_check_json = standard_check;
    total_score += score;
    return exists;
}

/**
 * 静态检查目前只支持C/C++
 * 采用oclint进行静态检查，用于检测部分代码风格和代码设计问题
 * 
 * 执行条件
 * 1. 配置中开启静态检查（静态检查满分大于 0）
 * 
 * 评分规则
 * oclint评测违规分3个等级：priority 1、priority 2、priority 3
 * 评测代码每违规一个 priority 1 扣 20%，每违规一个 priority 2 扣 10%，违规 priority 3 不扣分。扣分扣至 0 分为止.
 */
static bool summarize_static_check(configuration &, boost::rational<int> &total_score, programming_submission &submit, json &static_check_json) {
    bool exists = false;
    static_check_report static_check;
    static_check.cont = false;
    boost::rational<int> score;
    for (size_t i = 0; i < submit.results.size(); ++i) {
        auto &task_result = submit.results[i];
        if (submit.judge_tasks[i].tag == STATIC_CHECK_TYPE) {
            if (task_result.status == status::PENDING) continue;
            if (task_result.status == status::RUNNING) continue;
            if (task_result.status == status::DEPENDENCY_NOT_SATISFIED) continue;
            exists = true;

            try {
                static_check.report = json::parse(task_result.report);
            } catch (std::exception &e) {  // 非法 json 文件
                task_result.status = status::SYSTEM_ERROR;
                static_check.report = task_result.error_log = task_result.error_log + "\n" + boost::diagnostic_information(e) + "\n" + task_result.report;
            }

            static_check.cont = true;
            if (task_result.status == status::ACCEPTED) {
                score += submit.judge_tasks[i].score;
            } else if (task_result.status == status::PARTIAL_CORRECT) {
                score += task_result.score * submit.judge_tasks[i].score;
            } else if (task_result.status == status::SYSTEM_ERROR ||
                       task_result.status == status::RANDOM_GEN_ERROR ||
                       task_result.status == status::EXECUTABLE_COMPILATION_ERROR ||
                       task_result.status == status::COMPARE_ERROR) {
                static_check.grade = 0;
                static_check.cont = false;
                static_check.report = task_result.error_log;
                static_check_json = static_check;
                return true;
            }
        }
    }
    static_check.grade = (int)round(boost::rational_cast<double>(score));
    static_check_json = static_check;
    total_score += score;
    return exists;
}

/**
 * 采用 valgrind 进行内存检测，用于发现内存泄露、访问越界等问题
 * 内存检测需要提交代码的可执行文件，并且基于标准测试或随机测试（优先选用标准测试），因此只有当标准测试或随机测试的条件满足时才能进行内存测试
 * 
 * 执行条件
 * 1. 配置中开启内存检测（内存检测满分大于0）
 * 2. 已存在提交代码的可执行文件
 * 3. 已存在标准测试输入（优先选择）或随机输入生成器
 * 
 * 评分规则
 * 内存检测会多次运行提交代码，未检测出问题则通过，最后总分为通过数/总共运行次数 × 内存检测满分分数
 */
static bool summarize_memory_check(configuration &, boost::rational<int> &total_score, programming_submission &submit, json &memory_check_json) {
    bool exists = false;
    memory_check_report memory_check;
    boost::rational<int> score, full_score;
    for (size_t i = 0; i < submit.results.size(); ++i) {
        auto &task_result = submit.results[i];
        if (submit.judge_tasks[i].tag == MEMORY_CHECK_TYPE) {
            full_score += submit.judge_tasks[i].score;
            if (task_result.status == status::PENDING) continue;
            if (task_result.status == status::RUNNING) continue;
            if (task_result.status == status::DEPENDENCY_NOT_SATISFIED) continue;
            exists = true;

            if (task_result.status == status::ACCEPTED) {
                score += submit.judge_tasks[i].score;
            } else if (task_result.status == status::WRONG_ANSWER) {
                memory_check_error_report kase;
                try {
                    kase.valgrindoutput = json::parse(task_result.report);
                } catch (std::exception &e) {  // 非法 json 文件
                    kase.message = task_result.error_log + "\n" + boost::diagnostic_information(e) + "\n" + task_result.report;
                }
                kase.stdin = read_file_content(task_result.data_dir / "input" / "testdata.in", judge::MAX_IO_SIZE);
                memory_check.report.push_back(kase);
            } else if (task_result.status == status::TIME_LIMIT_EXCEEDED) {
                memory_check_error_report kase;
                kase.message = "Time limit exceeded";
                kase.stdin = read_file_content(task_result.data_dir / "input" / "testdata.in", judge::MAX_IO_SIZE);
                memory_check.report.push_back(kase);
            } else {
                memory_check_error_report kase;
                kase.message = fmt::format("Internal Error, status: {}:\n{}", get_display_message(task_result.status), task_result.error_log);
                memory_check.report.push_back(kase);
            }
        }
    }
    memory_check.grade = (int)round(boost::rational_cast<double>(score));
    memory_check_json = memory_check;
    total_score += score;
    return exists;
}

static bool summarize_gtest_check(configuration &, boost::rational<int> &total_score, programming_submission &submit, json &gtest_check_json, json &standard_check_json) {
    bool exists = false;
    gtest_check_report gtest_check;

    standard_check_report standard_check;
    standard_check.grade = 0;

    boost::rational<int> score;
    json config = any_cast<json>(submit.config);
    if (!nlohmann::exists(config, "google tests info"))
        return false;
    gtest_check.info = config.at("google tests info");
    for (size_t i = 0; i < submit.results.size(); ++i) {
        auto &task_result = submit.results[i];
        if (submit.judge_tasks[i].tag == GTEST_CHECK_TYPE) {
            if (task_result.status == status::PENDING) continue;
            if (task_result.status == status::RUNNING) continue;
            if (task_result.status == status::DEPENDENCY_NOT_SATISFIED) continue;
            exists = true;

            score = submit.judge_tasks[i].score;

            check_case_report kase;
            kase.stdout = read_file_content(task_result.run_dir / "run" / "testdata.out", judge::MAX_IO_SIZE);
            standard_check.cases.clear();
            standard_check.cases.push_back(kase);

            if (task_result.status == status::MEMORY_LIMIT_EXCEEDED) {
                score = 0;
                // 前端根据这个来检查是否是 MLE
                gtest_check.error_message = "Memery out of limit";
                standard_check_json = standard_check;
                break;
            } else if (task_result.status == status::RUNTIME_ERROR ||
                       task_result.status == status::SEGMENTATION_FAULT ||
                       task_result.status == status::FLOATING_POINT_ERROR) {
                score = 0;
                // 前端根据这个来检查是否是 RE
                gtest_check.error_message = "Run time error";
                standard_check_json = standard_check;
                break;
            } else if (task_result.status == status::TIME_LIMIT_EXCEEDED) {
                score = 0;
                // 前端根据这个来检查是否是 TLE
                gtest_check.error_message = "Time out of limit";
                standard_check_json = standard_check;
                break;
            } else if (task_result.status == status::SYSTEM_ERROR ||
                       task_result.status == status::RANDOM_GEN_ERROR ||
                       task_result.status == status::EXECUTABLE_COMPILATION_ERROR ||
                       task_result.status == status::COMPARE_ERROR) {
                score = 0;
                // 此时前端会返回 Unknown Error
                gtest_check.error_message = task_result.error_log;
                standard_check_json = standard_check;
                break;
            }

            try {
                // report 格式参见 exec/compare/gtest/run
                json report = json::parse(task_result.report);
                if (report.count("report") && report.at("report").is_array()) {
                    json cases = report.at("report");
                    for (const json &testcase : cases) {
                        if (!testcase.count("case") || !config.at("grading").count("google tests detail") || !config.at("grading").at("google tests detail").count(testcase.at("case").get<string>())) continue;
                        int grade = config.at("grading").at("google tests detail").at(testcase.at("case").get<string>()).get<int>();
                        score -= grade;
                        gtest_check.failed_cases[testcase.at("case").get<string>()] = grade;
                    }
                }
            } catch (std::exception &e) {  // 非法 json 文件
                score = 0;
                string stdout = read_file_content(task_result.run_dir / "run" / "testdata.out", judge::MAX_IO_SIZE);
                if (task_result.report.empty())  // 因为某些原因 Google Test 未能输出 test_detail.xml，我们返回标准输出内容
                    gtest_check.error_message = stdout.empty() ? "Google Test 未运行，请你检查你的程序" : "Google Test 未运行，下面是你的程序的标准输出内容: \n" + stdout;
                else
                    gtest_check.error_message = boost::diagnostic_information(e) + "\n" + task_result.report;
                break;
            }

            if (task_result.status == status::ACCEPTED) {
            } else if (task_result.status == status::PARTIAL_CORRECT ||
                       task_result.status == status::WRONG_ANSWER) {
                standard_check_json = standard_check;
            } else {
                score = 0;
                gtest_check.error_message = status_string.at(task_result.status);
            }
        }
    }
    gtest_check.grade = (int)round(boost::rational_cast<double>(score));
    gtest_check_json = gtest_check;
    total_score += score;
    return exists;
}

void summarize_programming(configuration &server, programming_submission &submit) {
    judge_report report;
    report.sub_id = submit.sub_id;
    report.prob_id = submit.prob_id;
    report.is_complete = submit.finished == submit.judge_tasks.size();
    report.report = {};

    for (const auto &task : submit.judge_tasks) {
        LOG_INFO << "Judge Task " << task.tag;
    }
    for (const auto &result : submit.results) {
        LOG_INFO << "Judge Result for " << result.id << ", " << result.tag << ", status: " << static_cast<int>(result.status);
    }

    boost::rational<int> total_score;

    if (json compile_check_json; summarize_compile_check(server, total_score, submit, compile_check_json))
        report.report["compile check"] = compile_check_json;

    if (json memory_check_json; summarize_memory_check(server, total_score, submit, memory_check_json))
        report.report["memory check"] = memory_check_json;

    if (json random_check_json; summarize_random_check(server, total_score, submit, random_check_json))
        report.report["random tests"] = random_check_json;

    if (json standard_check_json; summarize_standard_check(server, total_score, submit, standard_check_json))
        report.report["standard tests"] = standard_check_json;

    if (json static_check_json; summarize_static_check(server, total_score, submit, static_check_json))
        report.report["static check"] = static_check_json;

    if (json gtest_check_json, standard_check_json; summarize_gtest_check(server, total_score, submit, gtest_check_json, standard_check_json)) {
        report.report["google tests"] = gtest_check_json;
        if (!standard_check_json.is_null()) report.report["standard tests"] = standard_check_json;
    }

    report.grade = (int)round(boost::rational_cast<double>(total_score));

    report_to_server(server, report.is_complete, report);
    if (report.is_complete)
        any_cast<rabbitmq_channel::envelope_type>(submit.envelope).ack();

    // LOG_DEBUG << "submission report: " << report_json.dump(4);
}

void summarize_choice(configuration &server, choice_submission &submit) {
    judge_report report;
    report.grade = 0;
    report.sub_id = submit.sub_id;
    report.prob_id = submit.prob_id;
    report.is_complete = true;
    json detail = json::array();
    int qid = 0;
    for (auto &q : submit.questions) {
        report.grade += q.grade;
        detail += {{"grade", q.grade},
                   {"question_id", qid++},
                   {"student_answer", q.student_answer},
                   {"standard_answer", q.standard_answer}};
    }

    report.report = {{"report", detail}};

    report_to_server(server, report.is_complete, report);
    if (report.is_complete)
        any_cast<rabbitmq_channel::envelope_type>(submit.envelope).ack();

    // LOG_DEBUG << "choice submission report: " << report_json.dump(4);
}

void summarize_program_output(configuration &server, program_output_submission &submit) {
    judge_report report;
    report.grade = 0;
    report.sub_id = submit.sub_id;
    report.prob_id = submit.prob_id;
    report.is_complete = true;

    vector<double> grade_detail;
    for (auto &q : submit.questions) {
        report.grade += q.grade;
        grade_detail.push_back(q.grade);
    }

    report.report = {{"grade", report.grade},
                     {"grade_detail", grade_detail}};

    report_to_server(server, report.is_complete, report);
    if (report.is_complete)
        any_cast<rabbitmq_channel::envelope_type>(submit.envelope).ack();

    // LOG_DEBUG << "Matrix Course program output submission report: " << report_json.dump(4);
}

void configuration::summarize(submission &submit, bool ack) {
    if (submit.type == "programming") {
        summarize_programming(*this, dynamic_cast<programming_submission &>(submit));
    } else if (submit.type == "choice") {
        summarize_choice(*this, dynamic_cast<choice_submission &>(submit));
    } else if (submit.type == "program_blank_filling") {
    } else if (submit.type == "output") {
        summarize_program_output(*this, dynamic_cast<program_output_submission &>(submit));
    } else {
        throw runtime_error("Unrecognized submission type " + submit.type);
    }
}

}  // namespace judge::server::mcourse
