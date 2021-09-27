#pragma once

#include <any>
#include <boost/rational.hpp>
#include <filesystem>
#include <map>

#include "common/concurrent_queue.hpp"
#include "common/io_utils.hpp"
#include "common/messages.hpp"
#include "common/status.hpp"
#include "judge/judger.hpp"
#include "judge/submission.hpp"
#include "program.hpp"
#include "monitor/monitor.hpp"

/**
 * 这个头文件包含提交信息
 * 包含：
 * 1. programming_submission 类（表示一个编程题提交）
 * 2. test_case_data 类（表示一个标准测试数据组）
 * 3. judge_task 类（表示一个测试点）
 */
namespace judge {

/**
 * @brief 表示一组标准测试数据
 * 标准测试数据和测试点的顺序没有必然关系，比如一道题同时存在
 * 标准测试和内存测试，两种测试都需要使用标准测试数据进行评测，
 * 这种情况下会有多组测试点使用同一个标准测试数据
 */
struct test_case_data {
    /**
     * @brief Construct a new test case data object
     * 默认构造函数
     */
    test_case_data();

    /**
     * @brief Construct a new test case data object
     * 这个类中使用了 unique_ptr，因此只能移动
     * @param other 要移动的对象
     */
    test_case_data(test_case_data &&other);

    /**
     * @brief 该组标准测试的输入数据
     * @note 输入数据由选手程序手动打开，若输入数据文件名为 testdata.in 则喂给 stdin
     */
    std::vector<asset_uptr> inputs;

    /**
     * @brief 该组标准测试的输出数据
     * @note 输入数据由选手程序手动打开，若输出数据文件名为 testdata.out 则和 stdout 比对
     */
    std::vector<asset_uptr> outputs;
};

struct judge_task;

struct judge_task_result;

/**
 * @brief 表示评测任务执行完成后要执行的操作
 */
struct action {
    enum class condition {
        ALWAYS,               // 总是执行本动作
        ACCEPTED,             // 要求测试点通过才能执行本动作
        NON_ACCEPTED,         // 要求测试点不通过才能执行本动作
        PARTIAL_CORRECT,      // 要求测试点不为 0 分时才能执行本动作
        NON_PARTIAL_CORRECT,  // 要求测试点为 0 分时才能执行本动作
    };

    /**
     * @brief 用于标记操作类型，方便处理评测报告使用
     */
    std::string tag;

    /**
     * @brief 执行动作的条件
     */
    condition cond;

    /**
     * @brief 执行该操作
     * @param submission 该操作所属的提交
     * @param judge_task 该操作所属的评测任务
     */
    virtual bool act(submission &, judge_task &, judge_task_result &, std::string &) const;
};

/**
 * @brief 表示一个读取文件的操作
 */
struct read_action : action {
    /**
     * @brief 读取文件后要如何返回结果
     * 如果为 text，则将文件内容保存在 judge_task_result 中
     * 如果为 upload，则将文件上传到文件系统
     */
    std::string action;

    /**
     * @brief 如果读取文件后上传文件，那么 url 的值为文件上传地址。
     * 评测系统使用 HTTP POST 请求上传文件。
     */
    std::string url;

    /**
     * @brief 表示要读取的文件路径
     * 允许使用宏
     * $DATADIR 表示该评测任务使用的数据组
     * $RUNDIR 表示该评测任务选手程序的运行目录
     * 
     * 可能的值如下：
     * $DATADIR/input/testdata.in 为读取该数据组的标准输入
     * $DATADIR/output/testdata.out 为读取该数据组的标准输出
     * $RUNDIR/testdata.out 为读取选手程序的标准输出
     */
    std::string path;

    /**
     * @brief 允许读取的文件长度最大值
     * 如果 action 为 text，那么根据此项截断返回数据
     */
    long file_limit = -1;

    /**
     * @brief 执行读取文件的操作
     * 只允许读取 DATADIR 和 RUNDIR 下的文件，其他文件一律拒绝。
     */
    bool act(submission &, judge_task &, judge_task_result &, std::string &) const override;
};

struct action_result {
    /**
     * @brief 操作的标记，方便前端/服务端处理评测报告使用，和评测请求的 action.tag 完全一致
     */
    std::string tag;

    /**
     * @brief 操作的执行结果
     */
    std::string result;

    /**
     * @brief 操作是否成功
     */
    bool success;
};

/**
 * @brief 表示一个评测任务
 * 评测任务内对选手程序执行一次评测，比如一组标准测试、一组随机测试、一组 Google 测试等等。
 */
struct judge_task : program_limit {
    enum class dependency_condition {
        ACCEPTED,         // 要求依赖的测试点通过后才能执行本测试
        PARTIAL_CORRECT,  // 要求依赖的测试点不为 0 分时才能执行本测试
        NON_TIME_LIMIT    // 仅在依赖的测试点超出时间限制后才不继续测试
    };

    /**
     * @brief 标记该评测任务的功能，提供给 judge_server 在评测报告中区分结果的含义
     * 一道题的评测可以包含多种评测类型
     * 
     * 这个类型由 judge_server 自行决定，比如对于 MOJ、Matrix Course:
     * 0: CompileCheck
     * 1: MemoryCheck
     * 2: RandomCheck
     * 3: StandardCheck
     * 4: StaticCheck
     * 5: GTestCheck
     * 
     * 对于 Sicily， 不分 type
     * 对于 forth 接口，tag 提供给服务端处理
     */
    std::string tag;

    /**
     * @brief 本测试点使用的 check script
     * 不同的测试点可能有不同的 check script
     * 对于 StaticCheck，check script 需要选择 static
     */
    std::string check_script;

    /**
     * @brief 本测试点使用的 run script
     * 不同的测试点可能有不同的 run script
     * 对于 MemoryCheck，需要选择 valgrind
     * 对于 GTestCheck，需要选择 gtest
     */
    std::string run_script;

    /**
     * @brief 本测试点使用的比较脚本
     * 为空时表示使用提交自带的脚本，也就是 submission.compare
     * 对于 MemoryCheck，需要选择 valgrind
     * 对于 GTestCheck，需要选择 gtest
     */
    std::string compare_script;

    /**
     * @brief 本测试点总分
     * 在评测 4.0 接口中不使用
     */
    boost::rational<int> score;

    /**
     * @brief 本测试点使用的是随机测试还是标准测试
     * 如果是随机测试，那么评测客户端将选择新随机一个测试数据
     * 或者从现有的随机测试数据中随机选取一个测试。
     * 如果是标准测试，那么评测客户端将根据测试点编号评测。
     */
    bool is_random = false;

    /**
     * @brief 标准测试数据 id，在 submission.test_cases 中进行索引
     * 若为随机测试，此项为第几组随机测试。
     * 随机数据生成器可能可以根据第几组随机测试而生成不同规模、不同生成方式的数据。
     */
    int testcase_id = -1;

    /**
     * @brief 该组随机测试使用的测试数据编号
     * 对于每组随机测试，我们需要缓存一批随机测试数据。
     * 由于随机测试生成器根据输入的 testcase_id 来决定生成方式，传入同样的
     * testcase_id 可以生成一批性质相同的测试数据，此时为了支持其他评测任务
     * 依赖当前使用的随机测试数据，我们需要额外保存使用了哪一个缓存好的测试数据。
     * 在评测完成后该域将修改为该组随机测试使用哪一个缓存的测试数据点。
     */
    int subcase_id = -1;

    /**
     * @brief 本测试点依赖哪个测试点，负数表示不依赖任何测试点
     * 依赖指的是，本测试点是否执行，取决于依赖的测试点的状态。
     * 可以实现标准/随机测试点未通过的情况下不测试对应的内存测试的功能。
     * 可以实现所有的测试点均需依赖编译测试的功能。
     */
    int depends_on = -1;

    /**
     * @brief 测试点依赖条件，要求依赖的测试点满足要求时才执行之后的测试点
     * 
     */
    dependency_condition depends_cond = dependency_condition::ACCEPTED;

    /**
     * @brief 本测试点的运行环境依赖哪个测试点，负数表示与 depends_on 一致。
     * 依赖指的是，如果 B 依赖 A 的运行环境，那么 A 评测任务运行完成后的当前文件夹
     * 中的数据将会保留，允许 B 评测任务执行时读取。
     * 比如 A 是编译任务，编译产生了可执行文件，那么 B 若为标准测试，B 需要使用 A
     * 产生的可执行文件，那么 B 的运行环境依赖 A。
     * @note depends_on 和 file_depends_on 有区别，在于内存测试依赖随机测试，
     * 是表示若随机测试失败，则内存测试不进行。但内存测试的运行环境依赖编译测试而不是
     * 随机测试，因为随机测试仅会产生选手程序的输出文件，若内存测试时保留了该选手程序
     * 的输出文件，选手程序就可以通过检查是否是第二次运行程序而直接不运行程序，从而
     * 绕过内存测试。因此对于内存测试，是否执行依赖随机测试，运行环境依赖编译测试。
     * TODO: 尚未决定是否需要过滤文件的功能，即只保留部分文件或不继承部分文件。
     */
    int file_depends_on = -1;

    /**
     * @brief 运行命令，传递给选手程序使用
     */
    std::vector<std::string> run_args;

    /**
     * @brief 该评测点所需要的核心数
     */
    size_t cores = 1;

    /**
     * @brief 操作执行的时间间隔，单位为秒
     * 为了支持 CI 类评测，通过允许在程序执行中途执行读取操作将程序的标准输出
     * 通过评测报告返回给前端展示，这样就可以实时查看程序的输出
     */
    int action_delay = -1;  // debug

    std::vector<read_action> actions;
};

struct judge_task_result {
    judge_task_result();
    judge_task_result(const std::string &tag, std::size_t id);

    judge::status status;

    std::string tag;

    /**
     * @brief 本测试点的 id，是 submission.judge_tasks 的下标
     * 用于返回评测结果的时候统计
     */
    std::size_t id;

    /**
     * @brief 对于部分分情况，保存 0~1 范围内的部分分比例
     * 测试点的真实分数将根据测试点总分乘上 score 计算得到
     */
    boost::rational<int> score;

    /**
     * @brief 本测试点程序运行用时
     * 单位为秒
     */
    double run_time;

    /**
     * @brief 本测试点程序运行使用的内存
     * 单位为字节
     */
    int memory_used;

    /**
     * @brief 错误报告
     * 会保存 check script 生成的 system.out，会保存所有脚本文件的日志
     */
    std::string error_log;

    /**
     * @brief 报告信息
     * 对于编译测试, 这里将保存编译器 stderr.
     * 对于 Google Test，Oclint，这里将保存测试结果的详细信息
     */
    std::string report;

    /**
     * @brief 指向当前评测的运行路径
     * 
     * RUN_DIR // 选手程序的运行目录，包含选手程序输出结果
     * ├── run // 运行路径
     * │   └── testdata.out // 选手程序的 stdout 输出
     * ├── work // 工作路径，提供给 check script 用来 mount 的
     * ├── program.err // 选手程序的 stderr 输出
     * └── runguard.err // runguard 的错误输出
     */
    std::filesystem::path run_dir;

    /**
     * @brief 指向当前评测的数据路径
     * 这个文件夹内必须包含 input 文件夹和 output 文件夹
     * 
     * DATA_DIR
     * ├── input  // 输入数据文件夹
     * │   ├── testdata.in  // 标准输入数据
     * │   └── something.else  // 其他输入数据，由选手自行打开
     * └── output // 输出数据文件夹
     *     └── testdata.out // 标准输出数据
     */
    std::filesystem::path data_dir;

    std::vector<action_result> actions;
};

/**
 * @brief 一个选手代码提交
 */
struct programming_submission : public submission {
    /**
     * @brief 本题的评测任务
     * 
     */
    std::vector<judge_task> judge_tasks;

    /**
     * @brief 标准测试的测试数据
     */
    std::vector<test_case_data> test_data;

    /**
     * @brief 选手程序的下载地址和编译命令
     */
    std::unique_ptr<submission_program> submission;

    /**
     * @brief 标准程序的下载地址和编译命令
     * @note 如果存在随机测试，该项有效
     */
    std::unique_ptr<program> standard;

    /**
     * @brief 随机数据生成程序的下载地址和编译命令
     * @note 如果存在随机测试，该项有效
     */
    std::unique_ptr<program> random;

    /**
     * @brief 表示题目的评测方式，此项不能为空，
     * 比如你可以选择 diff-all 来使用默认的比较方式，
     * 或者自行提供 source_code 或者 executable 来实现自定义校验
     */
    std::unique_ptr<program> compare;

    /**
     * @brief 存储评测结果
     */
    std::vector<judge_task_result> results;

    /**
     * @brief 已经完成了多少个测试点的评测
     */
    std::size_t finished = 0;

    /**
     * @brief 题目读锁，提交销毁后会自动释放锁
     * 正在评测的提交需要使用读锁锁住题目文件夹以避免题目更新时导致数据错误。
     */
    scoped_file_lock problem_lock;

    /**
     * @brief 提交写锁，提交销毁后会自动释放锁
     * 可能会出现连续两个同 sub_id 的提交，因此我们必须锁住提交文件夹来
     * 防止两个提交的文件发生冲突（rejudge 会导致多个同 sub_id 的提交）
     */
    scoped_file_lock submission_lock;
};

/**
 * @brief 评测编程题的 Judger 类，编程题评测的逻辑都在这个类里
 */
struct programming_judger : public judger {
    std::string type() const override;

    bool verify(submission &submit) const override;

    bool distribute(concurrent_queue<message::client_task> &task_queue, submission &submit) const override;

    void judge(const message::client_task &task, concurrent_queue<message::client_task> &task_queue, const std::string &execcpuset) const override;
};

}  // namespace judge
