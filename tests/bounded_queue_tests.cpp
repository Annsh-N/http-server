#include "http/bounded_queue.hpp"

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace {

using namespace std::chrono_literals;

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

void test_capacity_must_be_positive() {
    bool threw = false;
    try {
        http::BoundedQueue<int> queue(0);
    } catch (const std::invalid_argument&) {
        threw = true;
    }

    expect(threw, "zero-capacity queue should be rejected");
}

void test_fifo_order() {
    http::BoundedQueue<int> queue(3);
    expect(queue.push(10), "first push should succeed");
    expect(queue.push(20), "second push should succeed");
    expect(queue.push(30), "third push should succeed");

    expect(queue.pop() == 10, "queue should pop the first item first");
    expect(queue.pop() == 20, "queue should preserve FIFO order");
    expect(queue.pop() == 30, "queue should pop the final item last");
}

void test_full_queue_blocks_producer() {
    http::BoundedQueue<int> queue(1);
    expect(queue.push(1), "initial push should fill queue");

    std::promise<void> producer_started;
    std::future<void> started = producer_started.get_future();
    std::promise<bool> producer_finished;
    std::future<bool> finished = producer_finished.get_future();

    std::thread producer([&] {
        producer_started.set_value();
        producer_finished.set_value(queue.push(2));
    });

    started.wait();
    expect(finished.wait_for(50ms) == std::future_status::timeout,
           "producer should block while queue is full");
    expect(queue.pop() == 1, "consumer should free one queue slot");
    expect(finished.wait_for(1s) == std::future_status::ready,
           "producer should wake after a slot becomes available");
    expect(finished.get(), "unblocked producer push should succeed");
    expect(queue.pop() == 2, "unblocked item should enter the queue");
    producer.join();
}

void test_close_wakes_waiters_and_drains_items() {
    http::BoundedQueue<int> queue(1);
    std::promise<std::optional<int>> consumer_result;
    auto result = consumer_result.get_future();

    std::thread consumer([&] { consumer_result.set_value(queue.pop()); });
    expect(result.wait_for(50ms) == std::future_status::timeout,
           "consumer should block while open queue is empty");

    queue.close();
    expect(result.wait_for(1s) == std::future_status::ready,
           "close should wake an empty-queue consumer");
    expect(!result.get().has_value(),
           "closed and drained queue should return no item");
    expect(!queue.push(1), "push should fail after close");
    consumer.join();

    http::BoundedQueue<int> full_queue(1);
    expect(full_queue.push(3), "initial item should fill queue");
    std::promise<void> close_producer_started;
    auto close_started = close_producer_started.get_future();
    std::promise<bool> producer_result;
    auto pushed = producer_result.get_future();
    std::thread producer([&] {
        close_producer_started.set_value();
        producer_result.set_value(full_queue.push(4));
    });
    close_started.wait();
    expect(pushed.wait_for(50ms) == std::future_status::timeout,
           "producer should wait before full queue closes");
    full_queue.close();
    expect(pushed.wait_for(1s) == std::future_status::ready,
           "close should wake a blocked producer");
    expect(!pushed.get(), "woken producer should report closed queue");
    expect(full_queue.pop() == 3,
           "closing a full queue should preserve its existing item");
    producer.join();

    http::BoundedQueue<int> draining_queue(2);
    expect(draining_queue.push(7), "push before close should succeed");
    draining_queue.close();
    expect(draining_queue.pop() == 7,
           "close should preserve already-enqueued items");
    expect(!draining_queue.pop().has_value(),
           "drained closed queue should return no item");
}

void test_move_only_items() {
    http::BoundedQueue<std::unique_ptr<int>> queue(1);
    expect(queue.push(std::make_unique<int>(42)),
           "queue should accept a move-only item");

    auto item = queue.pop();
    expect(item.has_value() && **item == 42,
           "queue should return ownership of a move-only item");
}

} // namespace

int main() {
    test_capacity_must_be_positive();
    test_fifo_order();
    test_full_queue_blocks_producer();
    test_close_wakes_waiters_and_drains_items();
    test_move_only_items();

    if (failures != 0) {
        std::cerr << failures << " bounded queue test assertion(s) failed\n";
        return EXIT_FAILURE;
    }

    std::cout << "bounded_queue_tests passed\n";
    return 0;
}
