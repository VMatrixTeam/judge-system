#include "monitor/interrupt_monitor.hpp"
#include "logging.hpp"

using namespace std;

namespace judge {

interrupt_monitor::interrupt_monitor() {}

void interrupt_monitor::start_submission(const submission &submit) {
    scoped_lock guard(mut);
    running_submissions[submit.judge_id] = &submit;
}

void interrupt_monitor::start_judge_task(int, const message::client_task &task) {
    scoped_lock guard(mut);
    running_client_tasks[make_pair(task.submit->judge_id, task.id)] = task;
}

void interrupt_monitor::end_judge_task(int, const message::client_task &task) {
    scoped_lock guard(mut);
    running_client_tasks.erase(make_pair(task.submit->judge_id, task.id));
}

void interrupt_monitor::end_submission(const submission &submit) {
    scoped_lock guard(mut);
    running_submissions.erase(submit.judge_id);
}

void interrupt_monitor::interrupt_submissions() {
    scoped_lock guard(mut);
    LOG_ERROR << "Submissions under judging";
    for (auto &p : running_submissions) {
        LOG_ERROR << p.second->category << "-" << p.second->prob_id << "-" << p.second->sub_id;
    }
}

void interrupt_monitor::interrupt_judge_tasks() {
    scoped_lock guard(mut);

    LOG_ERROR << "Judge tasks under judging";
    for (auto &p : running_client_tasks) {
        LOG_ERROR << p.second.submit->category << "-" << p.second.submit->prob_id << "-" << p.second.submit->sub_id << "-" << p.second.id << "-" << p.second.name;
    }
}

void interrupt_monitor::get_judge_time(submission &submit) {

}

}  // namespace judge
