#include "audio/apu.h"
#include "core/bus.h"
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

static const int CH4_DIVISORS[8] = { 8, 16, 32, 48, 64, 80, 96, 112 };

static APU* g_apu = nullptr;

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

void APU::reset() {
    ch1 = Channel1{};
    ch2 = Channel2{};
    ch3 = Channel3{};
    ch4 = Channel4{};
    fifoA = FIFOChannel{};
    fifoB = FIFOChannel{};
    frameSeqTimer = 0;
    frameSeqStep = 0;
    apuEnabled = false;
    nr50 = 0;
    nr51 = 0;
    sampleTimer = 0;
    std::fill(ringL.begin(), ringL.end(), 0);
    std::fill(ringR.begin(), ringR.end(), 0);
    ringWrite = 0;
    ringRead = 0;
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

    // ── Channel 3 timer ───────────────────────────────────
    // Steps through 32 nibbles stored in waveRAM.
    // Period is (2048 - freq) * 2 — half of ch1/ch2 because
    // we have 32 samples vs 8 duty steps, keeping pitch consistent.
    if (ch3.enabled && ch3.dacEnabled) {
        ch3.timer -= cycles;
        while (ch3.timer <= 0) {
            ch3.timer += (2048 - ch3.freq) * 2;
            ch3.wavePos = (ch3.wavePos + 1) & 31;
            uint8_t byte = ch3.waveRAM[ch3.wavePos / 2];
            // even wavePos = high nibble, odd = low nibble
            ch3.lastNibble = (ch3.wavePos & 1)
                ? (byte & 0x0F)
                : (byte >> 4);
        }
    }

    // ── Channel 4 timer ───────────────────────────────────
    // Clocks the LFSR at a rate determined by divisorCode + clockShift.
    // Each clock: XOR bits 0 and 1, shift right, feed XOR into bit 14.
    // shortMode also feeds XOR into bit 6 → 127-step sequence instead of 32767.
    if (ch4.enabled && ch4.dacEnabled) {
        ch4.timer -= cycles;
        while (ch4.timer <= 0) {
            ch4.timer += ch4.period;
            uint16_t xorBit = (ch4.lfsr & 1) ^ ((ch4.lfsr >> 1) & 1);
            ch4.lfsr >>= 1;
            ch4.lfsr |= (xorBit << 14);
            if (ch4.shortMode) {
                ch4.lfsr &= ~(1 << 6);
                ch4.lfsr |= (xorBit << 6);
            }
        }
    }

    // ── Frame sequencer (512 Hz = every 32768 cycles) ─────
    frameSeqTimer += cycles;
    if (frameSeqTimer >= 32768) {
        frameSeqTimer -= 32768;
        frameSeqStep = (frameSeqStep + 1) & 7;

        // Length counters — clocked at 256 Hz (every even step)
        if (frameSeqStep % 2 == 0) {
            if (ch1.lengthEnabled && ch1.lengthTimer > 0)
                if (--ch1.lengthTimer == 0) ch1.enabled = false;
            if (ch2.lengthEnabled && ch2.lengthTimer > 0)
                if (--ch2.lengthTimer == 0) ch2.enabled = false;
            if (ch3.lengthEnabled && ch3.lengthTimer > 0)
                if (--ch3.lengthTimer == 0) ch3.enabled = false;
            if (ch4.lengthEnabled && ch4.lengthTimer > 0)
                if (--ch4.lengthTimer == 0) ch4.enabled = false;
        }

        // Sweep — 128 Hz (steps 2 and 6), ch1 only
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

        // Envelopes — 64 Hz (step 7), ch1 ch2 ch4
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
            if (ch4.envPeriod > 0) {
                if (--ch4.envTimer <= 0) {
                    ch4.envTimer = ch4.envPeriod;
                    if (ch4.envDir && ch4.volume < 15) ch4.volume++;
                    else if (!ch4.envDir && ch4.volume > 0) ch4.volume--;
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

    // DMG channels — each outputs 0..15
    if (nr51 & (right ? 0x01 : 0x10)) out += ch1.sample();
    if (nr51 & (right ? 0x02 : 0x20)) out += ch2.sample();
    if (nr51 & (right ? 0x04 : 0x40)) out += ch3.sample();
    if (nr51 & (right ? 0x08 : 0x80)) out += ch4.sample();

    // DMG master volume scale (0-7 → multiply by 1-8)
    uint8_t vol = right ? (nr50 & 0x07) : ((nr50 >> 4) & 0x07);
    out = out * (vol + 1);

    // FIFO channels — signed 8-bit
    int32_t fifoOut = 0;
    if (right && fifoA.rightEnable) fifoOut += (int8_t)fifoA.sample();
    if (!right && fifoA.leftEnable)  fifoOut += (int8_t)fifoA.sample();
    if (right && fifoB.rightEnable) fifoOut += (int8_t)fifoB.sample();
    if (!right && fifoB.leftEnable)  fifoOut += (int8_t)fifoB.sample();

    if (!fifoA.doubleVolume) fifoOut = fifoOut >> 1;

    out += fifoOut * 8;

    out = clamp<int32_t>(out, -32768, 32767);
    return (int16_t)out;
}

// ── Channel 1 ─────────────────────────────────────────────────────────────────

int8_t APU::Channel1::sample() const {
    if (!enabled || !dacEnabled) return 0;
    return DUTY_TABLE[duty][dutyStep] ? (int8_t)volume : 0;
}

void APU::Channel1::trigger() {
    enabled = true;
    dacEnabled = (envInitVol > 0 || envDir > 0);
    volume = envInitVol;
    envTimer = envPeriod ? envPeriod : 8;
    if (lengthTimer == 0) lengthTimer = 64;
    timer = (2048 - freq) * 4;
    sweepShadow = freq;
    sweepTimer = sweepPeriod ? sweepPeriod : 8;
    sweepEnabled = (sweepPeriod != 0 || sweepShift != 0);
    if (sweepShift != 0) {
        int newFreq = sweepShadow + (sweepDir
            ? -(sweepShadow >> sweepShift)
            : (sweepShadow >> sweepShift));
        if (newFreq > 2047) enabled = false;
    }
}

// ── Channel 2 ─────────────────────────────────────────────────────────────────

int8_t APU::Channel2::sample() const {
    if (!enabled || !dacEnabled) return 0;
    return DUTY_TABLE[duty][dutyStep] ? (int8_t)volume : 0;
}

void APU::Channel2::trigger() {
    enabled = true;
    dacEnabled = (envInitVol > 0 || envDir > 0);
    volume = envInitVol;
    envTimer = envPeriod ? envPeriod : 8;
    if (lengthTimer == 0) lengthTimer = 64;
    timer = (2048 - freq) * 4;
}

// ── Channel 3 ─────────────────────────────────────────────────────────────────

int8_t APU::Channel3::sample() const {
    if (!enabled || !dacEnabled) return 0;
    int8_t s;
    switch (volumeCode) {
    case 0: return 0;                    // mute
    case 1: s = lastNibble; break;       // 100% — raw nibble 0-15
    case 2: s = lastNibble >> 1; break;  // 50%
    case 3: s = lastNibble >> 2; break;  // 25%
    default: return 0;
    }
    return s - 8; // shift from 0..15 to -8..7 to center around zero
}

void APU::Channel3::trigger() {
    enabled = true;
    // ch3 length counter is 256 steps not 64
    if (lengthTimer == 0) lengthTimer = 256;
    wavePos = 0;
    timer = (2048 - freq) * 2;
}

// ── Channel 4 ─────────────────────────────────────────────────────────────────

int8_t APU::Channel4::sample() const {
    if (!enabled || !dacEnabled) return 0;
    // bit 0 of LFSR: 0 = output volume, 1 = silence
    return (lfsr & 1) ? 0 : (int8_t)volume;
}

void APU::Channel4::trigger() {
    enabled = true;
    dacEnabled = (envInitVol > 0 || envDir > 0);
    volume = envInitVol;
    envTimer = envPeriod ? envPeriod : 8;
    if (lengthTimer == 0) lengthTimer = 64;
    lfsr = 0x7FFF; // reset to all 1s
    period = CH4_DIVISORS[divisorCode] << clockShift;
    timer = period;
}

// ── Register writes ───────────────────────────────────────────────────────────

void APU::writeRegister(uint32_t addr, uint8_t val) {
    if (addr != 0x84 && !apuEnabled) return;

    switch (addr) {

        // ── Master control ─────────────────────────────────────
    case 0x84:
        if (!(val & 0x80) && apuEnabled) {
            apuEnabled = false;
            ch1 = {}; ch2 = {}; ch4 = {};
            ch3.enabled = false;
            ch3.dacEnabled = false;
            ch3.waveRAM.assign(16, 0);
            nr50 = nr51 = 0;
        }
        else if ((val & 0x80) && !apuEnabled) {
            apuEnabled = true;
            frameSeqStep = 0;
        }
        break;

        // ── Channel 1 ──────────────────────────────────────────
    case 0x60:  // NR10 — sweep
        ch1.sweepPeriod = (val >> 4) & 0x07;
        ch1.sweepDir = (val >> 3) & 0x01;
        ch1.sweepShift = val & 0x07;
        break;

    case 0x62:  // NR11 — duty + length
        ch1.duty = (val >> 6) & 0x03;
        ch1.lengthTimer = 64 - (val & 0x3F);
        break;

    case 0x63:  // NR12 — envelope
        ch1.envInitVol = (val >> 4) & 0x0F;
        ch1.envDir = (val >> 3) & 0x01;
        ch1.envPeriod = val & 0x07;
        ch1.dacEnabled = (ch1.envInitVol > 0 || ch1.envDir > 0);
        if (!ch1.dacEnabled) ch1.enabled = false;
        break;

    case 0x64:  // NR13 — freq low
        ch1.freq = (ch1.freq & 0x700) | val;
        break;

    case 0x65:  // NR14 — freq high + trigger + length enable
        ch1.freq = (ch1.freq & 0x0FF) | ((val & 0x07) << 8);
        ch1.lengthEnabled = (val >> 6) & 0x01;
        if (val & 0x80) ch1.trigger();
        break;

        // ── Channel 2 ──────────────────────────────────────────
    case 0x68:  // NR21 — duty + length
        ch2.duty = (val >> 6) & 0x03;
        ch2.lengthTimer = 64 - (val & 0x3F);
        break;

    case 0x69:  // NR22 — envelope
        ch2.envInitVol = (val >> 4) & 0x0F;
        ch2.envDir = (val >> 3) & 0x01;
        ch2.envPeriod = val & 0x07;
        ch2.dacEnabled = (ch2.envInitVol > 0 || ch2.envDir > 0);
        if (!ch2.dacEnabled) ch2.enabled = false;
        break;

    case 0x6C:  // NR23 — freq low
        ch2.freq = (ch2.freq & 0x700) | val;
        break;

    case 0x6D:  // NR24 — freq high + trigger + length enable
        ch2.freq = (ch2.freq & 0x0FF) | ((val & 0x07) << 8);
        ch2.lengthEnabled = (val >> 6) & 0x01;
        if (val & 0x80) ch2.trigger();
        break;

        // ── Channel 3 ──────────────────────────────────────────
    case 0x70:  // NR30 — DAC enable
        // bit 7: DAC on/off. If DAC off, channel stops immediately.
        ch3.dacEnabled = (val >> 7) & 1;
        if (!ch3.dacEnabled) ch3.enabled = false;
        break;

    case 0x72:  // NR31 — length (256 step counter, not 64)
        ch3.lengthTimer = 256 - val;
        break;

    case 0x73:  // NR32 — volume code
        // bits 6-5: 00=mute 01=100% 10=50% 11=25%
        ch3.volumeCode = (val >> 5) & 0x03;
        break;

    case 0x74:  // NR33 — freq low
        ch3.freq = (ch3.freq & 0x700) | val;
        break;

    case 0x75:  // NR34 — freq high + trigger + length enable
        ch3.freq = (ch3.freq & 0x0FF) | ((val & 0x07) << 8);
        ch3.lengthEnabled = (val >> 6) & 1;
        if (val & 0x80) ch3.trigger();
        break;

        // Wave RAM — game writes waveform shape here before triggering ch3
    case 0x90: case 0x91: case 0x92: case 0x93:
    case 0x94: case 0x95: case 0x96: case 0x97:
    case 0x98: case 0x99: case 0x9A: case 0x9B:
    case 0x9C: case 0x9D: case 0x9E: case 0x9F:
        ch3.waveRAM[addr - 0x90] = val;
        break;

        // ── Channel 4 ──────────────────────────────────────────
    case 0x78:  // NR41 — length
        ch4.lengthTimer = 64 - (val & 0x3F);
        break;

    case 0x79:  // NR42 — envelope
        ch4.envInitVol = (val >> 4) & 0x0F;
        ch4.envDir = (val >> 3) & 0x01;
        ch4.envPeriod = val & 0x07;
        ch4.dacEnabled = (ch4.envInitVol > 0 || ch4.envDir > 0);
        if (!ch4.dacEnabled) ch4.enabled = false;
        break;

    case 0x7C:  // NR43 — clock shift + short mode + divisor
        // clockShift (bits 7-4): shifts the base period left → lower pitch
        // shortMode  (bit 3):    7-bit LFSR → buzzy tone instead of noise
        // divisorCode(bits 2-0): selects base period from lookup table
        ch4.clockShift = (val >> 4) & 0x0F;
        ch4.shortMode = (val >> 3) & 0x01;
        ch4.divisorCode = val & 0x07;
        ch4.period = CH4_DIVISORS[ch4.divisorCode] << ch4.clockShift;
        break;

    case 0x7D:  // NR44 — trigger + length enable
        ch4.lengthEnabled = (val >> 6) & 1;
        if (val & 0x80) ch4.trigger();
        break;

        // ── Master volume / panning ────────────────────────────
    case 0x80: nr50 = val; break;  // left/right master volume
    case 0x81: nr51 = val; break;  // channel left/right enables

    case 0x82:  // SOUNDCNT_H low — FIFO volume
        fifoA.doubleVolume = (val >> 2) & 1;
        fifoB.doubleVolume = (val >> 3) & 1;
        bus.io[0x82] = val;
        break;

    case 0x83:  // SOUNDCNT_H high — FIFO routing + reset
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
    }
}

void APU::onTimerOverflow(int timerIndex) {
    static uint32_t lastOverflowCycle = 0;
    /*dbg << "[TIMER_OVERFLOW] idx=" << timerIndex
        << " gap=" << (bus.step_counter + 1232 * bus.vcount) - lastOverflowCycle
        << " dma0_dad=0x" << std::hex << bus.dmaController.channels[0].dad
        << " dma0_idad=0x" << bus.dmaController.channels[0].idad
        << " dma0_cnt=0x" << bus.dmaController.channels[0].cnt
        << " dma3_dad=0x" << bus.dmaController.channels[3].dad
        << " dma3_idad=0x" << bus.dmaController.channels[3].idad
        << " dma3_cnt=0x" << bus.dmaController.channels[3].cnt
        << "\n";
    dbg.flush();*/
    lastOverflowCycle = (uint32_t)bus.scheduler.currentCycle;
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
    for (int ch = 1; ch <= 2; ch++) {
        auto& dma = bus.dmaController.channels[ch];
       /* dbg << "[APU] checking DMA" << ch
            << " enabled=" << ((dma.cnt >> 31) & 1)
            << " timing=" << ((dma.cnt >> 28) & 0x3)
            << " dad=0x" << std::hex << dma.dad << "\n";
        dbg.flush();*/
        if (!(dma.cnt & (1u << 31))) continue;
        if (((dma.cnt >> 28) & 0x3) != 3) continue;
        if (dma.dad != fifoAddr) continue;
        bus.dmaController.execute(ch, bus);
        break;
    }
}