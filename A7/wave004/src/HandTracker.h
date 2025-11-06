#pragma once
#include <atomic>
#include <mutex>
#include <optional>

struct HandSample {
    bool   hasHand = false;
    bool   pinching = false;
    float  normX = 0.5f; // [0..1], 0 = left
    float  normY = 0.5f; // [0..1], 0 = bottom
    double timestamp = 0.0;
};

class HandTracker {
public:
    HandTracker();
    ~HandTracker();

    bool start();   // begin camera + Vision
    void stop();
    std::optional<HandSample> latest() const; // most recent sample, if any

private:
    struct Impl;
    Impl* impl;     // PIMPL to keep ObjC++ out of this header
};
