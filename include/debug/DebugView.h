#pragma once
#include "vendor/raylib.h"
#include "core/bus.h"
#include "core/cpu.h"
#include <cstdint>
#include <deque>
class DebugView {
public:
    DebugView(Bus& b, CPU& c) : bus(b), cpu(c) {}
    void Draw(int ox, int oy, int w, int h);
    bool hasSteppedOnce = false;
private:
    Bus& bus;
    CPU& cpu;
    // ---- Tabs: 0=GFX  1=MEMORY  2=CPU ----
    int activeTab = 2;   // open on CPU by default
    // ---- Memory viewer ----
    uint32_t memViewBase = 0x03000000;
    uint32_t memViewCursor = 0x03000000;
    static constexpr int MEM_COLS = 16;
    char addrInputBuf[12] = "03000000";
    bool addrInputActive = false;
    struct Preset { const char* label; uint32_t addr; };
    static constexpr Preset presets[] = {
        { "IWRAM", 0x03000000 }, { "EWRAM", 0x02000000 },
        { "IO",    0x04000000 }, { "PRAM",  0x05000000 },
        { "VRAM",  0x06000000 }, { "OAM",   0x07000000 },
        { "ROM",   0x08000000 },
    };
    // ---- PC trace ----
    std::deque<uint32_t> pcTrace;

    static constexpr int PC_TRACE_LEN = 24;
    uint32_t lastPC = 0;
    // ---- Internal helpers ----
    void drawTabBar(int x, int y, int w);
    void drawGFXTab(int x, int y, int w, int h);
    void drawMemTab(int x, int y, int w, int h);
    void drawCPUTab(int x, int y, int w, int h);
    void handleMemInput();
    void drawPalette(int x, int y);
    void drawTiles(int x, int y);
    void drawTileMap(int x, int y);
    // small UI primitives
    void drawLabel(int x, int y, const char* label, Color col = { 100,130,180,255 });
    void drawValue(int x, int y, const char* val, Color col = { 220,220,220,255 });
    void drawBitRow(int x, int y, const char* title, uint16_t val,
        int bits, const char* const* names);
    Color gba15ToColor(uint16_t c);
    Color byteColor(uint8_t  v);
    const char* modeName(uint8_t mode);
};