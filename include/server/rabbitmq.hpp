#pragma once

#include <chrono>
#include <mutex>
#include "SimpleAmqpClient/SimpleAmqpClient.h"
#include "server/config.hpp"

namespace judge::server {

class rabbitmq_channel;

struct rabbitmq_envelope {
    friend class rabbitmq_channel;
    rabbitmq_envelope();
    rabbitmq_envelope(AmqpClient::Channel::ptr_t channel, AmqpClient::Envelope::ptr_t envelope);

    void ack() const;

    std::string body() const;

private:
    AmqpClient::Channel::ptr_t channel;
    AmqpClient::Envelope::ptr_t envelope;
};

struct rabbitmq_channel {
    using envelope_type = rabbitmq_envelope;
    
    rabbitmq_channel(amqp &amqp, bool write = false);

    bool fetch(rabbitmq_envelope &envelope, int timeout = -1);
    void ack(const rabbitmq_envelope &envelope);

    /**
     * @brief 向队列发送消息，routing_key 为队列默认
     * @param message 消息内容
     */
    void report(const std::string &message);

    /**
     * @brief 向队列发送消息
     * @param message 消息内容
     * @param routing_key 该消息采用特定的 routing key
     */
    void report(const std::string &message, const std::string &routing_key);

private:
    void connect();
    void try_connect(bool force);

    AmqpClient::Channel::ptr_t channel;
    std::string tag;
    judge::server::amqp queue;
    bool write;
    std::mutex mut;
};

}  // namespace judge::server
