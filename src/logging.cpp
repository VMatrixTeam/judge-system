#include "logging.hpp"

#include <boost/algorithm/string.hpp>
#include <deque>
#include <filesystem>
using namespace std;

thread_local deque<string> prefix_stack;

string LOG_PREFIX(const char *file, int line, const char *function) {
    filesystem::path path(file);
    return "[" + boost::algorithm::join(prefix_stack, "-") + ":" + path.filename().string() + ":" + to_string(line) + ":" + string(function) + "] ";
}

void LOG_BEGIN(const string &prefix) {
    prefix_stack.push_back(prefix);
}

void LOG_END() {
    if (prefix_stack.empty()) {
        fprintf(stderr, "No pair LOG_BEGIN");
        abort();
    }
    prefix_stack.pop_back();
}