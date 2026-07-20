#include "timer/TimerController.h"
#include "audio/apu.h"

void TimerController::tick(std::vector<uint8_t>& io, APU* apu) {
    const uint16_t prescaler_shifts[4] = { 0, 6, 8, 10 };
    bool overflowed[4] = { false, false, false, false };

    for (int i = 0; i < 4; i++) {
        uint16_t cnt_h = io[0x102 + i * 4] | (io[0x103 + i * 4] << 8);
        if (!(cnt_h & (1 << 7))) continue;

        bool should_increment = false;
        bool cascade_mode = (i > 0) && (cnt_h & (1 << 2));

        if (cascade_mode) {
            should_increment = overflowed[i - 1];
        }
        else {
            uint8_t prescaler_sel = cnt_h & 0x3;
            timer_ticks[i]++;
            if (timer_ticks[i] >= (1u << prescaler_shifts[prescaler_sel])) {
                timer_ticks[i] = 0;
                should_increment = true;
            }
        }

        if (should_increment) {
            uint16_t current_val = io[0x100 + i * 4] | (io[0x101 + i * 4] << 8);
            current_val++;

            if (current_val == 0) {
                overflowed[i] = true;
                current_val = timer_reload[i];

                if (cnt_h & (1 << 6)) {
                    uint16_t IF = io[0x202] | (io[0x203] << 8);
                    IF |= (1 << (3 + i));
                    io[0x202] = IF & 0xFF;
                    io[0x203] = (IF >> 8) & 0xFF;
                }

                if (apu) apu->onTimerOverflow(i);
            }

            io[0x100 + i * 4] = current_val & 0xFF;
            io[0x101 + i * 4] = (current_val >> 8) & 0xFF;
        }
    }
}

void TimerController::handleWrite(std::vector<uint8_t>& io, uint32_t ioAddr, uint8_t value) {
    int timerIdx = (ioAddr - 0x100) / 4;
    int byteOff = (ioAddr - 0x100) % 4;

    if (byteOff == 0) {
        // Low byte of reload latch — don't touch running counter
        ((uint8_t*)&timer_reload[timerIdx])[0] = value;
    }
    else if (byteOff == 1) {
        // High byte of reload latch
        ((uint8_t*)&timer_reload[timerIdx])[1] = value;
    }
    else if (byteOff == 2) {
        // CNT_H low byte — bit 7 = start/stop
        bool was_on = (io[ioAddr] & 0x80) != 0;
        bool now_on = (value & 0x80) != 0;
        io[ioAddr] = value;
        if (!was_on && now_on) {
            io[0x100 + timerIdx * 4] = timer_reload[timerIdx] & 0xFF;
            io[0x101 + timerIdx * 4] = (timer_reload[timerIdx] >> 8) & 0xFF;
        }
    }
    else {
        // byteOff == 3: CNT_H high byte (unused on GBA)
        io[ioAddr] = value;
    }
}