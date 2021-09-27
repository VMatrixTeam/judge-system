#include "asset.hpp"
#include "common/net_utils.hpp"
#include <fstream>
#include "common/exceptions.hpp"

namespace judge {
using namespace std;

asset::asset(const string &name) : name(name) {}

local_asset::local_asset(const string &name, const filesystem::path &path)
    : asset(name), path(path) {}

void local_asset::fetch(const filesystem::path &path) {
    filesystem::copy(this->path, path / name);
}

text_asset::text_asset(const string &name, const string &text)
    : asset(name), text(text) {}

void text_asset::fetch(const filesystem::path &path) {
    ofstream fout(path / name);
    fout << text;
}

remote_asset::remote_asset(const string &name, const string &url_get)
    : asset(name), url(url_get) {}

// TODO: 针对 CURLcode throw 更加精确的 exception
void remote_asset::fetch(const filesystem::path &path) {
    net::download_file(url, path / name);
}

}  // namespace judge
