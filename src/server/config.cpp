#include "server/config.hpp"

namespace judge::server {
using namespace std;
using namespace nlohmann;

void from_json(const json &j, login &log) {
    j.at("username").get_to(log.username);
    j.at("password").get_to(log.password);
}

void from_json(const json &j, amqp &mq) {
    j.at("exchange").get_to(mq.exchange);
    if (exists(j, "exchange_type"))
        j.at("exchange_type").get_to(mq.exchange_type);
    else
        mq.exchange_type = "direct";
    j.at("uri").get_to(mq.uri);
    j.at("queue").get_to(mq.queue);
    if (exists(j, "routing_key"))
        j.at("routing_key").get_to(mq.routing_key);
    else
        mq.routing_key = "";
    if (exists(j, "concurrency"))
        j.at("concurrency").get_to(mq.concurrency);
    else
        mq.concurrency = 4;
}

void from_json(const json &j, database &db) {
    j.at("host").get_to(db.host);
    if (exists(j, "port"))
        j.at("port").get_to(db.port);
    j.at("username").get_to(db.username);
    j.at("password").get_to(db.password);
    j.at("database").get_to(db.database);
}

void from_json(const json &j, system_config &config) {
    j.at("timeLimit").get_to(config.time_limit);
    j.at("fileApi").get_to(config.file_api);
    j.at("postRetryTime").get_to(config.post_retry_time);
    j.at("fileConnectTimeout").get_to(config.file_connect_timeout);
}

void from_json(const json &j, time_limit_config &limit) {
    j.at("cCompile").get_to(limit.compile_time_limit);
    j.at("crun").get_to(limit.crun);
    j.at("oclint").get_to(limit.oclint);
    j.at("randomGenerate").get_to(limit.random_generator);
    j.at("valgrind").get_to(limit.valgrind);
}

}  // namespace judge::server
