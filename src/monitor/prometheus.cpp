#include "monitor/prometheus.hpp"

#include <fmt/core.h>

#include <ctime>
#include <chrono>

#include "common/net_utils.hpp"
#include "common/periodic_timer.hpp"
#include "worker.hpp"


namespace judge {
using namespace std;

prometheus_monitor::prometheus_monitor(std::shared_ptr<prometheus::Registry> registry) : submission_started(prometheus::BuildCounter()
                                                                                                                .Name("judge_system_submissions_started")
                                                                                                                .Help("The number of submissions that has started judging")
                                                                                                                .Register(*registry)),
                                                                                         submission_ended(prometheus::BuildCounter()
                                                                                                              .Name("judge_system_submissions_ended")
                                                                                                              .Help("The number of submissions that has finished judging")
                                                                                                              .Register(*registry)),
                                                                                         judge_task_started(prometheus::BuildCounter()
                                                                                                                .Name("judge_system_judge_tasks_started")
                                                                                                                .Help("The number of judge tasks that has started judging")
                                                                                                                .Register(*registry)),
                                                                                         judge_task_ended(prometheus::BuildCounter()
                                                                                                              .Name("judge_system_judge_tasks_ended")
                                                                                                              .Help("The number of judge tasks that has finished judging")
                                                                                                              .Register(*registry)),
                                                                                         worker_status(prometheus::BuildGauge()
                                                                                                           .Name("judge_system_workers_status")
                                                                                                           .Help("Show status of each worker (0:START   ; 1:JUDGING   ; 2:IDLE   ; 3:CRASHED   ; 4:STOPPED)")
                                                                                                           .Register(*registry)),
                                                                                         judge_time(prometheus::BuildGauge()
                                                                                                           .Name("submmsion_judge_time")
                                                                                                           .Help("The time use to judge a submmision (/ms)")
                                                                                                           .Register(*registry)) {}

void prometheus_monitor::start_submission(const submission &submit) {
    submission_started.Add({{"type", submit.type},
                            {"category", submit.category}})
        .Increment();
}

void prometheus_monitor::start_judge_task(int worker_id, const message::client_task &task) {
    judge_task_started.Add({{"worker_id", std::to_string(worker_id)},
                            {"type", task.submit->type},
                            {"category", task.submit->category}})
        .Increment();
    ;
}

void prometheus_monitor::end_judge_task(int worker_id, const message::client_task &task) {
    judge_task_ended.Add({{"worker_id", std::to_string(worker_id)},
                          {"type", task.submit->type},
                          {"category", task.submit->category}})
        .Increment();
}

void prometheus_monitor::end_submission(const submission &submit) {
    submission_ended.Add({{"type", submit.type},
                          {"category", submit.category}})
        .Increment();
}

void prometheus_monitor::report_error(int, const std::string &) {
}

std::string stateToString(worker_state state) {
    switch (state) {
        case worker_state::JUDGING:
            return "judging";
        case worker_state::CRASHED:
            return "crashed";
        case worker_state::IDLE:
            return "idle";
        case worker_state::START:
            return "start";
        case worker_state::STOPPED:
            return "stopped";
    }
    return "unknown";
}

void prometheus_monitor::worker_state_changed(int worker_id, worker_state state, const std::string & /* info */) {
    std::string worker_id_str = std::to_string(worker_id);

    // Set state
    worker_status.Add({{"worker_id", worker_id_str}}).Set(static_cast<int>(state));
}

void prometheus_monitor::get_judge_time(submission &submit) {
    judge_time.Add({{"type", submit.type},
                    {"category", submit.category}}).
        Set(submit.judge_time.template duration<chrono::milliseconds>().count());
}

}  // namespace judge
