#pragma once
#include <cstdint>
#include <vector>

class APU;

struct TimerController {
    uint16_t timer_reload[4] = {};
    uint32_t timer_ticks[4] = { 0, 0, 0, 0 };

    // io = reference to Bus's io[] register array
    // apu = pointer to APU (for onTimerOverflow), may be nullptr
    void tick(std::vector<uint8_t>& io, APU* apu);

    // called from Bus::write8 for addresses 0x100-0x10F
    void handleWrite(std::vector<uint8_t>& io, uint32_t ioAddr, uint8_t value);
};