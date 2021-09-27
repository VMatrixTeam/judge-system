#pragma once

#include <functional>
#include <chrono>
#include <thread>
#include <boost/noncopyable.hpp>

template <typename _Duration>
struct periodic_timer : boost::noncopyable {
    periodic_timer(std::function<void()> handler, _Duration duration)
        : handler(handler), duration(duration) {
    }

    void run() {
        while (running) {
            auto now = std::chrono::system_clock::now();
            handler();
            std::this_thread::sleep_until(now + duration);
        }
    }

    void stop() {
        running = false;
    }

private:
    std::function<void()> handler;
    _Duration duration;
    bool running = true;
};