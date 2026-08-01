#pragma once
#include <cstdint>
#include <vector>
#include "scheduler/Scheduler.h"
#include "audio/apu.h"


struct TimerController {
    uint16_t timer_reload[4] = {};
    uint16_t timer_value[4] = {};
    bool timer_running[4] = {};
    bool timer_cascade[4] = {};
    bool timer_irqEnabled[4] = {};
    uint8_t timer_prescalerSel[4] = {};

    Scheduler* scheduler = nullptr;
    std::vector<uint8_t>* io = nullptr;
    APU* apu = nullptr;

    static constexpr uint16_t prescaler_shifts[4] = { 0, 6, 8, 10 };

    void init(Scheduler& sched, std::vector<uint8_t>& ioRef, APU* apuPtr) {
        scheduler = &sched;
        io = &ioRef;
        apu = apuPtr;
    }

    void handleWrite(std::vector<uint8_t>& ioRef, uint32_t ioAddr, uint8_t value) {
        int i = (ioAddr - 0x100) / 4;
        int byteOff = (ioAddr - 0x100) % 4;

        if (byteOff == 0) {
            ((uint8_t*)&timer_reload[i])[0] = value;
        }
        else if (byteOff == 1) {
            ((uint8_t*)&timer_reload[i])[1] = value;
        }
        else if (byteOff == 2) {
            bool was_on = (ioRef[ioAddr] & 0x80) != 0;
            bool now_on = (value & 0x80) != 0;
            ioRef[ioAddr] = value;

            timer_cascade[i] = (i > 0) && (value & (1 << 2));
            timer_irqEnabled[i] = (value & (1 << 6)) != 0;
            timer_prescalerSel[i] = value & 0x3;
            timer_running[i] = now_on;

            if (!was_on && now_on) {
                timer_value[i] = timer_reload[i];
                writeBack(i, ioRef);
                if (!timer_cascade[i]) scheduleOverflow(i);
                else scheduler->cancel((SchedEventType)(EVT_TIMER0 + i));
            }
            else if (was_on && !now_on) {
                snapshotValue(i, ioRef);
                scheduler->cancel((SchedEventType)(EVT_TIMER0 + i));
            }
            else if (was_on && now_on && !timer_cascade[i]) {
                snapshotValue(i, ioRef);
                scheduleOverflow(i);
            }
        }
        else {
            ioRef[ioAddr] = value;
        }
    }

    void writeBack(int i, std::vector<uint8_t>& ioRef) {
        ioRef[0x100 + i * 4] = timer_value[i] & 0xFF;
        ioRef[0x101 + i * 4] = (timer_value[i] >> 8) & 0xFF;
    }

    // Recompute the timer's live counter value from elapsed real cycles,
    // since we no longer tick it every cycle.
    void snapshotValue(int i, std::vector<uint8_t>& ioRef) {
        auto& ev = scheduler->events[EVT_TIMER0 + i];
        if (!ev.active) return;
        uint32_t shift = prescaler_shifts[timer_prescalerSel[i]];
        uint64_t cyclesTotal = ((uint64_t)(0x10000 - timer_value[i])) << shift;
        uint64_t cyclesRemaining = ev.triggerCycle - scheduler->currentCycle;
        uint64_t cyclesElapsed = (cyclesTotal > cyclesRemaining) ? (cyclesTotal - cyclesRemaining) : 0;
        uint32_t ticksElapsed = (uint32_t)(cyclesElapsed >> shift);
        timer_value[i] = (uint16_t)(timer_value[i] + ticksElapsed);
        writeBack(i, ioRef);
    }

    void scheduleOverflow(int i) {
        if (!timer_running[i] || timer_cascade[i]) return;
        uint32_t shift = prescaler_shifts[timer_prescalerSel[i]];
        uint64_t cyclesToOverflow = ((uint64_t)(0x10000 - timer_value[i])) << shift;
        /*extern std::ofstream dbg;
       dbg << "[TIMER_SCHED] i=" << i << " value=" << timer_value[i]
            << " shift=" << shift << " cyclesToOverflow=" << cyclesToOverflow
            << " nowCycle=" << scheduler->currentCycle << "\n";
        dbg.flush();*/
        scheduler->scheduleIn((SchedEventType)(EVT_TIMER0 + i), cyclesToOverflow,
            [this, i]() { onOverflow(i); });
    }

    void onOverflow(int i) {
        auto& ioRef = *io;
        timer_value[i] = timer_reload[i];
        writeBack(i, ioRef);

        if (timer_irqEnabled[i]) {
            uint16_t IF = ioRef[0x202] | (ioRef[0x203] << 8);
            IF |= (1 << (3 + i));
            ioRef[0x202] = IF & 0xFF;
            ioRef[0x203] = (IF >> 8) & 0xFF;
        }
        if (apu) apu->onTimerOverflow(i);
        if (timer_running[i]) scheduleOverflow(i);

        // Cascade: bump the next timer by exactly one tick, no prescaler of its own.
        if (i + 1 < 4 && timer_running[i + 1] && timer_cascade[i + 1]) {
            timer_value[i + 1]++;
            if (timer_value[i + 1] == 0) onOverflow(i + 1);
            else writeBack(i + 1, ioRef);
        }
    }
};