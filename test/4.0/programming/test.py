import json
import pika
import sys
import time

if len(sys.argv) != 3:
    sys.stderr.write('{0}: 1 arguments needed, {1} given\n'.format(
        sys.argv[0], len(sys.argv) - 1))
    print("Usage: {0} [request.json] [repeat times]".format(sys.argv[0]))
    sys.exit(2)

print('the first argument is {0}， the second argument is {1}\n'.format(
    sys.argv[1], sys.argv[2]))

host = "127.0.0.1"
port = 25565
queue_name = "matrix-judge-request"
exchange = "matrix-judge-request-exchange"
repeats = int(sys.argv[2])


def init_rabbitmq():
    global host, port, queue_name, exchange
    connection = pika.BlockingConnection(pika.ConnectionParameters(host, port))
    channel = connection.channel()
    channel.exchange_declare(exchange=exchange, durable=True,
                             exchange_type="direct")
    channel.queue_declare(queue=queue_name, durable=True)
    channel.queue_bind(exchange=exchange, queue=queue_name)
    return channel


def send(channel, sub_id, prob_id, req):
    print("Sending %s:%s" % (sub_id, prob_id))

    req['sub_id'] = sub_id
    if (prob_id != None):
        req['prob_id'] = prob_id

    global queue_name, exchange
    channel.basic_publish(
        exchange=exchange, routing_key=queue_name, body=json.dumps(req))
    channel.start_consuming()


def main():
    channel = init_rabbitmq()
    req = json.load(open(sys.argv[1]))
    for i in range(0, repeats):
        send(channel, '1234%d' % i, None, req)
        # send(channel, '1234%d' % i, req['prob_id'], req)


if __name__ == "__main__":
    main()
