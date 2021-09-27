#include "judge/choice.hpp"

#include "logging.hpp"
#include "server/judge_server.hpp"

namespace judge {
using namespace std;

/**
 * @brief 评测一道选择题
 */
static float judge_choice_question(const choice_question &q) {
    unsigned long std = 0, ans = 0;
    for (auto &choice : q.student_answer)
        ans |= 1 << choice;
    for (auto &choice : q.standard_answer)
        std |= 1 << choice;
    if (ans == std)
        return q.full_grade;
    else if ((ans & std) == ans && ans) // 得部分分： 选了 && 没有选到错误项
        return q.half_grade;
    else
        return 0;
}

string choice_judger::type() const {
    return "choice";
}

bool choice_judger::verify(submission &submit) const {
    auto sub = dynamic_cast<choice_submission *>(&submit);
    if (!sub) return false;
    LOG_INFO << "Judging choice submission [" << sub->category << "-" << sub->prob_id << "-" << sub->sub_id << "]";

    return true;
}

bool choice_judger::distribute(concurrent_queue<message::client_task> &task_queue, submission &submit) const {
    // 我们只需要发一个评测请求就行了，以便让 client 能调用我们的 judge 函数
    // 或者我们在 verify 的时候就评测完选择题然后返回 false 也行。
    judge::message::client_task client_task = {
        .submit = &submit,
        .id = 0,
        .name = "Choice",
        .cores = 1,
        .expect_runtime = 5};
    task_queue.push(client_task);
    return true;
}

void choice_judger::judge(const message::client_task &task, concurrent_queue<message::client_task> &, const string &) const {
    auto submit = dynamic_cast<choice_submission *>(task.submit);

    for (auto &q : submit->questions)
        q.grade = judge_choice_question(q);

    submit->judge_server->summarize(*submit);
    fire_judge_finished(*submit);
}

}  // namespace judge
