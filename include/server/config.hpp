#pragma once

#include "common/json_utils.hpp"

namespace judge::server {

/**
 * @brief 表示一个需要登录的服务配置
 */
struct login {
    /**
     * @brief 某个服务的访问用户名
     */
    std::string username;

    /**
     * @brief 某个服务的访问密码
     */
    std::string password;
};

void from_json(const nlohmann::json &j, login &mq);

/**
 * @brief 描述一个 AMQP 消息队列的配置数据结构
 */
struct amqp {
    /**
     * @brief AMQP 消息队列的地址
     */
    std::string uri;

    /**
     * @brief 通过该结构体发送的消息的 Exchange 名
     */
    std::string exchange;

    /**
     * @brief Exchange 类型，可选 direct, topic, fanout
     */
    std::string exchange_type;

    /**
     * @brief AMQP 消息队列的队列名
     */
    std::string queue;

    /**
     * @brief AMQP 消息队列的 Routing Key
     */
    std::string routing_key;

    /**
     * @brief AMQP 消息队列包含几个消费者
     * 有多少个消费者，评测就可以同时评测最多相应个提交
     * 默认为 4
     */
    int concurrency;
};

void from_json(const nlohmann::json &j, amqp &mq);

/**
 * @brief 描述一个 MySQL 数据库连接信息
 */
struct database : login {
    /**
     * @brief 数据库服务器的地址
     */
    std::string host;

    /**
     * @brief 数据库服务器端口
     */
    unsigned int port = 0;

    /**
     * @brief 使用连接到的数据库服务器的哪一个数据库
     */
    std::string database;
};

void from_json(const nlohmann::json &j, database &db);

/**
 * @brief 时间限制配置
 */
struct time_limit_config {
    /**
     * @brief 编译时间限制（单位为秒）
     */
    double compile_time_limit;

    /**
     * @brief 静态检查时限（单位为秒）
     */
    double oclint;

    /**
     * @brief 
     */
    double crun;

    /**
     * @brief 内存检查时限（单位为秒）
     */
    double valgrind;

    /**
     * @brief 随机数据生成器运行时限（单位为秒）
     */
    double random_generator;
};

void from_json(const nlohmann::json &j, time_limit_config &limit);

struct system_config {
    time_limit_config time_limit;

    /**
     * @brief 文件系统根路径
     */
    std::string file_api;

    /**
     * @brief 评测报告写入文件系统失败的重试次数
     */
    int post_retry_time;

    /**
     * @brief 文件系统连接时限（单位为秒）
     * 如果时限内没有连接上文件系统，则取消请求
     */
    double file_connect_timeout;
};

void from_json(const nlohmann::json &j, system_config &config);

}  // namespace judge::server
