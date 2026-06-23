// APU.cpp
#include "APU.h"
#include "../include/bus.h" 
#include <algorithm>
extern std::ofstream dbg;

template<typename T>
constexpr const T& clamp(const T& v, const T& lo, const T& hi) {
    return (v < lo) ? lo : (hi < v) ? hi : v;
}


static const uint8_t DUTY_TABLE[4][8] = {
    {0,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,1},
    {1,0,0,0,1,1,1,1},
    {0,1,1,1,1,1,1,0},
};


static APU* g_apu = nullptr;  // needed for static callback

APU::APU(Bus& bus) : bus(bus) {
    cyclesPerSample = CPU_FREQ / SAMPLE_RATE;
}

APU::~APU() {
    shutdown();
}

void APU::init() {
    g_apu = this;

    InitAudioDevice();
    stream = LoadAudioStream(SAMPLE_RATE, 16, 2);
    SetAudioStreamCallback(stream, audioCallback);
    PlayAudioStream(stream);
}

void APU::shutdown() {
    if (IsAudioStreamPlaying(stream))
        StopAudioStream(stream);
    UnloadAudioStream(stream);
    CloseAudioDevice();
    g_apu = nullptr;
}

void APU::writeRegister(uint32_t ioOffset, uint32_t val) {
    if (ioOffset == 0xA0) fifoA.push(val);
    if (ioOffset == 0xA4) fifoB.push(val);
}

void APU::FIFOChannel::push(uint32_t word) {
    for (int i = 0; i < 4; i++) {
        if (count < 32) {
            buffer[writePos] = (int8_t)((word >> (i * 8)) & 0xFF);
            writePos = (writePos + 1) % 32;
            count++;
        }
    }
}

void APU::FIFOChannel::pop() {
    if (count > 0) {
        currentSample = buffer[readPos];
        readPos = (readPos + 1) % 32;
        count--;
    }
}

void APU::FIFOChannel::reset() {
    std::fill(buffer.begin(), buffer.end(), 0);
    readPos = writePos = count = 0;
    currentSample = 0;
}


void APU::audioCallback(void* buffer, unsigned int frames) {
    if (!g_apu) return;
    int16_t* out = (int16_t*)buffer;

    for (unsigned int i = 0; i < frames; i++) {
        // pull from ring buffer if available, else silence
        if (g_apu->ringRead != g_apu->ringWrite) {
            out[i * 2 + 0] = g_apu->ringL[g_apu->ringRead];
            out[i * 2 + 1] = g_apu->ringR[g_apu->ringRead];
            g_apu->ringRead = (g_apu->ringRead + 1) % RING_SIZE;
        }
        else {
            out[i * 2 + 0] = 0;
            out[i * 2 + 1] = 0;
        }
    }
}

void APU::tick(int cycles) {
    if (!apuEnabled) return;

    // ── Channel 1 timer ───────────────────────────────────
    if (ch1.enabled) {
        ch1.timer -= cycles;
        while (ch1.timer <= 0) {
            ch1.timer += (2048 - ch1.freq) * 4;
            ch1.dutyStep = (ch1.dutyStep + 1) & 7;
        }
    }

    // ── Channel 2 timer ───────────────────────────────────
    if (ch2.enabled) {
        ch2.timer -= cycles;
        while (ch2.timer <= 0) {
            ch2.timer += (2048 - ch2.freq) * 4;
            ch2.dutyStep = (ch2.dutyStep + 1) & 7;
        }
    }

    // ── Frame sequencer (512 Hz = every 32768 cycles) ─────
    frameSeqTimer += cycles;
    if (frameSeqTimer >= 32768) {
        frameSeqTimer -= 32768;
        frameSeqStep = (frameSeqStep + 1) & 7;

        // length counter — 256 Hz (every even step)
        if (frameSeqStep % 2 == 0) {
            if (ch1.lengthEnabled && ch1.lengthTimer > 0)
                if (--ch1.lengthTimer == 0) ch1.enabled = false;
            if (ch2.lengthEnabled && ch2.lengthTimer > 0)
                if (--ch2.lengthTimer == 0) ch2.enabled = false;
        }

        // sweep — 128 Hz (steps 2 and 6)
        if (frameSeqStep == 2 || frameSeqStep == 6) {
            if (ch1.sweepEnabled && ch1.sweepPeriod > 0) {
                if (--ch1.sweepTimer <= 0) {
                    ch1.sweepTimer = ch1.sweepPeriod;
                    int newFreq = ch1.sweepShadow + (ch1.sweepDir
                        ? -(ch1.sweepShadow >> ch1.sweepShift)
                        : (ch1.sweepShadow >> ch1.sweepShift));
                    if (newFreq > 2047) {
                        ch1.enabled = false;
                    }
                    else if (ch1.sweepShift > 0) {
                        ch1.freq = ch1.sweepShadow = (uint16_t)newFreq;
                    }
                }
            }
        }

        // envelope — 64 Hz (step 7)
        if (frameSeqStep == 7) {
            if (ch1.envPeriod > 0) {
                if (--ch1.envTimer <= 0) {
                    ch1.envTimer = ch1.envPeriod;
                    if (ch1.envDir && ch1.volume < 15) ch1.volume++;
                    else if (!ch1.envDir && ch1.volume > 0) ch1.volume--;
                }
            }
            if (ch2.envPeriod > 0) {
                if (--ch2.envTimer <= 0) {
                    ch2.envTimer = ch2.envPeriod;
                    if (ch2.envDir && ch2.volume < 15) ch2.volume++;
                    else if (!ch2.envDir && ch2.volume > 0) ch2.volume--;
                }
            }
        }
    }

    // ── Sample output ─────────────────────────────────────
    sampleTimer += cycles;
    while (sampleTimer >= cyclesPerSample) {
        sampleTimer -= cyclesPerSample;
        int nextWrite = (ringWrite + 1) % RING_SIZE;
        if (nextWrite != ringRead) {
            ringL[ringWrite] = mixSample(false);
            ringR[ringWrite] = mixSample(true);
            ringWrite = nextWrite;
        }
    }
}


int16_t APU::mixSample(bool right) {
    int32_t out = 0;

    // DMG channels (4-bit, 0-15)
    if (nr51 & (right ? 0x01 : 0x10)) out += ch1.sample();
    if (nr51 & (right ? 0x02 : 0x20)) out += ch2.sample();

    // DMG volume scale
    uint8_t vol = right ? (nr50 & 0x07) : ((nr50 >> 4) & 0x07);
    out = out * (vol + 1);

    // FIFO channels (signed 8-bit, -128..127)
    int32_t fifoOut = 0;
    if (right && fifoA.rightEnable) fifoOut += (int8_t)fifoA.sample();
    if (!right && fifoA.leftEnable)  fifoOut += (int8_t)fifoA.sample();
    if (right && fifoB.rightEnable) fifoOut += (int8_t)fifoB.sample();
    if (!right && fifoB.leftEnable)  fifoOut += (int8_t)fifoB.sample();

    // FIFO volume: doubleVolume=1 means full, 0 means half
    if (!fifoA.doubleVolume) fifoOut = fifoOut >> 1;

    // mix — FIFO is much louder than DMG so scale accordingly
    out += fifoOut * 8;

    // clamp to 16-bit output range
    out = clamp<int32_t>(out, -32768, 32767);
    return (int16_t)out;
}


int8_t APU::Channel1::sample() const {
    if (!enabled || !dacEnabled) return 0;
    return DUTY_TABLE[duty][dutyStep] ? (int8_t)volume : 0;
}

void APU::Channel1::trigger() {
    enabled = true;
    dacEnabled = (envInitVol > 0 || envDir > 0);
    volume = envInitVol;
    envTimer = envPeriod ? envPeriod : 8;  // 0 treated as 8 per pandocs

    // reload length if expired
    if (lengthTimer == 0) lengthTimer = 64;

    // freq timer reload: (2048 - freq) * 4 CPU cycles
    timer = (2048 - freq) * 4;

    // sweep
    sweepShadow = freq;
    sweepTimer = sweepPeriod ? sweepPeriod : 8;
    sweepEnabled = (sweepPeriod != 0 || sweepShift != 0);
    if (sweepShift != 0) {
        // immediate overflow check
        int newFreq = sweepShadow + (sweepDir
            ? -(sweepShadow >> sweepShift)
            : (sweepShadow >> sweepShift));
        if (newFreq > 2047) enabled = false;
    }
}

void APU::writeRegister(uint32_t addr, uint8_t val) {
    // master enable check — except 0x84 itself
    if (addr != 0x84 && !apuEnabled) return;

    switch (addr) {

        // ── SOUNDCNT_X (master on/off) ─────────────────────────
    case 0x84:
        if (!(val & 0x80) && apuEnabled) {
            // turning APU off — reset all PSG registers
            apuEnabled = false;
            ch1 = {}; ch2 = {}; ch3 = {}; ch4 = {};
            nr50 = nr51 = 0;
        }
        else if ((val & 0x80) && !apuEnabled) {
            apuEnabled = true;
            frameSeqStep = 0;
        }
        break;

        // ── Channel 1 ──────────────────────────────────────────
    case 0x60:  // SOUND1CNT_L (NR10) — sweep
        ch1.sweepPeriod = (val >> 4) & 0x07;
        ch1.sweepDir = (val >> 3) & 0x01;
        ch1.sweepShift = val & 0x07;
        break;

    case 0x62:
        ch1.duty = (val >> 6) & 0x03;
        ch1.lengthTimer = 64 - (val & 0x3F);  // compute directly, no lengthLoad needed
        break;

    case 0x63:  // SOUND1CNT_H high byte (NR12) — envelope
        ch1.envInitVol = (val >> 4) & 0x0F;
        ch1.envDir = (val >> 3) & 0x01;
        ch1.envPeriod = val & 0x07;
        ch1.dacEnabled = (ch1.envInitVol > 0 || ch1.envDir > 0);
        if (!ch1.dacEnabled) ch1.enabled = false;
        break;

    case 0x64:  // SOUND1CNT_X low byte (NR13) — freq low 8 bits
        ch1.freq = (ch1.freq & 0x700) | val;
        break;

    case 0x65:  // SOUND1CNT_X high byte (NR14) — freq high + trigger + length enable
        ch1.freq = (ch1.freq & 0x0FF) | ((val & 0x07) << 8);
        ch1.lengthEnabled = (val >> 6) & 0x01;
        if (val & 0x80) ch1.trigger();  // bit 7 = trigger/restart
        break;

        // ── SOUNDCNT_L (NR50/NR51) ────────────────────────────
    case 0x80: nr50 = val; break;
    case 0x81: nr51 = val; break;


        // ── Channel 2 ──────────────────────────────────────────
    case 0x68:  // SOUND2CNT_L low byte (NR21) — duty + length
        ch2.duty = (val >> 6) & 0x03;
        ch2.lengthTimer = 64 - (val & 0x3F);
        break;

    case 0x69:  // SOUND2CNT_L high byte (NR22) — envelope
        ch2.envInitVol = (val >> 4) & 0x0F;
        ch2.envDir = (val >> 3) & 0x01;
        ch2.envPeriod = val & 0x07;
        ch2.dacEnabled = (ch2.envInitVol > 0 || ch2.envDir > 0);
        if (!ch2.dacEnabled) ch2.enabled = false;
        break;

    case 0x6C:  // SOUND2CNT_H low byte (NR23) — freq low 8 bits
        ch2.freq = (ch2.freq & 0x700) | val;
        break;

    case 0x6D:  // SOUND2CNT_H high byte (NR24) — freq high + trigger + length enable
        ch2.freq = (ch2.freq & 0x0FF) | ((val & 0x07) << 8);
        ch2.lengthEnabled = (val >> 6) & 0x01;
        if (val & 0x80) ch2.trigger();
        break;


    case 0x82:  // SOUNDCNT_H low byte
        // bits 0-1: DMG volume (00=25% 01=50% 10=100%)
        // bit 2: FIFO A volume (0=half 1=full)
        // bit 3: FIFO B volume (0=half 1=full)
        fifoA.doubleVolume = (val >> 2) & 1;
        fifoB.doubleVolume = (val >> 3) & 1;
        bus.io[0x82] = val;
        break;

    case 0x83:  // SOUNDCNT_H high byte
        // bit 0: FIFO A right enable
        // bit 1: FIFO A left enable
        // bit 2: FIFO A timer select (0=timer0 1=timer1)
        // bit 3: FIFO A reset
        // bit 4: FIFO B right enable
        // bit 5: FIFO B left enable
        // bit 6: FIFO B timer select
        // bit 7: FIFO B reset
        fifoA.rightEnable = (val >> 0) & 1;
        fifoA.leftEnable = (val >> 1) & 1;
        fifoA.timerSelect = (val >> 2) & 1;
        fifoB.rightEnable = (val >> 4) & 1;
        fifoB.leftEnable = (val >> 5) & 1;
        fifoB.timerSelect = (val >> 6) & 1;
        if (val & (1 << 3)) fifoA.reset();
        if (val & (1 << 7)) fifoB.reset();
        bus.io[0x83] = val;
        break;
    } // more channels to be added here later


}

void APU::onTimerOverflow(int timerIndex) {
    static uint32_t lastOverflowCycle = 0;
    dbg << "[TIMER_OVERFLOW] idx=" << timerIndex
        << " gap=" << (bus.step_counter + 1232 * bus.vcount) - lastOverflowCycle
        << " dma0_dad=0x" << std::hex << bus.dma[0].dad
        << " dma0_idad=0x" << bus.dma[0].idad
        << " dma0_cnt=0x" << bus.dma[0].cnt
        << " dma3_dad=0x" << bus.dma[3].dad
        << " dma3_idad=0x" << bus.dma[3].idad
        << " dma3_cnt=0x" << bus.dma[3].cnt
        << "\n";
    dbg.flush();
    lastOverflowCycle = bus.step_counter + 1232 * bus.vcount;
    if (!apuEnabled) return;
    if ((int)fifoA.timerSelect == timerIndex) {
        fifoA.pop();
        if (fifoA.needsRefill()) onFIFORefill(0);
    }
    if ((int)fifoB.timerSelect == timerIndex) {
        fifoB.pop();
        if (fifoB.needsRefill()) onFIFORefill(1);
    }
}

void APU::onFIFORefill(int fifoIndex) {
    uint32_t fifoAddr = (fifoIndex == 0) ? 0x040000A0 : 0x040000A4;

    /*dbg << "[APU] FIFO refill requested for " << (fifoIndex == 0 ? "A" : "B") << "\n";
    dbg.flush();*/

    for (int ch = 1; ch <= 2; ch++) {
        auto& dma = bus.dma[ch];
        /*dbg << "[APU] checking DMA" << ch
            << " enabled=" << ((dma.cnt >> 31) & 1)
            << " timing=" << ((dma.cnt >> 28) & 0x3)
            << " dad=0x" << std::hex << dma.dad << "\n";
        dbg.flush();*/
        if (!(dma.cnt & (1u << 31))) continue;
        if (((dma.cnt >> 28) & 0x3) != 3) continue;
        if (dma.dad != fifoAddr) continue;
        bus.executeDMA(ch);
        break;
    }
}
void APU::Channel2::trigger() {
    enabled = true;
    dacEnabled = (envInitVol > 0 || envDir > 0);
    volume = envInitVol;
    envTimer = envPeriod ? envPeriod : 8;
    if (lengthTimer == 0) lengthTimer = 64;
    timer = (2048 - freq) * 4;
}

int8_t APU::Channel2::sample() const {
    if (!enabled || !dacEnabled) return 0;
    return DUTY_TABLE[duty][dutyStep] ? (int8_t)volume : 0;
}