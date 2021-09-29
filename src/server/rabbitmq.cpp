#include "server/rabbitmq.hpp"

#include <sys/prctl.h>

#include <future>
#include <thread>

#include "logging.hpp"

namespace judge::server {
using namespace std;

rabbitmq_channel::rabbitmq_channel(amqp &amqp, bool write) : queue(amqp), write(write) {
    connect();
    if (write) {
        std::thread message_write_thread([this]() {
            prctl(PR_SET_NAME, "mq write loop", 0, 0, 0);
            this->message_write_loop();
        });
        message_write_thread.detach();
    }
}

rabbitmq_channel::~rabbitmq_channel() {
    server_shutdown = true;
}

void rabbitmq_channel::connect() {
    channel = AmqpClient::Channel::Open(AmqpClient::Channel::OpenOpts::FromUri(queue.uri));
    channel->DeclareQueue(queue.queue, /* passive */ false, /* durable */ true, /* exclusive */ false, /* auto_delete */ false);
    channel->DeclareExchange(queue.exchange, queue.exchange_type, /* passive */ false, /* durable */ true);
    channel->BindQueue(queue.queue, queue.exchange, queue.routing_key);
    if (!write) {  // 对于从消息队列读取消息的情况，我们需要监听队列
        channel->BasicConsume(queue.queue, /* consumer tag */ "", /* no_local */ true, /* no_ack */ false, /* exclusive */ false, queue.concurrency);
    }
}

void rabbitmq_channel::try_connect(bool force) {
    try {
        connect();
    } catch (const std::exception &ex) {
        LOG_ERROR << "Failed to connect, cause: " << ex.what();
        if (force) throw;
    }
}

bool rabbitmq_channel::fetch(rabbitmq_envelope &envelope, int timeout) {
    for (int retry = 0; retry < 5; retry++) {
        try {
            envelope.channel = channel;
            return channel->BasicConsumeMessage(envelope.envelope, timeout);
        } catch (...) {
            LOG_DEBUG << "Fetching message failed, retry: " << retry;
            if (retry >= 4) throw;
            std::this_thread::sleep_for(std::chrono::milliseconds(1000));
            try_connect(false);
        }
    }

    // Unreachable
    return false;
}

void rabbitmq_channel::message_write_loop() {
    LOG_DEBUG << "Start message write loop for exchange: " << queue.exchange;
    pending_message message;
    while (!server_shutdown) {
        if (!write_queue.try_pop(message)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        };
        AmqpClient::BasicMessage::ptr_t msg = AmqpClient::BasicMessage::Create(message.message);
        for (int retry = 0;; retry++) {
            try {
                LOG_DEBUG << "report: sending message to exchange:" << queue.exchange
                          << ", routing_key=" << queue.routing_key;
                channel->BasicPublish(queue.exchange, queue.routing_key, msg);
                break;
            } catch (const std::exception &ex) {
                LOG_DEBUG << "Sending message failed, retry: " << retry << ", cause: " << ex.what();
                std::this_thread::sleep_for(std::chrono::milliseconds(1000));
                try_connect(false);
            }
        }
    }
    LOG_DEBUG << "server shutdown, exiting message write loop";
}

void rabbitmq_channel::report(const string &message) {
    report(message, queue.routing_key);
}

void rabbitmq_channel::report(const string &message, const string &routing_key) {
    write_queue.push({message, queue.routing_key});
}

rabbitmq_envelope::rabbitmq_envelope() {}

rabbitmq_envelope::rabbitmq_envelope(AmqpClient::Channel::ptr_t channel, AmqpClient::Envelope::ptr_t envelope)
    : channel(channel), envelope(envelope) {}

void rabbitmq_envelope::ack() const {
    if (channel && envelope)
        channel->BasicAck(envelope);
}

string rabbitmq_envelope::body() const {
    return envelope->Message()->Body();
}

}  // namespace judge::server
