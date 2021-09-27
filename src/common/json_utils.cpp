#include "common/json_utils.hpp"
#include "common/io_utils.hpp"

namespace nlohmann {
using namespace std;

string ensure_utf8(const string &str) {
    return judge::utf8_check_is_valid(str) ? str : "Not UTF-8 encoded";
}

}  // namespace nlohmann
