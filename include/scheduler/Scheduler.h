#pragma once
#include <cstdint>
#include <functional>
#include <array>
#include <limits>

enum SchedEventType {
    EVT_HBLANK,
    EVT_SCANLINE_ADVANCE,
    EVT_TIMER0,
    EVT_TIMER1,
    EVT_TIMER2,
    EVT_TIMER3,
    EVT_COUNT
};

struct Scheduler {
    struct Event {
        bool active = false;
        uint64_t triggerCycle = 0;
        std::function<void()> callback;
    };

    uint64_t currentCycle = 0;
    std::array<Event, EVT_COUNT> events;

    void schedule(SchedEventType type, uint64_t absoluteCycle, std::function<void()> cb) {
        events[type].active = true;
        events[type].triggerCycle = absoluteCycle;
        events[type].callback = std::move(cb);
    }

    void scheduleIn(SchedEventType type, uint64_t cyclesFromNow, std::function<void()> cb) {
        schedule(type, currentCycle + cyclesFromNow, std::move(cb));
    }

    void cancel(SchedEventType type) {
        events[type].active = false;
    }

    uint64_t peekNextEventCycle() const {
        uint64_t soonest = UINT64_MAX;
        for (auto& e : events)
            if (e.active && e.triggerCycle < soonest)
                soonest = e.triggerCycle;
        return soonest;
    }

    void fireDueEvents() {
        bool firedAny = true;
        while (firedAny) {
            firedAny = false;
            for (auto& e : events) {
                if (e.active && e.triggerCycle <= currentCycle) {
                    e.active = false;
                    auto cb = e.callback;
                    cb();
                    firedAny = true;
                }
            }
        }
    }

    // Jumps the clock forward by `cycles` total, firing events along the way
    // instead of stepping one cycle at a time.
    void advanceBy(uint32_t cycles) {
        uint64_t target = currentCycle + cycles;
        while (currentCycle < target) {
            uint64_t next = peekNextEventCycle();
            currentCycle = (next < target) ? next : target;
            fireDueEvents();
        }
    }
};