#include "server/forth/forth.hpp"

#include <boost/assign.hpp>
#include <boost/exception/diagnostic_information.hpp>
#include <boost/lexical_cast.hpp>
#include <fstream>

#include "common/io_utils.hpp"
#include "common/messages.hpp"
#include "common/stl_utils.hpp"
#include "common/utils.hpp"
#include "config.hpp"
#include "judge/choice.hpp"
#include "judge/programming.hpp"
#include "logging.hpp"

namespace judge {
using namespace std;
using namespace nlohmann;

void from_json(const json &j, judge_task::dependency_condition &value) {
    string str = j.get<string>();
    if (str == "ACCEPTED")
        value = judge_task::dependency_condition::ACCEPTED;
    else if (str == "NOT_TIME_LIMIT")
        value = judge_task::dependency_condition::NON_TIME_LIMIT;
    else if (str == "PARTIAL_CORRECT")
        value = judge_task::dependency_condition::PARTIAL_CORRECT;
    else
        throw std::invalid_argument("Unrecognized dependency_condition " + str);
}

void from_json(const json &j, action::condition &value) {
    string str = j.get<string>();
    if (str == "ACCEPTED")
        value = action::condition::ACCEPTED;
    else if (str == "NON_ACCEPTED")
        value = action::condition::NON_ACCEPTED;
    else if (str == "PARTIAL_CORRECT")
        value = action::condition::PARTIAL_CORRECT;
    else if (str == "NON_PARTIAL_CORRECT")
        value = action::condition::NON_PARTIAL_CORRECT;
    else if (str == "ALWAYS" || str == "")
        value = action::condition::ALWAYS;
    else
        throw std::invalid_argument("Unrecognized action condition " + str);
}

void from_json(const json &j, read_action &value) {
    j.at("action").get_to(value.action);
    j.at("path").get_to(value.path);
    assign_optional(j, value.cond, "condition");
    assign_optional(j, value.url, "url");
    assign_optional(j, value.tag, "tag");
    assign_optional(j, value.file_limit, "file_limit");
}

void from_json(const json &j, judge_task &value) {
    assign_optional(j, value.tag, "tag");
    j.at("check_script").get_to(value.check_script);
    assign_optional(j, value.run_script, "run_script");
    assign_optional(j, value.compare_script, "compare_script");
    j.at("is_random").get_to(value.is_random);
    assign_optional(j, value.testcase_id, "testcase_id");
    j.at("depends_on").get_to(value.depends_on);
    assign_optional(j, value.depends_cond, "depends_cond");
    assign_optional(j, value.memory_limit, "memory_limit");
    j.at("time_limit").get_to(value.time_limit), value.time_limit /= 1000;
    assign_optional(j, value.file_limit, "file_limit");
    assign_optional(j, value.proc_limit, "proc_limit");
    assign_optional(j, value.run_args, "run_args");
    assign_optional(j, value.actions, "actions");
}

void from_json(const json &j, asset_uptr &asset) {
    string type = get_value<string>(j, "type");
    string name = get_value<string>(j, "name");
    if (type == "text") {
        asset = make_unique<text_asset>(name, get_value<string>(j, "text"));
    } else if (type == "remote") {
        asset = make_unique<remote_asset>(name, get_value<string>(j, "url"));
    } else if (type == "local") {
        asset = make_unique<local_asset>(name, filesystem::path(get_value<string>(j, "path")));
    } else {
        throw invalid_argument("Unrecognized asset type " + type);
    }
}

void from_json(const json &j, test_case_data &value) {
    assign_optional(j, value.inputs, "inputs");
    assign_optional(j, value.outputs, "outputs");
}

void from_json(const json &j, unique_ptr<source_code> &value) {
    value = make_unique<source_code>();
    j.at("language").get_to(value->language);
    assign_optional(j, value->entry_point, "entry_point");
    assign_optional(j, value->source_files, "source_files");
    assign_optional(j, value->assist_files, "assist_files");
    assign_optional(j, value->compile_command, "compile_command");
}

void from_json(const json &j, unique_ptr<git_repository> &value) {
    value = make_unique<git_repository>();
    j.at("url").get_to(value->url);
    assign_optional(j, value->commit, "commit");
    assign_optional(j, value->username, "username");
    assign_optional(j, value->password, "password");
    assign_optional(j, value->overrides, "overrides");
    assign_optional(j, value->source_files, "source_files");
    assign_optional(j, value->assist_files, "assist_files");
}

void from_json(const json &j, unique_ptr<program> &value) {
    string type = get_value<string>(j, "type");
    if (type == "source_code") {
        unique_ptr<source_code> p;
        from_json(j, p);
        value = move(p);
    } else if (type == "git") {
        unique_ptr<git_repository> p;
        from_json(j, p);
        value = move(p);
    } else {
        throw invalid_argument("Unrecognized program type " + type);
    }
    // TODO: type == "executable"
}

void from_json(const json &j, unique_ptr<submission_program> &value) {
    string type = get_value<string>(j, "type");
    if (type == "source_code") {
        unique_ptr<source_code> p;
        from_json(j, p);
        value = move(p);
    } else if (type == "git") {
        unique_ptr<git_repository> p;
        from_json(j, p);
        value = move(p);
    } else {
        throw invalid_argument("Unrecognized program type " + type);
    }
    // TODO: type == "executable"
}

void from_json(const json &j, programming_submission &submit) {
    j.at("judge_tasks").get_to(submit.judge_tasks);
    for (auto &test_data : j.at("test_data")) {
        test_case_data data;
        from_json(test_data, data);
        submit.test_data.push_back(move(data));
    }
    if (exists(j, "submission")) from_json(j.at("submission"), submit.submission);
    if (exists(j, "standard")) from_json(j.at("standard"), submit.standard);
    if (exists(j, "compare")) from_json(j.at("compare"), submit.compare);
    if (exists(j, "random")) from_json(j.at("random"), submit.random);
}

void from_json(const json &j, choice_question &question) {
    j.at("type").get_to(question.type);
    j.at("grade").get_to(question.grade);
    j.at("full_grade").get_to(question.full_grade);
    assign_optional(j, question.half_grade, "half_grade");
    j.at("student_answer").get_to(question.student_answer);
    j.at("standard_answer").get_to(question.standard_answer);
}

void from_json(const json &j, choice_submission &submit) {
    j.at("questions").get_to(submit.questions);
}

void from_json(const json &j, unique_ptr<submission> &submit) {
    string type = get_value<string>(j, "type");
    if (type == "programming") {
        auto p = make_unique<programming_submission>();
        from_json(j, *p);
        submit = move(p);
    } else if (type == "choice") {
        auto p = make_unique<choice_submission>();
        from_json(j, *p);
        submit = move(p);
    } else {
        throw invalid_argument("Unrecognized submission type " + type);
    }

    submit->type = type;
    j.at("category").get_to(submit->category);
    assign_optional(j, submit->prob_id, "prob_id");
    j.at("sub_id").get_to(submit->sub_id);
    assign_optional(j, submit->updated_at, "updated_at");
}

struct programming_judge_report {
    string type;
    string category;
    string prob_id;
    string sub_id;
    vector<judge_task_result> results;
};

void to_json(json &j, const action_result &result) {
    j = {{"tag", result.tag}};
    if (result.success) j["result"] = ensure_utf8(result.result);
}

void to_json(json &j, const judge_task_result &result) {
    json report;
    try {
        report = json::parse(result.report);
    } catch (json::exception &e) {
        report = json();
    }

    j = {{"tag", result.tag},
         {"status", get_display_message(result.status)},
         {"score", fmt::format("{}/{}", result.score.numerator(), max(result.score.denominator(), 1))},
         {"run_time", (int)result.run_time * 1000},
         {"memory_used", result.memory_used},
         {"error_log", ensure_utf8(result.error_log)},
         {"report", report},
         {"actions", result.actions}};
}

void to_json(json &j, const programming_judge_report &report) {
    j = {{"type", report.type},
         {"category", report.category},
         {"prob_id", report.prob_id},
         {"sub_id", report.sub_id},
         {"results", report.results}};
}

struct choice_judge_report {
    string type;
    string category;
    string prob_id;
    string sub_id;
    float grade;
    float full_grade;
    vector<float> results;
};

void to_json(json &j, const choice_judge_report &report) {
    j = {{"type", report.type},
         {"category", report.category},
         {"prob_id", report.prob_id},
         {"sub_id", report.sub_id},
         {"grade", report.grade},
         {"full_grade", report.full_grade},
         {"results", report.results}};
}

}  // namespace judge

namespace judge::server::forth {
using namespace std;
using namespace nlohmann;

configuration::configuration()
    : exec_mgr(CACHE_DIR, EXEC_DIR) {}

void configuration::init(const filesystem::path &config_path) {
    if (!filesystem::exists(config_path))
        throw runtime_error("Unable to find configuration file");
    ifstream fin(config_path);
    json config;
    fin >> config;

    config.at("category").get_to(category_name);
    config.at("submissionQueue").get_to(sub_queue);
    sub_fetcher = make_unique<rabbitmq_channel>(sub_queue);
    config.at("reportQueue").get_to(report_queue);
    judge_reporter = make_unique<rabbitmq_channel>(report_queue, true);
}

string configuration::category() const {
    return category_name;
}

const executable_manager &configuration::get_executable_manager() const {
    return exec_mgr;
}

static bool report_to_server(configuration &server, const submission &submit, const string &report) {
    server.judge_reporter->report(report, submit.category);
    return true;
}

bool configuration::fetch_submission(unique_ptr<submission> &submit) {
    rabbitmq_channel::envelope_type envelope;
    if (sub_fetcher->fetch(envelope, 0)) {
        try {
            from_json(json::parse(envelope.body()), submit);
            submit->envelope = envelope;
            return true;
        } catch (...) {
            envelope.ack();
            throw;
        }
    }

    return false;
}

void configuration::summarize_invalid(submission &submit) {
    // TODO
    any_cast<judge::server::rabbitmq_envelope>(submit.envelope).ack();
}

void summarize_programming(configuration &server, programming_submission &submit, bool ack) {
    programming_judge_report report;
    report.category = submit.category;
    report.type = submit.type;
    report.sub_id = submit.sub_id;
    report.prob_id = submit.prob_id;
    report.results = submit.results;

    if (report_to_server(server, submit, json(report).dump())) {
        LOG_DEBUG << "in summarize_programming: report_to_server(no ack) success";
        if (ack) {
            LOG_DEBUG << "in summarize_programming: report_to_server(with ack) success";
            any_cast<judge::server::rabbitmq_envelope>(submit.envelope).ack();
        }
    }
}

void summarize_choice(configuration &server, choice_submission &submit) {
    choice_judge_report report;
    report.category = submit.category;
    report.type = submit.type;
    report.sub_id = submit.sub_id;
    report.prob_id = submit.prob_id;
    report.grade = 0;
    report.full_grade = 0;
    for (auto &q : submit.questions) {
        report.grade += q.grade;
        report.full_grade += q.full_grade;
        report.results.push_back(q.grade);
    }

    if (report_to_server(server, submit, json(report).dump()))
        any_cast<rabbitmq_channel::envelope_type>(submit.envelope).ack();
}

void configuration::summarize(submission &submit, bool ack) {
    LOG_DEBUG << "in the function configuration::summarize";  // debug
    if (submit.type == "programming") {
        summarize_programming(*this, dynamic_cast<programming_submission &>(submit), ack);
    } else if (submit.type == "choice") {
        summarize_choice(*this, dynamic_cast<choice_submission &>(submit));
    } else {
        BOOST_THROW_EXCEPTION(judge_exception() << "Unrecognized submission type " << submit.type);
    }
}

}  // namespace judge::server::forth
