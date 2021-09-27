#pragma once

#include <iostream>
#include <limits>
#include <string>
#include <vector>
using std::cout;
using std::endl;

struct time_limit {
    double soft, hard;
};

struct runguard_options {
    std::string cgroupname;
    std::string chroot_dir;
    std::string work_dir;
    size_t nproc = std::numeric_limits<size_t>::max();
    std::string preexecute;
    std::string user;
    std::string group;
    int user_id = -1;
    int group_id = -1;
    std::string netns;   // network namespace name created by "ip netns add"
    std::string cpuset;  // processor id to run client program.

    bool use_wall_limit = false;
    struct time_limit wall_limit;  // wall clock time
    bool use_cpu_limit = false;
    struct time_limit cpu_limit;  // CPU time

    int64_t memory_limit = -1;  // Memory limit in bytes
    int file_limit = -1;        // Output limit
    bool no_core_dumps = false;

    /**
     * Allowed syscall numbers
     */
    std::vector<int> syscalls;

    std::string stdin_filename;
    std::string stdout_filename;
    std::string stderr_filename;

    bool preserve_sys_env = false;
    std::vector<std::string> env;

    std::string metafile_path;
    std::vector<std::string> command;

    friend std::ostream& operator<<(std::ostream& out, runguard_options& opt);  // for debug
};
