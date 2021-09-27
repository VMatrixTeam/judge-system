#include "common/utils.hpp"

#include <sys/wait.h>
#include <unistd.h>
using namespace std;

process_builder &process_builder::directory(const std::filesystem::path &path) {
    this->epath = true;
    this->path = path;
    return *this;
}

process_builder &process_builder::awake_period(int period, std::function<void()> callback) {
    this->period = period;
    this->callback = callback;
    return *this;
}

int process_builder::exec_program(const char **argv) {
    // 使用 POSIX 提供的函数来实现外部程序调用
    pid_t pid;
    switch (pid = fork()) {
        case -1:  // fork 失败
            throw system_error();
        case 0:  // 子进程
            // 避免子进程被终止，要求父进程处理中断信号
            signal(SIGINT, SIG_IGN);  // 忽略中断信号
            for (auto &[key, value] : env) {
                set_env(key, value);
            }
            if (epath) filesystem::current_path(path);
            execvp(argv[0], (char **)argv);
            _exit(EXIT_FAILURE);
        default:  // 父进程
            int status;
            LOG_DEBUG << "period = " << period;  // debug
            if (period > 0) {
                while (true) {
                    int ret = waitpid(pid, &status, WNOHANG);
                    LOG_DEBUG << "Father process get child process status = " << status << " child pid = " << pid;  // debug
                    if (ret == -1) throw system_error(errno, system_category(), "waitpid");
                    if (ret != 0) break;
                    sleep(period);
                    callback();
                }
            } else {
                if (waitpid(pid, &status, 0) == -1) {
                    throw system_error(errno, system_category(), "waitpid");
                }
            }

            if (WIFEXITED(status))           // child exited normally
                return WEXITSTATUS(status);  // return the code when child exited
            else
                return -1;
    }
    return 0;
}

string get_env(const string &key, const string &def_value) {
    char *result = getenv(key.c_str());
    return !result ? def_value : string(result);
}

void set_env(const string &key, const string &value, bool replace) {
    setenv(key.c_str(), value.c_str(), replace);
}

elapsed_time::elapsed_time() {
    start = chrono::system_clock::now();
}
