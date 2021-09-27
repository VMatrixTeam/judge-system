#include "env.hpp"
#include "gtest/gtest.h"
#include "judge/programming.hpp"
#include "server/rabbitmq.hpp"

using namespace std;
using namespace std::filesystem;
using namespace judge;

class DISABLED_RabbitMQTest : public ::testing::Test {
};

TEST_F(DISABLED_RabbitMQTest, MultiplePrefetchTest) {
    judge::server::amqp config = {
        .uri = "amqp://localhost:25565",
        .exchange = "ProgrammingSubmission",
        .exchange_type = "direct",
        .queue = "ProgrammingSubmission",
        .routing_key = "ProgrammingSubmission",
        .concurrency = 2
    };
    judge::server::rabbitmq_channel mq(config);
    judge::server::rabbitmq_channel::envelope_type e1, e2;
    EXPECT_TRUE(mq.fetch(e1, 0));
    EXPECT_TRUE(mq.fetch(e2, 0));
}

TEST_F(DISABLED_RabbitMQTest, SinglePrefetchTest) {
    judge::server::amqp config = {
        .uri = "amqp://localhost:25565",
        .exchange = "ProgrammingSubmission",
        .exchange_type = "direct",
        .queue = "ProgrammingSubmission",
        .routing_key = "ProgrammingSubmission",
        .concurrency = 1
    };
    judge::server::rabbitmq_channel mq(config);
    judge::server::rabbitmq_channel::envelope_type e1, e2;
    EXPECT_TRUE(mq.fetch(e1, 0));
    EXPECT_FALSE(mq.fetch(e2, 0));
}
