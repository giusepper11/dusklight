#include "dusk/performance_capture.hpp"
#include <iostream>

using namespace dusk::perf;

int main() {
    int failures = 0;
    Capture capture;
    capture.start(1);
    capture.record(1000000001, 1);
    capture.record(2000000001, 2);
    capture.record(120000000001, 3);
    if (capture.samples().size() != 1 || capture.eventCount() != 2) {
        std::cerr << "FAIL: endpoint beyond deadline entered capture population\n";
        ++failures;
    }
    capture.start(1, 1000000);
    for (size_t i = 0; i <= SampleCapacity; ++i) {
        capture.record(i + 1, i + 1);
    }
    capture.transportOverflow(7);
    if (capture.droppedCount() != 7) {
        std::cerr << "FAIL: capacity completion suppressed observed transport losses\n";
        ++failures;
    }
    if (failures == 0) {
        std::cout << "PASS: independent-review deadline and completed-capture loss regressions\n";
    }
    return failures == 0 ? 0 : 1;
}
