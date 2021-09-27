#pragma once

#include <vector>

#include "asset.hpp"
#include "common/exceptions.hpp"
#include "common/io_utils.hpp"

/**
 * 这个头文件包含表示可以编译的可执行文件的类
 * 我们有两种可执行文件：
 * 1. executable: 是用来提供给评测系统使用的外置程序，用 zip 打包。
 *                比如比较脚本、运行脚本、测试脚本和编译脚本，
 *                这些脚本一般通过配置系统进行维护，是内部使用的。
 *                评测系统会允许 executable 获得 root 的执行权限，因此 executable 比较敏感。
 * 2. source_code：是表示外部导入进来的程序，比如选手的代码、
 *                 Matrix 的随机数据生成器、评测标准程序、比较程序。
 * 区别：
 * 1. executable 比较灵活，通过提供 build 脚本编译运行，不需要明确编程语言。
 * source_code 需要提供语言，在选择 Makefile 语言之后其实也可以表示成 executable。
 * 2. executable 一般不在保护模式下运行，且有 root 权限；而 source code 在保护模式下运行。
 */

namespace judge {

/**
 * @brief 表示 program 编译错误
 * 可以表示 program 格式不正确，或者编译错误
 */
struct compilation_error : public judge_exception {
public:
    const std::string error_log;

    explicit compilation_error(const std::string &what, const std::string &error_log) noexcept;
};

/**
 * @brief 表示 executable 编译错误，表示标准程序不合法
 */
struct executable_compilation_error : public compilation_error {
public:
    const std::string error_log;

    explicit executable_compilation_error(const std::string &what, const std::string &error_log) noexcept;
};

struct program_limit {
    /**
     * @brief 内存限制，限制应用程序实际最多能申请多少内存空间
     * @note 单位为 KB，小于 0 的数表示不限制内存空间申请
     */
    int memory_limit = -1;

    /**
     * @brief 时间限制，限制应用程序的实际运行时间
     * @note 单位为秒
     */
    double time_limit = -1;

    /**
     * @brief 文件输出限制，限制应用程序实际最多能写入磁盘多少数据
     * @note 单位为 KB，小于 0 的数表示不限制内存空间申请
     */
    int file_limit = -1;

    /**
     * @brief 进程数限制，限制应用程序实际最多能打开多少个进程
     * @note ，小于 0 的数表示不限制进程数
     */
    int proc_limit = -1;
};

struct executable;

/**
 * @brief 外置脚本的全局管理器
 * 本类将允许调用者访问全局配置的所有外置脚本，比如允许访问基础的编译脚本、运行脚本、检查脚本、比较脚本。
 */
struct executable_manager {
    /**
     * @brief 根据语言获取编译脚本
     * @param language 编程语言，必须和该编译脚本的外置脚本名一致
     * @return 尚未完成下载、编译的编译脚本信息 executable 类，需要手动调用 fetch 来进行下载
     */
    virtual std::unique_ptr<executable> get_compile_script(const std::string &language) const = 0;

    /**
     * @brief 根据语言获取运行脚本
     * @param name 运行脚本名，必须和该编译脚本的外置脚本名一致
     * @return 尚未完成下载、编译的编译脚本信息 executable 类，需要手动调用 fetch 来进行下载
     */
    virtual std::unique_ptr<executable> get_run_script(const std::string &name) const = 0;

    /**
     * @brief 根据名称获取测试脚本
     * @param name 测试脚本名
     * @return 尚未完成下载、编译的编译脚本信息 executable 类，需要手动调用 fetch 来进行下载
     */
    virtual std::unique_ptr<executable> get_check_script(const std::string &name) const = 0;

    /**
     * @brief 根据语言获取比较脚本
     * @param name 比较脚本名，可能的名称有 diff-ign-space，diff-all
     * @return 尚未完成下载、编译的编译脚本信息 executable 类，需要手动调用 fetch 来进行下载
     */
    virtual std::unique_ptr<executable> get_compare_script(const std::string &name) const = 0;

    virtual ~executable_manager() = default;
};

/**
 * @brief 表示一个程序
 * 这个类负责下载代码、编译、生成可执行文件
 */
struct program {
    /**
     * @brief 下载并编译程序
     * @param cpuset 编译器所能使用的 CPU 核心
     * @param workdir 提交的评测工作路径，源代码将下载到这个文件夹中
     * @param chrootdir 配置好的 Linux 子系统环境
     */
    virtual void fetch(const std::string &cpuset, const std::filesystem::path &workdir, const std::filesystem::path &chrootdir, const executable_manager &exec_mgr, program_limit limit = program_limit()) = 0;

    /**
     * @brief 获取程序的编译错误信息
     * @param workdir 提交的评测工作路径
     * @return 获得的编译信息
     */
    virtual std::string get_compilation_log(const std::filesystem::path &workdir);

    /**
     * @brief 获取程序的编译日志
     * @param workdir 提交的评测工作路径
     */
    virtual std::string get_compilation_details(const std::filesystem::path &workdir);

    /**
     * @brief 获取程序的编译脚本
     * 编译脚本将传递给 check script 和 random_generator.sh
     * 若无法决定编译脚本，返回空
     */
    virtual std::unique_ptr<executable> get_compile_script(const executable_manager &exec_mgr) = 0;

    /**
     * @brief 获得程序的可执行文件路径
     * @param path 评测的工作路径
     * @return 可执行文件路径，有些程序可能必须要使用参数，你可以通过一个不需要参数的 bash 脚本来运行这个程序
     */
    virtual std::filesystem::path get_run_path(const std::filesystem::path &path) noexcept = 0;

    virtual scoped_file_lock shared_lock();

    virtual ~program() = default;
};

/**
 * @brief 表示一个打包的外部程序
 * 每个 executable 的源代码根目录必须包含一个 run 程序，届时调用该 executable
 * 就是调用这个 ./run 程序。如果源代码需要编译，你可以在根目录放一个 ./build 脚本
 * 来执行编译命令，编译完后确保能生成 ./run 程序，或者自行编写一个 ./run bash
 * 脚本来执行你的程序。
 */
struct executable : program {
    std::filesystem::path dir, runpath;
    std::string id, md5sum;

    /**
     * @brief executable 的构造函数
     * @param id executable 的 id，用来索引（比如下载时需要 executable 的 name）
     * @param workdir 统一存放所有 executable 的位置，你只需要给定一个全局的常量即可
     * @param asset 这个 executable 的获取方式，可以是本地复制，或者远程下载
     * @param md5sum 如果 executable 是远程的，那么我们需要比对哈希值来验证下载下来的 zip 压缩包是否无损
     */
    executable(const std::string &id, const std::filesystem::path &workdir, asset_uptr &&asset, const std::string &md5sum = "");

    /**
     * @brief 获取并编译 executable
     * executable 有自己的文件存放路径，因此不使用 fetch 传入的 path
     */
    virtual void fetch(const std::string &cpuset, const std::filesystem::path &dir, const std::filesystem::path &chrootdir, const executable_manager &exec_mgr, program_limit limit = program_limit()) override;

    /**
     * @brief 获取并编译 executable
     */
    void fetch(const std::string &cpuset, const std::filesystem::path &chrootdir, const executable_manager &exec_mgr);

    virtual std::unique_ptr<executable> get_compile_script(const executable_manager &exec_mgr) override;

    virtual scoped_file_lock shared_lock() override;

    /**
     * @brief 获得 executable 的可执行文件路径
     * executable 有自己的文件存放路径，因此不使用传入的 path 来计算可执行文件路径
     */
    virtual std::filesystem::path get_run_path(const std::filesystem::path & = std::filesystem::path()) noexcept override;

protected:
    std::filesystem::path md5path, deploypath, buildpath;
    asset_uptr asset;

    /**
     * @brief 判断 executable 是否需要重新获取
     */
    virtual bool is_dirty();
};

/**
 * @brief 表示一个从本地获取的外部程序，这个程序一般指 EXEC_DIR 下的本地脚本
 */
struct local_executable : public executable {
    std::filesystem::path localdir;

    /**
     * @brief 构造函数
     * @param type 该外部程序的类型，用于在 EXEC_DIR 下索引，比如 "check", "compare", "compile", "run"
     * @param id 该外部程序的名称，用于在 EXEC_DIR 下索引
     * @param workdir 统一存放所有 executable 的位置，你只需要给定一个全局的常量即可
     * @param execdir 存放本地的外部程序的文件夹，文件夹格式参考 README.md，一般指源代码目录下的 exec 文件夹
     */
    local_executable(const std::string &type, const std::string &id, const std::filesystem::path &workdir, const std::filesystem::path &execdir);

protected:
    /**
     * @brief 判断 executable 是否需要重新获取
     * local_executable 在每次调用 fetch 的时候都会遍历检查源文件的最后更新时间
     * 如果发生变更，则执行重新获取
     */
    bool is_dirty() override;
};

/**
 * @brief 表示一个空的外部程序
 */
struct empty_executable : public executable {
    empty_executable();
    void fetch(const std::string &cpuset, const std::filesystem::path &dir, const std::filesystem::path &chrootdir, const executable_manager &exec_mgr, program_limit limit = program_limit()) override;
    std::string get_compilation_details(const std::filesystem::path &workdir) override;
    std::unique_ptr<executable> get_compile_script(const executable_manager &exec_mgr) override;
    std::filesystem::path get_run_path(const std::filesystem::path & = std::filesystem::path()) noexcept override;
    scoped_file_lock shared_lock() override;
};

/**
 * @brief 表示 executable 的本地复制方式
 * 本地复制方式支持从 execdir 文件夹内获得已经配置好的 executables
 */
struct local_executable_asset : public asset {
    std::filesystem::path dir, execdir;

    local_executable_asset(const std::string &type, const std::string &id, const std::filesystem::path &execdir);

    void fetch(const std::filesystem::path &dir) override;
};

/**
 * @brief 表示 executable 的远程下载方式
 * 远程下载方式从远程服务器下载 executable 的 zip 压缩包
 * （通过 remote_asset 来进行下载），之后会验证压缩包正确。
 * 压缩包的 md5 哈希值来确保压缩包正确，并予以解压
 */
struct remote_executable_asset : public asset {
    std::string md5sum;
    asset_uptr remote_asset;

    remote_executable_asset(asset_uptr &&remote_asset, const std::string &md5sum);

    void fetch(const std::filesystem::path &dir) override;
};

/**
 * @brief 外置脚本的全局管理器，其中外置脚本直接从本地预设中查找.
 */
struct local_executable_manager : public executable_manager {
    /**
     * @brief 构造函数
     * @param workdir 存放 executable 的临时文件夹，用于存放编译后的程序
     * @param execdir 存放 executable 的本地预设文件夹
     */
    local_executable_manager(const std::filesystem::path &workdir, const std::filesystem::path &execdir);

    std::unique_ptr<executable> get_compile_script(const std::string &language) const override;
    std::unique_ptr<executable> get_run_script(const std::string &name) const override;
    std::unique_ptr<executable> get_check_script(const std::string &name) const override;
    std::unique_ptr<executable> get_compare_script(const std::string &name) const override;

private:
    // 存放 executable 的临时文件夹，用于存放编译后的程序
    std::filesystem::path workdir;
    // 存放 executable 的本地预设文件夹
    std::filesystem::path execdir;
};

struct submission_program : program {
    /**
     * @brief 该程序的所有源文件的文件系统路径
     * @note 不可为空
     * @code{.json}
     * ["somedir/framework.cpp", "somedir/source.cpp"]
     * @endcode
     */
    std::vector<asset_uptr> source_files;

    /**
     * @brief 该程序的所有不参与编译（比如 c/c++ 的头文件）的文件的文件系统路径
     * @note 由于 gcc/g++ 在编译时会自行判断传入的源文件是不是头文件，因此没有
     * 必要专门将头文件放在这里，这个变量仅在文件必须不能传给编译器时使用。（对于
     * sicily 的 framework 评测，两个源文件为 framework.cpp, source.cpp，但是
     * source.cpp 是由 framework.cpp include 进来参与编译，如果将 source.cpp
     * 加入到编译器输入中将会产生链接错误）
     * @code{.json}
     * ["somedir/source.cpp"]
     * @endcode
     */
    std::vector<asset_uptr> assist_files;

    /**
     * @brief 该程序的额外编译命令
     * @note 这个参数将传送给 compile script，请参见这些脚本以获得更多信息
     * 
     * 示例（对于 gcc）:
     * -g -lcgroup -Wno-long-long -nostdinc -nostdinc++
     */
    std::vector<std::string> compile_command;
};

/**
 * @class source_code
 * @brief 表示一般提交的代码，包含程序文件（及获取方式）、编译方法
 */
struct source_code : submission_program {
    /**
     * @brief 该程序使用的语言
     * @note 不能为空，这涉及判题时使用的编译器与执行器
     * 因此对于 source_code，其会自行查找 get_run_script 和 get_compile_script
     * @note ["bash", "c", "cpp", "go", "haskell", "java", "make", "pas", "python2", "python3", "rust", "swift"]
     */
    std::string language;

    /**
     * @brief 应用程序入口
     * 对于 Java，entry_point 为应用程序主类。若空，默认为 Main
     * 对于 Python，entry_point 为默认的 python 脚本。若空，为 source_files 的第一个文件
     */
    std::string entry_point;

    void fetch(const std::string &cpuset, const std::filesystem::path &dir, const std::filesystem::path &chrootdir, const executable_manager &exec_mgr, program_limit limit = program_limit()) override;
    std::string get_compilation_log(const std::filesystem::path &workdir) override;
    std::unique_ptr<executable> get_compile_script(const executable_manager &exec_mgr) override;
    std::filesystem::path get_run_path(const std::filesystem::path &path) noexcept override;
};

/**
 * @brief 表示从 Git 仓库获取代码的程序
 * 通过命令 "git clone --depth=1 <url> --branch=<branch> ." 的方式下载源代码
 * 如果仓库根目录下有 build.sh，则执行 build.sh 编译；否则检查是否存在 makefile/Makefile，并执行 make。
 * 
 * 对于 git_repository，source_files 和 assist_files 仅提供给静态测试使用
 * compile_command 将以环境变量的方式传递给 make 命令
 */
struct git_repository : submission_program {
    /**
     * @brief Git 仓库的访问地址
     * 如：https://username:password@github.com/huanghongxun/Judger
     * 服务端可以在 url 中附上用户名和密码，这样评测就不需要知道
     */
    std::string url;

    /**
     * @brief clone 的 commit id，如果本项存在，则忽略 branch
     */
    std::string commit;

    /**
     * @brief 仓库用户，仅在 http/https 协议下使用
     */
    std::string username;

    /**
     * @brief 仓库用户密码，仅在 http/https 协议下使用
     */
    std::string password;

    /**
     * @brief 覆盖文件
     * 学生可能会修改 Git 仓库内部分不应该被修改的文件，此时通过文件覆盖恢复文件
     * 或者提供对学生不可见的隐藏文件。
     */
    std::vector<asset_uptr> overrides;

    void fetch(const std::string &cpuset, const std::filesystem::path &dir, const std::filesystem::path &chrootdir, const executable_manager &exec_mgr, program_limit limit = program_limit()) override;
    std::string get_compilation_log(const std::filesystem::path &workdir) override;
    std::unique_ptr<executable> get_compile_script(const executable_manager &exec_mgr) override;
    std::filesystem::path get_run_path(const std::filesystem::path &path) noexcept override;
};

}  // namespace judge
