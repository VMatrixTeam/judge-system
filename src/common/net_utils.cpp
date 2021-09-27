#include "common/net_utils.hpp"

#include <cpr/cpr.h>
#include <sys/stat.h>

#include "common/exceptions.hpp"

namespace judge::net {
using namespace std;
using namespace nlohmann;

string get(const string &url, const map<string, string> &params, const double file_connect_timeout) {
    cpr::Parameters cprParams;
    for (auto const &[key, value] : params) {
        cprParams.Add({key, value});
    }
    cpr::Response resp = cpr::Get(
        cpr::Url{url},
        cprParams,
        cpr::Timeout{static_cast<int>(file_connect_timeout * 1000)});

    if (resp.status_code == 0) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to download " << resp.url << ", error=" << resp.error.message);
    }

    if (resp.status_code >= 400) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to download " << resp.url << ", status code=" << resp.status_code);
    }

    return resp.text;
}

string post_json(const string &url, const json &post, const double file_connect_timeout) {
    cpr::Response resp = cpr::Post(
        cpr::Url{url},
        cpr::Body{post.dump()},
        cpr::Header{
            {"Content-Type", "application/json"},
            {"Charset", "UTF-8"},
        },
        cpr::Timeout{static_cast<int>(file_connect_timeout * 1000)});

    if (resp.status_code == 0) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to post json to " << resp.url << ", error=" << resp.error.message);
    }

    if (resp.status_code >= 400) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to post json to " << resp.url << ", status code=" << resp.status_code);
    }

    return resp.text;
}

void upload_file_string(const string &url, const string &content, const double file_connect_timeout) {
    cpr::Response resp = cpr::Post(
        cpr::Url{url},
        cpr::Multipart{{"key", content}}, // fix: use a Multipart upload
	    cpr::Timeout{static_cast<int>(file_connect_timeout * 1000)}); // fix: use file_connect_timeout

    if (resp.status_code == 0) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to post file to " << resp.url << ", error=" << resp.error.message);
    }

    if (resp.status_code >= 400) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to post file to " << resp.url << ", status code=" << resp.status_code);
    }
}

void download_file(const string &url, const filesystem::path &path, const double file_connect_timeout) {
    filesystem::create_directories(path.parent_path());
    std::ofstream destination(path, ios::binary);
    cpr::Response resp = cpr::Download(
        destination,
        cpr::Url{url},
        cpr::Timeout{static_cast<int>(file_connect_timeout * 1000)});

    if (resp.status_code == 0) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to download file from " << resp.url << ", error=" << resp.error.message);
    }

    if (resp.status_code >= 400) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to download file from " << resp.url << ", status code=" << resp.status_code);
    }
}

void upload_file(const string &url, const filesystem::path &path, const double file_connect_timeout) {
    cpr::Response resp = cpr::Post(cpr::Url{url}, 
                                   cpr::Multipart{{"file", cpr::File{path}}},
                                   cpr::Timeout{static_cast<int>(file_connect_timeout * 1000)});

    if (resp.status_code == 0) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to post file to " << resp.url << ", error=" << resp.error.message);
    }

    if (resp.status_code >= 400) {
        BOOST_THROW_EXCEPTION(network_error() << "unable to post file to " << resp.url << ", status code=" << resp.status_code);
    }
}

}  // namespace judge::net
