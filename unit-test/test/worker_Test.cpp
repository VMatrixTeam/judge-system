#include "test/worker.hpp"
#include "logging.hpp"
#include <gtest/gtest.h>
#include <filesystem>
#include "common/utils.hpp"
#include "config.hpp"
#include "env.hpp"

namespace judge {
using namespace std;

void push_submission(const judger &j, concurrent_queue<message::client_task> &task_queue, submission &submit) {
    EXPECT_TRUE(j.verify(submit));
    EXPECT_TRUE(j.distribute(task_queue, submit));
}

void worker_loop(const judger &j, concurrent_queue<message::client_task> &task_queue) {
    while (true) {
        message::client_task task;
        if (!task_queue.try_pop(task)) break;

        j.judge(task, task_queue, "0");
    }
}

void setup_test_environment() {
    put_error_codes();

    if (getenv("DEBUG")) judge::DEBUG = true;

    judge::EXEC_DIR = filesystem::weakly_canonical(filesystem::path("exec"));
    set_env("JUDGE_UTILS", (judge::EXEC_DIR / "utils").string(), true);
    CHECK(filesystem::is_directory(judge::EXEC_DIR))
        << "Executables directory " << judge::EXEC_DIR << " does not exist";
    set_env("EXECDIR", judge::EXEC_DIR.string());
    judge::CACHE_DIR = filesystem::path("/tmp/test/cache");
    set_env("CACHEDIR", "/tmp/test/cache");
    filesystem::create_directories(judge::CACHE_DIR);
    judge::RUN_DIR = filesystem::path("/tmp/test/run");
    set_env("RUNDIR", "/tmp/test/run");
    filesystem::create_directories(judge::RUN_DIR);
    judge::CHROOT_DIR = filesystem::path("/chroot-docker");
    set_env("CHROOTDIR", "/tmp/test/run");
    set_env("SCRIPTMEMLIMIT", to_string(judge::SCRIPT_MEM_LIMIT), false);
    set_env("SCRIPTTIMELIMIT", to_string(judge::SCRIPT_TIME_LIMIT), false);
    set_env("SCRIPTFILELIMIT", to_string(judge::SCRIPT_FILE_LIMIT), false);
    set_env("RUNUSER", "judge-docker");
    set_env("RUNGROUP", "judge-docker");

    set_env("RUNGUARD", filesystem::weakly_canonical(filesystem::path("runguard/bin/runguard")), false);
}

}  // namespace judge
