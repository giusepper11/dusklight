#include "gfx/present_observer.hpp"

#include <atomic>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
AuroraPresentEvent lastEvent{};
std::atomic<uint64_t> calls{0};
void receive(const AuroraPresentEvent* event) {
    lastEvent = *event; // One producer; read only after producer joins.
    calls.fetch_add(1, std::memory_order_relaxed);
}
void require(bool value) {
    if (!value) {
        std::cerr << "FAIL: present observer lifecycle/publication\n";
        std::exit(1);
    }
}
}

int main() {
    aurora::gfx::PresentObserver observer;
    require(!observer.enabled());
    observer.publish({1000, 0, 5, 6, 7, 8});
    require(calls == 0);
    observer.setCallback(receive);
    require(observer.enabled());
    observer.publish({2000, 0, 5, 6, 7, 8});
    require(calls == 1 && lastEvent.timestampNs == 2000 && lastEvent.presentationId == 1 &&
            lastEvent.drawCalls == 5 && lastEvent.pipelinesQueued == 8);
    std::thread producer([&] {
        for (uint64_t i = 1; i <= 100000; ++i) {
            observer.publish({i * 1000 + 2000, 0, 9, 10, 11, 12});
        }
    });
    for (int i = 0; i < 10000; ++i) {
        observer.setCallback(nullptr);
        observer.setCallback(receive);
    }
    producer.join();
    observer.setCallback(nullptr);
    const auto before = calls.load();
    observer.publish({4000, 0, 0, 0, 0, 0});
    require(calls.load() == before && lastEvent.presentationId == before);
    std::cout << "PASS: additive Aurora present observer, disabled path and concurrent callback publication\n";
}
