#pragma once
#include <cstdint>
#include <vector>
#include <algorithm>
#include "../include/raylib.h"

struct Bus;

class APU {
public:
    APU(Bus& bus);
    ~APU();

    void init();
    void shutdown();
    void tick(int cycles);
    void writeRegister(uint32_t ioOffset, uint8_t val);
    void writeRegister(uint32_t ioOffset, uint32_t val);  // FIFO overload
    void onTimerOverflow(int timerIndex);
    void onFIFORefill(int fifoIndex);  // 0=A, 1=B

private:
    Bus& bus;

    struct Channel1 {
        uint8_t  sweepPeriod = 0, sweepDir = 0, sweepShift = 0;
        int      sweepTimer = 0;
        uint16_t sweepShadow = 0;
        bool     sweepEnabled = false;
        uint8_t  duty = 0;
        int      lengthTimer = 0;
        bool     lengthEnabled = false;
        uint8_t  envInitVol = 0;
        uint8_t  envDir = 0;
        uint8_t  envPeriod = 0;
        int      envTimer = 0;
        uint8_t  volume = 0;
        uint16_t freq = 0;
        int      timer = 0;
        int      dutyStep = 0;
        bool     enabled = false;
        bool     dacEnabled = false;
        int8_t   sample() const;
        void     trigger();
    } ch1;

    struct Channel2 {
        uint8_t  duty = 0;
        int      lengthTimer = 0;
        bool     lengthEnabled = false;
        uint8_t  envInitVol = 0;
        uint8_t  envDir = 0;
        uint8_t  envPeriod = 0;
        int      envTimer = 0;
        uint8_t  volume = 0;
        uint16_t freq = 0;
        int      timer = 0;
        int      dutyStep = 0;
        bool     enabled = false;
        bool     dacEnabled = false;
        int8_t   sample() const;
        void     trigger();
    } ch2;

    struct Channel3 {
        bool     dacEnabled = false;
        uint8_t  volumeCode = 0;
        uint16_t freq = 0;
        int      timer = 0;
        int      wavePos = 0;
        bool     enabled = false;
        int      lengthTimer = 0;
        bool     lengthEnabled = false;
        int8_t   lastNibble = 0;
        std::vector<uint8_t> waveRAM = std::vector<uint8_t>(16, 0);
        int8_t   sample() const;
        void     trigger();
    } ch3;

    struct Channel4 {
        uint8_t  volume = 0;
        uint8_t  envInitVol = 0;
        uint8_t  envDir = 0;
        uint8_t  envPeriod = 0;
        int      envTimer = 0;
        uint16_t lfsr = 0x7FFF;
        bool     shortMode = false;
        int      timer = 0;
        bool     enabled = false;
        int      lengthTimer = 0;
        bool     lengthEnabled = false;
        bool     dacEnabled = false;
        int8_t   sample() const;
        void     trigger();
    } ch4;

    struct FIFOChannel {
        std::vector<int8_t> buffer = std::vector<int8_t>(32, 0);
        int     readPos = 0;
        int     writePos = 0;
        int     count = 0;
        int8_t  currentSample = 0;
        bool    leftEnable = false;
        bool    rightEnable = false;
        bool    doubleVolume = false;
        bool timerSelect = false;  // false=timer0, true=timer1

        void    push(uint32_t word);
        void    pop();
        bool    needsRefill() const { return count <= 16; }
        int8_t  sample() const { return currentSample; }
        void    reset();
    } fifoA, fifoB;

    // frame sequencer
    int frameSeqTimer = 0;
    int frameSeqStep = 0;

    // master control
    bool    apuEnabled = false;
    uint8_t nr50 = 0;
    uint8_t nr51 = 0;

    // raylib stream
    AudioStream stream;
    static void audioCallback(void* buffer, unsigned int frames);

    // sample timing
    int sampleTimer = 0;
    int cyclesPerSample = 0;

    static constexpr int SAMPLE_RATE = 32768;
    static constexpr int CPU_FREQ = 16777216;
    static constexpr int RING_SIZE = 4096;

    // ring buffer — vectors
    std::vector<int16_t> ringL = std::vector<int16_t>(RING_SIZE, 0);
    std::vector<int16_t> ringR = std::vector<int16_t>(RING_SIZE, 0);
    int ringWrite = 0;
    int ringRead = 0;

    int16_t mixSample(bool right);
};