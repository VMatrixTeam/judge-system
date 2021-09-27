#include "common/io_utils.hpp"
#include "logging.hpp"
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <utime.h>
#include <algorithm>
#include <fstream>
#include "common/exceptions.hpp"

namespace judge {
using namespace std;
namespace fs = std::filesystem;

string read_file_content(filesystem::path const &path, long max_bytes) {
    ifstream fin(path.string(), ios::in | ios::binary | ios::ate);

    auto file_size = fin.tellg();
    fin.seekg(0, ios::beg);

    auto read_size = max_bytes > 0 ? min(file_size, ifstream::pos_type(max_bytes)) : file_size;

    string result;
    result.resize(read_size);
    fin.read(&result[0], read_size);

    if (read_size < file_size) result += "<...truncated>";
    return result;
}

string read_file_content(filesystem::path const &path, const string &def, long max_bytes) {
    if (!filesystem::exists(path)) {
        return def;
    } else {
        return read_file_content(path, max_bytes);
    }
}

bool utf8_check_is_valid(const string &string) {
    int c, i, ix, n, j;
    for (i = 0, ix = string.length(); i < ix; i++) {
        c = (unsigned char)string[i];
        //if (c==0x09 || c==0x0a || c==0x0d || (0x20 <= c && c <= 0x7e) ) n = 0; // is_printable_ascii
        if (0x00 <= c && c <= 0x7f)
            n = 0;  // 0bbbbbbb
        else if ((c & 0xE0) == 0xC0)
            n = 1;  // 110bbbbb
        else if (c == 0xed && i < (ix - 1) && ((unsigned char)string[i + 1] & 0xa0) == 0xa0)
            return false;  //U+d800 to U+dfff
        else if ((c & 0xF0) == 0xE0)
            n = 2;  // 1110bbbb
        else if ((c & 0xF8) == 0xF0)
            n = 3;  // 11110bbb
        //else if (($c & 0xFC) == 0xF8) n=4; // 111110bb //byte 5, unnecessary in 4 byte UTF-8
        //else if (($c & 0xFE) == 0xFC) n=5; // 1111110b //byte 6, unnecessary in 4 byte UTF-8
        else
            return false;
        for (j = 0; j < n && i < ix; j++) {  // n bytes matching 10bbbbbb follow ?
            if ((++i == ix) || (((unsigned char)string[i] & 0xC0) != 0x80))
                return false;
        }
    }
    return true;
}

string assert_safe_path(const string &subpath) {
    if (subpath.find("../") != string::npos)
        BOOST_THROW_EXCEPTION(judge_exception() << "subpath is not safe " << subpath);
    return subpath;
}

int count_directories_in_directory(const fs::path &dir) {
    if (!fs::is_directory(dir))
        return -1;
    return count_if(fs::directory_iterator(dir), {}, (bool (*)(const fs::path &))fs::is_directory);
}

scoped_file_lock::scoped_file_lock() {
    valid = false;
}

scoped_file_lock::scoped_file_lock(const fs::path &path, bool shared) : lock_file(path) {
    LOG_DEBUG << "Locking " << path << " share: " << shared;
    fd = open(path.c_str(), O_CREAT | O_RDWR);
    flock(fd, shared ? LOCK_SH : LOCK_EX);
    valid = true;
}

scoped_file_lock::scoped_file_lock(scoped_file_lock &&lock) {
    *this = move(lock);
}

scoped_file_lock::~scoped_file_lock() {
    release();
}

scoped_file_lock &scoped_file_lock::operator=(scoped_file_lock &&lock) {
    swap(fd, lock.fd);
    swap(lock_file, lock.lock_file);
    swap(valid, lock.valid);
    return *this;
}

std::filesystem::path scoped_file_lock::file() const {
    return lock_file;
}

void scoped_file_lock::release() {
    if (!valid) return;
    LOG_DEBUG << "Unlocking " << lock_file;
    flock(fd, LOCK_UN);
    close(fd);
    valid = false;
}

scoped_file_lock lock_directory(const fs::path &dir, bool shared) {
    fs::create_directories(dir);
    fs::path lock_file = dir / ".lock";
    if (fs::is_directory(lock_file))
        fs::remove_all(lock_file);
    scoped_file_lock lock(lock_file, shared);
    // Unix 允许删除上锁的文件，因此获得文件夹锁后必须重新创建文件夹
    fs::create_directories(dir);
    return lock;
}

void clean_locked_directory(const std::filesystem::path &dir) {
    for (auto &subitem : filesystem::directory_iterator(dir)) {
        if (subitem.path().filename().string() == ".lock") continue;
        try {
            filesystem::remove_all(subitem.path());
        } catch (std::exception &e) {
            LOG_ERROR << "Unable to delete file " << subitem.path() << " " << e.what();
        }
    }
}

time_t last_write_time(const fs::path &path) {
    struct stat attr;
    if (stat(path.c_str(), &attr) != 0)
        BOOST_THROW_EXCEPTION(system_error(errno, system_category(), "error when reading modification time of path " + path.string()));
    return attr.st_mtim.tv_sec;
}

void last_write_time(const fs::path &path, time_t timestamp) {
    struct utimbuf buf;
    struct stat attr;
    stat(path.c_str(), &attr);
    buf.actime = chrono::system_clock::to_time_t(chrono::system_clock::now());
    buf.modtime = timestamp;
    if (utime(path.c_str(), &buf) == -1)
        BOOST_THROW_EXCEPTION(system_error(errno, system_category(), "error when setting modification time of path " + path.string()));
}

}  // namespace judge
