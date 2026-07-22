#pragma once
#include <cstdint>

struct Bus;

struct DMAChannel {
    uint32_t sad = 0, dad = 0;
    uint32_t cnt = 0;
    uint32_t isad = 0;
    uint32_t idad = 0;
};

struct DMAController {
    DMAChannel channels[4];
    uint32_t pendingCycles = 0;

    bool writeReg(uint32_t address, uint8_t value); // returns true if this write should trigger immediate DMA
    void execute(int channel, Bus& bus);             // needs bus for read/write
    void onVBlank(Bus& bus);
    void onHBlank(Bus& bus);
};