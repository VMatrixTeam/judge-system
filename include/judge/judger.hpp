#pragma once

#include <functional>
#include "common/concurrent_queue.hpp"
#include "common/messages.hpp"
#include "judge/submission.hpp"

namespace judge {

/**
 * @brief 表示一种题目类型的评测逻辑
 */
struct judger {
    /**
     * @brief judger 负责评测哪种类型的提交
     * 目前可以为：programming, choice, output, program_blank_filling
     */
    virtual std::string type() const = 0;

    /**
     * @brief 检查 submission 是不是当前 judger 所负责的，而且内部约束都满足
     * @note verify 调用时将占有全局锁，不可以在 verify 内继续调用别的锁，否则可能与全局锁一起死锁
     * @param submit 要检查正确性的 submission
     * @return true 若 submission 是合法的
     */
    virtual bool verify(submission &submit) const = 0;

    /**
     * @brief 将检查通过的 submission 拆解成评测子任务并分发到内部的消息队列中等待后续处理
     * 调用方确保 submit 是已经通过验证的
     * @note 如果需要等待合适的时机再执行评测任务，应该在此函数内执行等待，此函数不会占有全局锁
     * @param task_queue 评测子任务的消息队列
     * @param submit 要被评测的提交信息
     * @return true 若成功分发子任务
     */
    virtual bool distribute(concurrent_queue<message::client_task> &task_queue, submission &submit) const = 0;

    /**
     * @brief 当前从消息队列中取到该消息的 worker 将评测子任务发给 judger 进行实际的评测
     * @param task 拆解后的子任务
     * @param task_queue 允许子任务评测完成后继续分发后续的子任务评测
     * @param execcpuset 当前评测任务可以使用哪些 cpu 核心进行评测
     */
    virtual void judge(const message::client_task &task, concurrent_queue<message::client_task> &task_queue, const std::string &execcpuset) const = 0;

    /**
     * @brief 注册评测结束的事件回调函数
     * 这些回调函数会在一个提交评测结束后被调用，通常是回收内存以及返回提交结果
     */
    void on_judge_finished(std::function<void(submission &)> callback);

    /**
     * @brief 评测结束，调用已注册的回调函数
     * @param submit 
     */
    void fire_judge_finished(submission &submit) const;

private:
    std::vector<std::function<void(submission &)>> judge_finished;
};

}  // namespace judge
