#pragma once

#include "monitor/monitor.hpp"
#include "server/config.hpp"

namespace judge {

struct interrupt_monitor : public monitor {
    interrupt_monitor();

    void start_submission(const submission &submit);

    void start_judge_task(int worker_id, const message::client_task &client_task);

    void end_judge_task(int worker_id, const message::client_task &client_task);

    void end_submission(const submission &submit);

    void interrupt_submissions();

    void interrupt_judge_tasks();

    void get_judge_time(submission &submit);

private:
    std::mutex mut;

    std::map<unsigned, const submission *> running_submissions;
    std::map<std::pair<unsigned, int>, message::client_task> running_client_tasks;
};

}  // namespace judge
