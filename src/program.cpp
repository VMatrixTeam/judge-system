#include "program.hpp"

#include <fmt/core.h>
#include <stdlib.h>

#include <boost/algorithm/string/join.hpp>
#include <fstream>
#include <mutex>
#include <stdexcept>

#include "common/exceptions.hpp"
#include "common/io_utils.hpp"
#include "common/utils.hpp"
#include "config.hpp"
#include "logging.hpp"
using namespace std;

namespace judge {
namespace fs = std::filesystem;

compilation_error::compilation_error(const string &what, const string &error_log) noexcept
    : judge_exception(what), error_log(error_log) {}

executable_compilation_error::executable_compilation_error(const string &what, const string &error_log) noexcept
    : compilation_error(what, error_log) {}

scoped_file_lock program::shared_lock() {
    return scoped_file_lock();
}

std::string program::get_compilation_log(const std::filesystem::path &workdir) {
    return get_compilation_details(workdir);
}

std::string program::get_compilation_details(const std::filesystem::path &workdir) {
    fs::path system_log_file(workdir / "compile" / "compile.out");
    return read_file_content(system_log_file, "No compilation information, log file does not exist: " + system_log_file.string(), judge::MAX_IO_SIZE);
}

executable::executable(const string &id, const fs::path &workdir, asset_uptr &&asset, const string &md5sum)
    : dir(workdir / "executable" / id), runpath(dir / "compile" / "run"), id(assert_safe_path(id)), md5sum(md5sum), md5path(dir / "md5sum"), deploypath(dir / ".deployed"), buildpath(dir / "compile" / "build"), asset(move(asset)) {
}

void executable::fetch(const string &cpuset, const fs::path &, const fs::path &chrootdir, const executable_manager &, program_limit limit) {
    LOG_DEBUG << "Try to fetch executable: dir = " << dir;  //debug
    if (is_dirty()) {
        // 如果直接加锁再检查会导致 fetch 过程单线程，将导致整个评测单线程
        scoped_file_lock lock = lock_directory(dir, false);
        // 加锁后必须再次检查，避免竞争
        if (is_dirty()) {
            clean_locked_directory(dir);

            asset->fetch(dir / "compile");

            if (fs::exists(buildpath)) {
                LOG_DEBUG << "Fetching executable: buildpath = " << buildpath;  //debug
                process_builder pb;
                if (limit.file_limit > 0) pb.environment("SCRIPTFILELIMIT", limit.file_limit);
                if (limit.time_limit > 0) pb.environment("SCRIPTTIMELIMIT", limit.time_limit);
                if (limit.memory_limit > 0) pb.environment("SCRIPTMEMLIMIT", limit.memory_limit);
                if (auto ret = pb.run(EXEC_DIR / "compile_executable.sh", "-n", cpuset, /* workdir */ dir, chrootdir); ret != 0) {
                    switch (ret) {
                        case E_COMPILER_ERROR:
                            BOOST_THROW_EXCEPTION(executable_compilation_error("executable compilation error", get_compilation_log(dir)));
                        default:
                            BOOST_THROW_EXCEPTION(internal_error() << "unknown exitcode " << ret);
                    }
                }
            }

            if (!fs::exists(runpath)) {
                BOOST_THROW_EXCEPTION(executable_compilation_error("executable malformed", "Executable malformed"));
            }

            fs::permissions(runpath,
                            fs::perms::group_exec | fs::perms::others_exec | fs::perms::owner_exec,
                            fs::perm_options::add);
            ofstream to_be_created(deploypath);
        }
    }
}

void executable::fetch(const string &cpuset, const fs::path &chrootdir, const executable_manager &exec_mgr) {
    fetch(cpuset, {}, chrootdir, exec_mgr);
}

scoped_file_lock executable::shared_lock() {
    return lock_directory(dir, true);
}

std::unique_ptr<executable> executable::get_compile_script(const executable_manager &) {
    return nullptr;
}

fs::path executable::get_run_path(const fs::path &) noexcept {
    return dir / "compile";
}

bool executable::is_dirty() {
    return !fs::is_directory(dir) ||
           !fs::is_regular_file(deploypath) ||
           (!md5sum.empty() && !fs::is_regular_file(md5path)) ||
           (!md5sum.empty() && read_file_content(md5path) != md5sum);
}

local_executable::local_executable(const std::string &type, const std::string &id, const std::filesystem::path &workdir, const std::filesystem::path &execdir)
    : executable(type + "-" + id, workdir, make_unique<local_executable_asset>(type, id, execdir)), localdir(execdir / assert_safe_path(type) / assert_safe_path(id)) {
}

bool local_executable::is_dirty() {
    if (!fs::is_directory(dir) ||
        !fs::is_regular_file(deploypath)) return true;

    auto current = judge::last_write_time(deploypath);
    for (auto &subitem : filesystem::recursive_directory_iterator(localdir)) {
        auto path = subitem.path();
        if (judge::last_write_time(path) > current)
            return true;
    }
    return false;
}

empty_executable::empty_executable() : executable("", {}, nullptr) {
}

void empty_executable::fetch(const std::string &, const std::filesystem::path &, const std::filesystem::path &, const executable_manager &, program_limit) {
    // 空的 executable 不需要获取
}

std::string empty_executable::get_compilation_details(const std::filesystem::path &) {
    return "";
}

std::unique_ptr<executable> empty_executable::get_compile_script(const executable_manager &) {
    return nullptr;
}

std::filesystem::path empty_executable::get_run_path(const std::filesystem::path &dir) noexcept {
    return dir;
}

scoped_file_lock empty_executable::shared_lock() {
    return scoped_file_lock();
}

local_executable_asset::local_executable_asset(const string &type, const string &id, const fs::path &execdir)
    : asset(""), execdir(execdir / assert_safe_path(type) / assert_safe_path(id)) {}

void local_executable_asset::fetch(const fs::path &dir) {
    fs::copy(/* from */ execdir, /* to */ dir, fs::copy_options::recursive);
}

remote_executable_asset::remote_executable_asset(asset_uptr &&remote_asset, const string &md5sum)
    : asset(""), md5sum(md5sum), remote_asset(move(remote_asset)) {}

void remote_executable_asset::fetch(const fs::path &dir) {
    fs::path md5path(dir / "md5sum");
    fs::path deploypath(dir / ".deployed");
    fs::path buildpath(dir / "build");
    fs::path runpath(dir / "run");
    fs::path zippath(dir / "executable.zip");

    remote_asset->fetch(zippath);

    if (!md5sum.empty()) {
        // TODO: check for md5 of executable.zip
    }

    LOG_INFO << "Unzipping executable " << zippath;

    if (system(fmt::format("unzip -Z '{}' | grep -q ^l", zippath.string()).c_str()) == 0)
        BOOST_THROW_EXCEPTION(executable_compilation_error("Executable contains symlinks", "Unable to unzip executable"));

    if (system(fmt::format("unzip -j -q -d {}, {}", dir, zippath).c_str()) != 0)
        BOOST_THROW_EXCEPTION(executable_compilation_error("Unable to unzip executable", "Unable to unzip executable"));
}

local_executable_manager::local_executable_manager(const fs::path &workdir, const fs::path &execdir)
    : workdir(workdir), execdir(execdir) {}

unique_ptr<executable> local_executable_manager::get_compile_script(const string &language) const {
    if (language.empty()) {
        return make_unique<empty_executable>();
    } else {
        return make_unique<local_executable>("compile", language, workdir, execdir);
    }
}

unique_ptr<executable> local_executable_manager::get_run_script(const string &language) const {
    if (language.empty()) {
        return make_unique<empty_executable>();
    } else {
        return make_unique<local_executable>("run", language, workdir, execdir);
    }
}

unique_ptr<executable> local_executable_manager::get_check_script(const string &language) const {
    if (language.empty()) {
        return make_unique<empty_executable>();
    } else {
        return make_unique<local_executable>("check", language, workdir, execdir);
    }
}

unique_ptr<executable> local_executable_manager::get_compare_script(const string &language) const {
    if (language.empty()) {
        return make_unique<empty_executable>();
    } else {
        return make_unique<local_executable>("compare", language, workdir, execdir);
    }
}

void source_code::fetch(const string &cpuset, const fs::path &workdir, const fs::path &chrootdir, const executable_manager &exec_mgr, program_limit limit) {
    LOG_DEBUG << "Fetch and compile source code";

    vector<string> paths;
    auto compilepath = workdir / "compile";
    auto compiledpath = compilepath / ".compiled";
    scoped_file_lock lock = lock_directory(compilepath, false);
    if (filesystem::exists(compiledpath)) return;
    fs::create_directories(compilepath);

    for (auto &file : source_files) {
        assert_safe_path(file->name);
        file->fetch(compilepath);
        paths.push_back(file->name);
    }

    for (auto &file : assist_files) {
        assert_safe_path(file->name);
        file->fetch(compilepath);
    }

    if (paths.empty()) {
        // skip program that has no source files
        return;
    }

    LOG_DEBUG << "Source code's language = " << language;
    // LOG_DEBUG << "Source code's compile command = " << compile_command;

    auto exec = exec_mgr.get_compile_script(language);
    exec->fetch(cpuset, chrootdir, exec_mgr);
    auto compile_script_lock = exec->shared_lock();

    process_builder pb;
    if (!entry_point.empty()) pb.environment("ENTRY_POINT", entry_point);
    if (limit.file_limit > 0) pb.environment("SCRIPTFILELIMIT", limit.file_limit);
    if (limit.time_limit > 0) pb.environment("SCRIPTTIMELIMIT", limit.time_limit);
    if (limit.memory_limit > 0) pb.environment("SCRIPTMEMLIMIT", limit.memory_limit);

    // compile.sh <compile script> <chrootdir> <workdir> <files> <extra compile args...>
    if (auto ret = pb.run(EXEC_DIR / "compile.sh", "-n", cpuset, /* compile script */ exec->get_run_path(), chrootdir, workdir,
                          /* source files */ boost::algorithm::join(paths, ":"), compile_command);
        ret != 0) {
        switch (ret) {
            case E_COMPILER_ERROR:
                BOOST_THROW_EXCEPTION(compilation_error("", get_compilation_log(workdir)));
            case E_INTERNAL_ERROR:
                BOOST_THROW_EXCEPTION(internal_error(get_compilation_log(workdir)));
            default:
                BOOST_THROW_EXCEPTION(internal_error(fmt::format("Unrecognized compile.sh exitcode: {}\n{}", ret, get_compilation_log(workdir))));
        }
    }

    ofstream to_be_created(compiledpath);
}

std::unique_ptr<executable> source_code::get_compile_script(const executable_manager &exec_mgr) {
    return exec_mgr.get_compile_script(language);
}

string source_code::get_compilation_log(const fs::path &workdir) {
    fs::path compilation_log_file(workdir / "compile" / "compile.tmp");
    string compilation_log = read_file_content(compilation_log_file, "", judge::MAX_IO_SIZE);
    if (!compilation_log.empty()) return compilation_log;
    return get_compilation_details(workdir);
}

fs::path source_code::get_run_path(const fs::path &path) noexcept {
    return path / "compile";
}

void git_repository::fetch(const string &cpuset, const fs::path &workdir, const fs::path &chrootdir, const executable_manager &, program_limit limit) {
    vector<string> paths;
    auto compilepath = workdir / "compile";
    auto compiledpath = compilepath / ".compiled";
    scoped_file_lock lock = lock_directory(compilepath, false);
    if (filesystem::exists(compiledpath)) return;
    fs::create_directories(compilepath);

    process_builder pb1;
    pb1.environment("GIT_USERNAME", username);
    pb1.environment("GIT_PASSWORD", password);

    // git_clone.sh <url> <commit> <workdir>
    if (auto ret = pb1.run(EXEC_DIR / "git_clone.sh", url, commit, workdir); ret != 0) {
        BOOST_THROW_EXCEPTION(compilation_error("Unable to clone git repository " + url + ", exitcode: " + to_string(ret), read_file_content(workdir / "system.out", "No detailed information", judge::MAX_IO_SIZE)));
    }

    for (auto &file : overrides) {
        assert_safe_path(file->name);
        file->fetch(compilepath);
    }

    process_builder pb2;
    if (limit.file_limit > 0) pb2.environment("SCRIPTFILELIMIT", limit.file_limit);
    if (limit.time_limit > 0) pb2.environment("SCRIPTTIMELIMIT", limit.time_limit);
    if (limit.memory_limit > 0) pb2.environment("SCRIPTMEMLIMIT", limit.memory_limit);
    // compile.sh $EXECDIR/compile/git <chrootdir> <workdir>
    if (auto ret = pb2.run(EXEC_DIR / "compile.sh", "-n", cpuset, EXEC_DIR / "compile" / "git", chrootdir, workdir, /* no source file */ ""); ret != 0) {
        switch (ret) {
            case E_COMPILER_ERROR:
                BOOST_THROW_EXCEPTION(compilation_error("", get_compilation_log(workdir)));
            case E_INTERNAL_ERROR:
                BOOST_THROW_EXCEPTION(internal_error(get_compilation_log(workdir)));
            default:
                BOOST_THROW_EXCEPTION(internal_error(fmt::format("Unrecognized compile.sh exitcode: {}\n{}", ret, get_compilation_log(workdir))));
        }
    }

    ofstream to_be_created(compiledpath);
}

std::unique_ptr<executable> git_repository::get_compile_script(const executable_manager &) {
    return nullptr;
}

string git_repository::get_compilation_log(const fs::path &workdir) {
    fs::path compilation_log_file(workdir / "compile" / "compile.tmp");
    string compilation_log = read_file_content(compilation_log_file, "", judge::MAX_IO_SIZE);
    if (!compilation_log.empty()) return compilation_log;
    return get_compilation_details(workdir);
}

fs::path git_repository::get_run_path(const fs::path &path) noexcept {
    return path / "compile";
}

}  // namespace judge
