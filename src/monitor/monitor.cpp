#include "monitor/monitor.hpp"
namespace judge {

monitor::~monitor() {
}

void monitor::start_submission(const submission &) {
}

void monitor::start_judge_task(int, const message::client_task &) {
}

void monitor::end_judge_task(int, const message::client_task &) {
}

void monitor::worker_state_changed(int, worker_state, const std::string &) {
}

void monitor::report_error(int /*worker_id*/, const std::string &) {
}

void monitor::end_submission(const submission &) {
}

void monitor::interrupt_submissions() {
}

void monitor::interrupt_judge_tasks() {
}

void monitor::get_judge_time(submission &submit) {

}

}  // namespace judge
