#pragma once

#include <filesystem>
#include "common/json_utils.hpp"

namespace judge::net {

/**
 * @brief 发送 post 请求
 * @param url post 请求地址
 * @param post post 请求体,必须是 json
 * @param file_connect_timeout 最大限制请求时间，单位为秒
 * @return post 请求响应内容,必须是 json
 */
std::string post_json(const std::string &url, const nlohmann::json &post, const double file_connect_timeout = 1.0);

/**
 * @brief 发送 GET 请求
 * @param url GET 请求地址
 * @param params GET 请求参数
 * @param file_connect_timeout 最大限制请求时间，单位为秒
 * @return GET 请求响应内容
 */
std::string get(const std::string &url, const std::map<std::string, std::string> &params, const double file_connect_timeout = 1.0);

/**
 * @brief 发送 POST 请求上传文件
 * @param url POST 请求地址
 * @param content 文件内容
 * @param file_connect_timeout 最大限制请求时间，单位为秒
 * @return POST 响应内容
 */
void upload_file_string(const std::string &url, const std::string &content, const double file_connect_timeout = 1.0);

/**
 * @brief 将文件从 url 下载到本地路径 path
 * @param url 要下载的文件的网络地址
 * @param path 下载文件的保存路径
 * @param file_connect_timeout 最大限制请求时间，单位为秒
 */
void download_file(const std::string &url, const std::filesystem::path &path, const double file_connect_timeout = 1.0);

/**
 * @brief 将文件上传到 url
 * @param url 要上传到的网络地址
 * @param path 要上传的文件路径
 * @param file_connect_timeout 最大限制请求时间，单位为秒
 */
void upload_file(const std::string &url, const std::filesystem::path &path, const double file_connect_timeout = 1.0);

}  // namespace judge::net
