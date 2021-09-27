
#pragma once

#include "metrics.hpp"
#include "monitor/monitor.hpp"
#include "server/config.hpp"

namespace judge {

/**
 * @brief 向课程系统报告当前 worker 的状态
 * 提供给 Matrix 课程系统用于监控评测系统状态
 */
struct prometheus_monitor : public monitor {
    prometheus::Family<prometheus::Counter> &submission_started, &submission_ended, &judge_task_started, &judge_task_ended;
    prometheus::Family<prometheus::Gauge> &worker_status, &judge_time;
    prometheus_monitor(std::shared_ptr<prometheus::Registry> registry);

    void start_submission(const submission &submit) override;
    void start_judge_task(int worker_id, const message::client_task &client_task) override;
    void end_judge_task(int worker_id, const message::client_task &client_task) override;
    void end_submission(const submission &submit) override;
    void report_error(int worker_id, const std::string &error_log) override;
    void worker_state_changed(int worker_id, worker_state state, const std::string &info) override;
    void get_judge_time(submission &submit) override;
};

}  // namespace judge
