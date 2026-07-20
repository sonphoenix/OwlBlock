#include "video/PPU.h"
#include "core/bus.h"
#include "common/gba_registers.h"
#include <algorithm>
extern std::ofstream dbg;

PPU::PPU(Bus& b) : bus(b), frameBuffer(240 * 160, 0xFF000000) {}

inline uint32_t gba15ToRGBA32(uint16_t col16) {
    uint8_t r5 = col16 & 0x1F;
    uint8_t g5 = (col16 >> 5) & 0x1F;
    uint8_t b5 = (col16 >> 10) & 0x1F;
    return (255u << 24)
        | ((uint8_t)((b5 << 3) | (b5 >> 2)) << 16)
        | ((uint8_t)((g5 << 3) | (g5 >> 2)) << 8)
        | ((uint8_t)((r5 << 3) | (r5 >> 2)));
}

// ---------------------------------------------------------------------------
// composeScanLine
// Reads all layer buffers, finds top two visible layers per pixel,
// applies REG_BLDCNT blend mode, writes final RGBA to frameBuffer.
// ---------------------------------------------------------------------------
void PPU::composeScanLine(uint8_t scanline) {
    static int frameCount = 0;
    if (scanline == 0) frameCount++;

    if (frameCount == 300 && scanline == 80) {
        for (int i = 0; i < 0x4000; i++) {
            uint8_t val = bus.read8(0x0600C000 + i);
            if (val != 0) {
                dbg << "[CB3_FIRST_NONZERO] offset=0x" << std::hex << i
                    << " val=0x" << (int)val << "\n";
                dbg.flush();
                break;
            }
        }
    }
    uint16_t bldcnt = bus.read16(0x04000050);
    uint16_t bldalpha = bus.read16(0x04000052);
    uint16_t bldyReg = bus.read16(0x04000054);

    uint8_t blendMode = (bldcnt >> 6) & 0x3;
    uint8_t topMask = bldcnt & 0x3F;
    uint8_t botMask = (bldcnt >> 8) & 0x3F;
    int     eva = std::min(16, (int)(bldalpha & 0x1F));
    int     evb = std::min(16, (int)((bldalpha >> 8) & 0x1F));
    int     ey = std::min(16, (int)(bldyReg & 0x1F));

    uint8_t bgPrio[4];
    for (int i = 0; i < 4; i++)
        bgPrio[i] = bus.read16(0x04000008 + i * 2) & 0x3;

    uint16_t backdrop = bus.read16(MEM_PRAM) & 0x7FFF;

    for (int x = 0; x < 240; x++) {
        uint8_t  top1Key = 255, top2Key = 255;
        uint8_t  top1ID = 5, top2ID = 5;
        uint16_t top1Col = backdrop, top2Col = backdrop;
        bool     top1Semi = false;

        for (int i = 0; i < 4; i++) {
            if (bgLineBuffer[i][x] == 0x8000) continue;
            uint8_t key = bgPrio[i] * 5 + (uint8_t)i + 1;
            if (key < top1Key) {
                top2Key = top1Key; top2ID = top1ID; top2Col = top1Col;
                top1Key = key; top1ID = (uint8_t)i;
                top1Col = bgLineBuffer[i][x]; top1Semi = false;
            }
            else if (key < top2Key) {
                top2Key = key; top2ID = (uint8_t)i;
                top2Col = bgLineBuffer[i][x];
            }
        }

        if (objLineBuffer[x] != 0x8000) {
            uint8_t key = objPriorityBuffer[x] * 5;
            bool    semi = objSemiTransBuffer[x];
            if (key < top1Key) {
                top2Key = top1Key; top2ID = top1ID; top2Col = top1Col;
                top1Key = key; top1ID = 4;
                top1Col = objLineBuffer[x]; top1Semi = semi;
            }
            else if (key < top2Key) {
                top2Key = key; top2ID = 4;
                top2Col = objLineBuffer[x];
            }
        }

        uint16_t finalCol = top1Col;

        if (top1Semi && top1ID == 4) {
            // semi-transparent sprite — blended with layer below
        }
        else if (blendMode == 1
            && (topMask & (1 << top1ID))
            && (botMask & (1 << top2ID))) {
            int r = std::min(31, ((top1Col & 0x1F) * eva + (top2Col & 0x1F) * evb) / 16);
            int g = std::min(31, (((top1Col >> 5) & 0x1F) * eva + ((top2Col >> 5) & 0x1F) * evb) / 16);
            int b = std::min(31, (((top1Col >> 10) & 0x1F) * eva + ((top2Col >> 10) & 0x1F) * evb) / 16);
            finalCol = (uint16_t)((b << 10) | (g << 5) | r);
        }
        else if (blendMode == 2 && (topMask & (1 << top1ID))) {
            int r = (top1Col & 0x1F) + ((31 - (top1Col & 0x1F)) * ey / 16);
            int g = ((top1Col >> 5) & 0x1F) + ((31 - ((top1Col >> 5) & 0x1F)) * ey / 16);
            int b = ((top1Col >> 10) & 0x1F) + ((31 - ((top1Col >> 10) & 0x1F)) * ey / 16);
            finalCol = (uint16_t)((std::min(31, b) << 10) | (std::min(31, g) << 5) | std::min(31, r));
        }
        else if (blendMode == 3 && (topMask & (1 << top1ID))) {
            int r = (top1Col & 0x1F) * (16 - ey) / 16;
            int g = ((top1Col >> 5) & 0x1F) * (16 - ey) / 16;
            int b = ((top1Col >> 10) & 0x1F) * (16 - ey) / 16;
            finalCol = (uint16_t)((b << 10) | (g << 5) | r);
        }
        if (top1Semi && top1ID == 4) {
            static int count = 0;
            if (count++ < 20) {
                dbg << "[BLEND] x=" << x << " scanline=" << (int)scanline
                    << " top2ID=" << (int)top2ID
                    << " top2Col=0x" << std::hex << top2Col
                    << " BG3=" << bgLineBuffer[3][x] << "\n";
                dbg.flush();
            }
        }
        frameBuffer[scanline * 240 + x] = gba15ToRGBA32(finalCol);
    }
}

// ---------------------------------------------------------------------------
// renderMode0 — tiled BGs 0-3
// ---------------------------------------------------------------------------
void PPU::renderMode0(uint8_t scanline) {
    uint16_t dispcnt = bus.read16(REG_DISPCNT);

    struct BGInfo { int index; uint8_t priority; };
    BGInfo bgs[4];
    int bgCount = 0;

    for (int i = 0; i < 4; i++) {
        if (!(dispcnt & (1 << (i + 8)))) continue;
        uint16_t bgcnt = bus.read16(0x04000008 + i * 2);
        bgs[bgCount++] = { i, (uint8_t)(bgcnt & 0x3) };
    }

    for (int a = 0; a < bgCount - 1; a++)
        for (int b = a + 1; b < bgCount; b++)
            if (bgs[a].priority < bgs[b].priority ||
                (bgs[a].priority == bgs[b].priority && bgs[a].index < bgs[b].index))
                std::swap(bgs[a], bgs[b]);

    bool win0_active = (dispcnt >> 13) & 1;
    bool win1_active = (dispcnt >> 14) & 1;
    bool any_win = win0_active || win1_active;

    uint16_t win0h = bus.win0h_scanline[scanline];
    uint16_t win0v = bus.read16(0x04000044);
    uint8_t  win0_left = win0h >> 8, win0_right = win0h & 0xFF;
    uint8_t  win0_top = win0v >> 8, win0_bot = win0v & 0xFF;

    uint16_t win1h = bus.read16(0x04000042);
    uint16_t win1v = bus.read16(0x04000046);
    uint8_t  win1_left = win1h >> 8, win1_right = win1h & 0xFF;
    uint8_t  win1_top = win1v >> 8, win1_bot = win1v & 0xFF;

    uint16_t winin_reg = bus.read16(0x04000048);
    uint16_t winout_reg = bus.read16(0x0400004A);
    uint8_t  win0_flags = winin_reg & 0x3F;
    uint8_t  win1_flags = (winin_reg >> 8) & 0x3F;
    uint8_t  winout_flags = winout_reg & 0x3F;

    for (int bi = 0; bi < bgCount; bi++) {
        int i = bgs[bi].index;

        uint16_t bgcnt = bus.read16(0x04000008 + i * 2);
        uint8_t  charBlock = (bgcnt >> 2) & 0x3;
        uint8_t  colorMode = (bgcnt >> 7) & 0x1;
        uint8_t  screenBlock = (bgcnt >> 8) & 0x1F;
        uint8_t  screenSize = (bgcnt >> 14) & 0x3;

        uint32_t tileDataBase = MEM_VRAM + charBlock * 0x4000;
        uint16_t scrollX = bus.read16(0x04000010 + i * 4) & 0x1FF;
        uint16_t scrollY = bus.read16(0x04000012 + i * 4) & 0x1FF;
        uint16_t mapWidth = (screenSize & 1) ? 512 : 256;
        uint16_t mapHeight = (screenSize & 2) ? 512 : 256;
        uint16_t effectiveY = (scrollY + scanline) & (mapHeight - 1);
        uint8_t  tileRow = (effectiveY / 8) & 31;
        uint8_t  pixelRow = effectiveY % 8;

        for (int x = 0; x < 240; x++) {
            if (any_win) {
                bool in_win0 = win0_active
                    && (x >= win0_left && x < win0_right)
                    && (scanline >= win0_top && scanline < win0_bot);
                bool in_win1 = !in_win0 && win1_active
                    && (x >= win1_left && x < win1_right)
                    && (scanline >= win1_top && scanline < win1_bot);
                uint8_t mask = in_win0 ? win0_flags
                    : in_win1 ? win1_flags
                    : winout_flags;
                if (!(mask & (1 << i))) continue;
            }

            uint16_t effectiveX = (scrollX + x) & (mapWidth - 1);
            uint8_t  tileCol = (effectiveX / 8) & 31;
            uint8_t  pixelCol = effectiveX % 8;
            uint8_t  blockX = effectiveX >= 256 ? 1 : 0;
            uint8_t  blockY = effectiveY >= 256 ? 1 : 0;

            uint8_t subBlock = screenBlock;
            if (screenSize == 1) subBlock += blockX;
            else if (screenSize == 2) subBlock += blockY;
            else if (screenSize == 3) subBlock += blockX + blockY * 2;

            uint32_t mapBase = MEM_VRAM + subBlock * 0x800;
            uint32_t mapAddr = mapBase + (tileRow * 32 + tileCol) * 2;
            uint16_t mapEntry = bus.read16(mapAddr);

            uint16_t tileIndex = mapEntry & 0x3FF;
            bool     flipH = (mapEntry >> 10) & 0x1;
            bool     flipV = (mapEntry >> 11) & 0x1;
            uint8_t  palette = (mapEntry >> 12) & 0xF;
            uint8_t  col = flipH ? (7 - pixelCol) : pixelCol;
            uint8_t  row = flipV ? (7 - pixelRow) : pixelRow;

            uint16_t color;
            if (colorMode == 0) {
                uint32_t tileAddr = tileDataBase + tileIndex * 32 + row * 4 + col / 2;
                uint8_t  nibbles = bus.read8(tileAddr);
                uint8_t  palIndex = (col & 1) ? (nibbles >> 4) : (nibbles & 0xF);
                if (palIndex == 0) continue;
                color = bus.read16(MEM_PRAM + palette * 32 + palIndex * 2);
            }
            else {
                uint32_t tileAddr = tileDataBase + tileIndex * 64 + row * 8 + col;
                uint8_t  palIndex = bus.read8(tileAddr);
                if (palIndex == 0) continue;
                color = bus.read16(MEM_PRAM + palIndex * 2);
            }

            bgLineBuffer[i][x] = color & 0x7FFF;
        }
    }
}

// ---------------------------------------------------------------------------
// renderMode1 — BG0/BG1 tiled + BG2 affine
// ---------------------------------------------------------------------------
void PPU::renderMode1(uint8_t scanline) {
    uint16_t dispcnt = bus.read16(REG_DISPCNT);

    struct BGInfo { int index; uint8_t priority; };
    BGInfo bgs[3];
    int bgCount = 0;

    for (int i = 0; i < 2; i++) {
        if (!(dispcnt & (1 << (i + 8)))) continue;
        uint16_t bgcnt = bus.read16(0x04000008 + i * 2);
        bgs[bgCount++] = { i, (uint8_t)(bgcnt & 0x3) };
    }
    if (dispcnt & (1 << 10)) {
        uint16_t bgcnt = bus.read16(0x0400000C);
        bgs[bgCount++] = { 2, (uint8_t)(bgcnt & 0x3) };
    }

    for (int a = 0; a < bgCount - 1; a++)
        for (int b = a + 1; b < bgCount; b++)
            if (bgs[a].priority < bgs[b].priority ||
                (bgs[a].priority == bgs[b].priority && bgs[a].index < bgs[b].index))
                std::swap(bgs[a], bgs[b]);

    int16_t  PA = 0, PB = 0, PC = 0, PD = 0;
    int32_t  startX = 0, startY = 0;
    uint8_t  bg2CharBlock = 0, bg2ScreenBlock = 0, bg2ScreenSize = 0;
    bool     bg2Overflow = false;
    uint16_t bg2MapSize = 0;

    if (dispcnt & (1 << 10)) {
        uint16_t bgcnt = bus.read16(0x0400000C);
        bg2CharBlock = (bgcnt >> 2) & 0x3;
        bg2ScreenBlock = (bgcnt >> 8) & 0x1F;
        bg2ScreenSize = (bgcnt >> 14) & 0x3;
        bg2Overflow = (bgcnt >> 13) & 0x1;
        static const uint16_t mapSizes[4] = { 128, 256, 512, 1024 };
        bg2MapSize = mapSizes[bg2ScreenSize];

        PA = (int16_t)bus.read16(0x04000020);
        PB = (int16_t)bus.read16(0x04000022);
        PC = (int16_t)bus.read16(0x04000024);
        PD = (int16_t)bus.read16(0x04000026);

        // Use accumulated reference point instead of recomputing from registers
        startX = bg2RefX;
        startY = bg2RefY;
    }

    for (int bi = 0; bi < bgCount; bi++) {
        int i = bgs[bi].index;

        if (i == 2) {
            uint32_t tileDataBase = MEM_VRAM + bg2CharBlock * 0x4000;
            uint32_t mapBase = MEM_VRAM + bg2ScreenBlock * 0x800;

            for (int x = 0; x < 240; x++) {
                int32_t bgX = (startX + (int32_t)PA * x) >> 8;
                int32_t bgY = (startY + (int32_t)PC * x) >> 8;

                if (bg2Overflow) {
                    bgX = ((bgX % bg2MapSize) + bg2MapSize) % bg2MapSize;
                    bgY = ((bgY % bg2MapSize) + bg2MapSize) % bg2MapSize;
                }
                else {
                    if (bgX < 0 || bgX >= bg2MapSize || bgY < 0 || bgY >= bg2MapSize)
                        continue;
                }

                uint16_t tileX = bgX / 8, tileY = bgY / 8;
                uint8_t  pixX = bgX % 8, pixY = bgY % 8;

                uint8_t tileIndex = bus.read8(mapBase + tileY * (bg2MapSize / 8) + tileX);
                uint8_t palIdx = bus.read8(tileDataBase + tileIndex * 64 + pixY * 8 + pixX);
                if (palIdx == 0) continue;

                bgLineBuffer[2][x] = bus.read16(MEM_PRAM + palIdx * 2) & 0x7FFF;
            }

            // Accumulate reference point for next scanline
            bg2RefX += PB;
            bg2RefY += PD;
        }
        else {
            uint16_t bgcnt = bus.read16(0x04000008 + i * 2);
            uint8_t  charBlock = (bgcnt >> 2) & 0x3;
            uint8_t  colorMode = (bgcnt >> 7) & 0x1;
            uint8_t  screenBlock = (bgcnt >> 8) & 0x1F;
            uint8_t  screenSize = (bgcnt >> 14) & 0x3;

            uint32_t tileDataBase = MEM_VRAM + charBlock * 0x4000;
            uint16_t scrollX = bus.read16(0x04000010 + i * 4) & 0x1FF;
            uint16_t scrollY = bus.read16(0x04000012 + i * 4) & 0x1FF;
            uint16_t mapWidth = (screenSize & 1) ? 512 : 256;
            uint16_t mapHeight = (screenSize & 2) ? 512 : 256;
            uint16_t effectiveY = (scrollY + scanline) & (mapHeight - 1);
            uint8_t  tileRow = (effectiveY / 8) & 31;
            uint8_t  pixelRow = effectiveY % 8;

            for (int x = 0; x < 240; x++) {
                uint16_t effectiveX = (scrollX + x) & (mapWidth - 1);
                uint8_t  tileCol = (effectiveX / 8) & 31;
                uint8_t  pixelCol = effectiveX % 8;
                uint8_t  blockX = effectiveX >= 256 ? 1 : 0;
                uint8_t  blockY = effectiveY >= 256 ? 1 : 0;

                uint8_t subBlock = screenBlock;
                if (screenSize == 1) subBlock += blockX;
                else if (screenSize == 2) subBlock += blockY;
                else if (screenSize == 3) subBlock += blockX + blockY * 2;

                uint32_t mapBase = MEM_VRAM + subBlock * 0x800;
                uint32_t mapAddr = mapBase + (tileRow * 32 + tileCol) * 2;
                uint16_t mapEntry = bus.read16(mapAddr);

                uint16_t tileIndex = mapEntry & 0x3FF;
                bool     flipH = (mapEntry >> 10) & 0x1;
                bool     flipV = (mapEntry >> 11) & 0x1;
                uint8_t  palette = (mapEntry >> 12) & 0xF;
                uint8_t  col = flipH ? (7 - pixelCol) : pixelCol;
                uint8_t  row = flipV ? (7 - pixelRow) : pixelRow;

                uint16_t color;
                if (colorMode == 0) {
                    uint32_t tileAddr = tileDataBase + tileIndex * 32 + row * 4 + col / 2;
                    uint8_t  nibbles = bus.read8(tileAddr);
                    uint8_t  palIndex = (col & 1) ? (nibbles >> 4) : (nibbles & 0xF);
                    if (palIndex == 0) continue;
                    color = bus.read16(MEM_PRAM + palette * 32 + palIndex * 2);
                }
                else {
                    uint32_t tileAddr = tileDataBase + tileIndex * 64 + row * 8 + col;
                    uint8_t  palIndex = bus.read8(tileAddr);
                    if (palIndex == 0) continue;
                    color = bus.read16(MEM_PRAM + palIndex * 2);
                }

                bgLineBuffer[i][x] = color & 0x7FFF;
            }
        }
    }
}


// ---------------------------------------------------------------------------
// renderMode2 — BG2/BG3 affine
// ---------------------------------------------------------------------------
void PPU::renderMode2(uint8_t scanline) {
    uint16_t dispcnt = bus.read16(REG_DISPCNT);

    struct BGInfo { int index; uint8_t priority; };
    BGInfo bgs[2];
    int bgCount = 0;

    for (int bgNum = 2; bgNum <= 3; bgNum++) {
        if (!(dispcnt & (1 << (bgNum + 8)))) continue;
        uint16_t bgcnt = bus.read16(0x04000008 + bgNum * 2);
        bgs[bgCount++] = { bgNum, (uint8_t)(bgcnt & 0x3) };
    }

    for (int a = 0; a < bgCount - 1; a++)
        for (int b = a + 1; b < bgCount; b++)
            if (bgs[a].priority < bgs[b].priority ||
                (bgs[a].priority == bgs[b].priority && bgs[a].index < bgs[b].index))
                std::swap(bgs[a], bgs[b]);

    for (int bi = 0; bi < bgCount; bi++) {
        int bgNum = bgs[bi].index;

        uint16_t bgcnt = bus.read16(0x04000008 + bgNum * 2);
        uint8_t  charBlock = (bgcnt >> 2) & 0x3;
        uint8_t  screenBlock = (bgcnt >> 8) & 0x1F;
        uint8_t  screenSize = (bgcnt >> 14) & 0x3;
        bool     overflow = (bgcnt >> 13) & 0x1;

        static const uint16_t mapSizes[4] = { 128, 256, 512, 1024 };
        uint16_t mapSize = mapSizes[screenSize];

        uint32_t tileDataBase = MEM_VRAM + charBlock * 0x4000;
        uint32_t mapBase = MEM_VRAM + screenBlock * 0x800;

        uint32_t affineBase = (bgNum == 2) ? 0x04000020 : 0x04000030;
        int16_t PA = (int16_t)bus.read16(affineBase + 0);
        int16_t PB = (int16_t)bus.read16(affineBase + 2);
        int16_t PC = (int16_t)bus.read16(affineBase + 4);
        int16_t PD = (int16_t)bus.read16(affineBase + 6);

        int32_t startX = (bgNum == 2) ? bg2RefX : bg3RefX;
        int32_t startY = (bgNum == 2) ? bg2RefY : bg3RefY;

        for (int x = 0; x < 240; x++) {
            int32_t bgX = (startX >> 8) + (((int32_t)PA * x) >> 8);
            int32_t bgY = (startY >> 8) + (((int32_t)PC * x) >> 8);

            if (overflow) {
                bgX = ((bgX % mapSize) + mapSize) % mapSize;
                bgY = ((bgY % mapSize) + mapSize) % mapSize;
            }
            else {
                if (bgX < 0 || bgX >= mapSize || bgY < 0 || bgY >= mapSize)
                    continue;
            }

            uint16_t tileX = bgX / 8, tileY = bgY / 8;
            uint8_t  pixX = bgX % 8, pixY = bgY % 8;

            uint8_t tileIndex = bus.read8(mapBase + tileY * (mapSize / 8) + tileX);
            uint8_t palIdx = bus.read8(tileDataBase + tileIndex * 64 + pixY * 8 + pixX);
            if (palIdx == 0) continue;

            bgLineBuffer[bgNum][x] = bus.read16(MEM_PRAM + palIdx * 2) & 0x7FFF;
        }

        if (bgNum == 3 && scanline == 50) {
            int count = 0;
            for (int x = 0; x < 240; x++)
                if (bgLineBuffer[3][x] != 0x8000) count++;
            dbg << "[BG3_COUNT] scanline=50 count=" << count << "\n";
            dbg.flush();
        }

        if (bgNum == 2) { bg2RefX += PB; bg2RefY += PD; }
        else { bg3RefX += PB; bg3RefY += PD; }
    }
}// ---------------------------------------------------------------------------
// renderMode3 — 15-bit direct bitmap (BG2)
// ---------------------------------------------------------------------------
void PPU::renderMode3(uint8_t scanline) {
    for (int i = 0; i < 240; i++) {
        uint16_t col16 = bus.read16(MEM_VRAM + (scanline * 240 + i) * 2) & 0x7FFF;
        bgLineBuffer[2][i] = col16;
    }
}

// ---------------------------------------------------------------------------
// renderMode4 — 8bpp indexed bitmap (BG2)
// ---------------------------------------------------------------------------
void PPU::renderMode4(uint8_t scanline) {
    uint32_t base = (bus.read16(REG_DISPCNT) & 0x10) ? 0x0600A000 : 0x06000000;
    for (int i = 0; i < 240; i++) {
        uint8_t  index = bus.read8(base + (scanline * 240 + i));
        uint16_t col16 = bus.read16(MEM_PRAM + index * 2) & 0x7FFF;
        bgLineBuffer[2][i] = col16;
    }
}

// ---------------------------------------------------------------------------
// renderMode5 — 15-bit direct bitmap 160x128 (BG2)
// ---------------------------------------------------------------------------
void PPU::renderMode5(uint8_t scanline) {
    bool frame = (bus.read16(REG_DISPCNT)) & 0x10;
    for (int i = 0; i < 240; i++) {
        uint16_t col16;
        if (i >= 160 || scanline >= 128) {
            col16 = bus.read16(MEM_PRAM) & 0x7FFF;
        }
        else {
            uint32_t address = (frame ? 0x0600A000 : 0x06000000) + (scanline * 160 + i) * 2;
            col16 = bus.read16(address) & 0x7FFF;
        }
        bgLineBuffer[2][i] = col16;
    }
}

// ---------------------------------------------------------------------------
// renderSprites
// ---------------------------------------------------------------------------
void PPU::renderSprites(uint8_t scanline) {
    uint16_t dispcnt = bus.read16(REG_DISPCNT);
    uint8_t  mode = dispcnt & 0x7;
    bool     oneDim = (dispcnt >> 6) & 1;

    bool win0_active = (dispcnt >> 13) & 1;
    bool win1_active = (dispcnt >> 14) & 1;
    bool any_win = win0_active || win1_active;

    uint16_t win0h = bus.win0h_scanline[scanline];
    uint16_t win0v = bus.read16(0x04000044);
    uint8_t  win0_left = win0h >> 8, win0_right = win0h & 0xFF;
    uint8_t  win0_top = win0v >> 8, win0_bot = win0v & 0xFF;

    uint16_t win1h = bus.read16(0x04000042);
    uint16_t win1v = bus.read16(0x04000046);
    uint8_t  win1_left = win1h >> 8, win1_right = win1h & 0xFF;
    uint8_t  win1_top = win1v >> 8, win1_bot = win1v & 0xFF;

    uint16_t winin_reg = bus.read16(0x04000048);
    uint16_t winout_reg = bus.read16(0x0400004A);
    uint8_t  win0_flags = winin_reg & 0x3F;
    uint8_t  win1_flags = (winin_reg >> 8) & 0x3F;
    uint8_t  winout_flags = winout_reg & 0x3F;

    uint32_t objBase = (mode >= 3) ? 0x06014000 : 0x06010000;

    static const uint8_t W[3][4] = { {8,16,32,64},{16,32,32,64},{ 8, 8,16,32} };
    static const uint8_t H[3][4] = { {8,16,32,64},{ 8, 8,16,32},{16,32,32,64} };

    struct SpriteOrder { int idx; uint8_t prio; };
    SpriteOrder order[128];
    int count = 0;

    for (int i = 0; i < 128; i++) {
        uint16_t attr0 = bus.read16(0x07000000 + i * 8);
        bool rotScale = (attr0 >> 8) & 1;
        bool disableOrDouble = (attr0 >> 9) & 1;
        if (!rotScale && disableOrDouble) continue;
        uint16_t attr2 = bus.read16(0x07000004 + i * 8);
        order[count++] = { i, (uint8_t)((attr2 >> 10) & 0x3) };
    }

    std::stable_sort(order, order + count, [](const SpriteOrder& a, const SpriteOrder& b) {
        if (a.prio != b.prio) return a.prio > b.prio;
        return a.idx > b.idx;
        });

    for (int oi = 0; oi < count; oi++) {
        int     i = order[oi].idx;
        uint8_t spritePrio = order[oi].prio;

        uint32_t oamAddr = 0x07000000 + i * 8;
        uint16_t attr0 = bus.read16(oamAddr + 0);
        uint16_t attr1 = bus.read16(oamAddr + 2);
        uint16_t attr2 = bus.read16(oamAddr + 4);

        bool rotScale = (attr0 >> 8) & 1;
        bool disableOrDouble = (attr0 >> 9) & 1;
        if (!rotScale && disableOrDouble) continue;

        bool    isAffine = rotScale;
        bool    doubleSize = rotScale && disableOrDouble;
        uint8_t gfxMode = (attr0 >> 10) & 0x3;

        if (gfxMode == 2 || gfxMode == 3) continue;

        uint8_t shape = (attr0 >> 14) & 0x3;
        uint8_t size = (attr1 >> 14) & 0x3;
        if (shape == 3) continue;

        uint8_t w = W[shape][size];
        uint8_t h = H[shape][size];
        int     drawW = doubleSize ? w * 2 : w;
        int     drawH = doubleSize ? h * 2 : h;

        int objY = attr0 & 0xFF;
        int localY = (int)scanline - (int)objY;
        if (localY < 0) localY += 256;
        if (localY < 0 || localY >= drawH) continue;

        int objX = attr1 & 0x1FF;
        if (objX >= 256) objX -= 512;

        bool     flipH = !isAffine && ((attr1 >> 12) & 1);
        bool     flipV = !isAffine && ((attr1 >> 13) & 1);
        bool     is8bpp = (attr0 >> 13) & 1;
        uint16_t tileIdx = attr2 & 0x3FF;
        uint8_t  pal = (attr2 >> 12) & 0xF;

        int16_t PA = 0x100, PB = 0, PC = 0, PD = 0x100;
        if (isAffine) {
            uint8_t matrixNum = (attr1 >> 9) & 0x1F;
            PA = (int16_t)bus.read16(0x07000006 + matrixNum * 32);
            PB = (int16_t)bus.read16(0x0700000E + matrixNum * 32);
            PC = (int16_t)bus.read16(0x07000016 + matrixNum * 32);
            PD = (int16_t)bus.read16(0x0700001E + matrixNum * 32);
        }

        int halfW = drawW / 2;
        int halfH = drawH / 2;
        int dy = localY - halfH;

        for (int px = 0; px < drawW; px++) {
            int screenX = objX + px;
            if (screenX < 0 || screenX >= 240) continue;

            if (any_win) {
                bool in_win0 = win0_active
                    && (screenX >= win0_left && screenX < win0_right)
                    && (scanline >= win0_top && scanline < win0_bot);
                bool in_win1 = !in_win0 && win1_active
                    && (screenX >= win1_left && screenX < win1_right)
                    && (scanline >= win1_top && scanline < win1_bot);
                uint8_t mask = in_win0 ? win0_flags
                    : in_win1 ? win1_flags
                    : winout_flags;
                if (!(mask & (1 << 4))) continue;
            }

            int texX, texY;
            if (isAffine) {
                int dx = px - halfW;
                texX = (PA * dx + PB * dy) >> 8;
                texY = (PC * dx + PD * dy) >> 8;
                texX += w / 2;
                texY += h / 2;
                if (texX < 0 || texX >= w || texY < 0 || texY >= h) continue;
            }
            else {
                texX = flipH ? (w - 1 - px) : px;
                texY = flipV ? (h - 1 - localY) : localY;
            }

            int tileX = texX / 8, pixX = texX % 8;
            int tileY = texY / 8, pixY = texY % 8;

            uint8_t  palIdx;
            uint16_t color;

            if (is8bpp) {
                int tileNum = oneDim
                    ? tileIdx + (tileY * (w / 8) + tileX) * 2
                    : (tileIdx & ~1) + tileY * 32 + tileX * 2;
                uint32_t tileAddr = objBase + tileNum * 32 + pixY * 8 + pixX;
                palIdx = bus.read8(tileAddr);
                if (palIdx == 0) continue;
                color = bus.read16(0x05000200 + palIdx * 2);
            }
            else {
                int tileNum = oneDim
                    ? tileIdx + tileY * (w / 8) + tileX
                    : tileIdx + tileY * 32 + tileX;
                uint32_t tileAddr = objBase + tileNum * 32 + pixY * 4 + pixX / 2;
                uint8_t  nibbles = bus.read8(tileAddr);
                palIdx = (pixX & 1) ? (nibbles >> 4) : (nibbles & 0xF);
                if (palIdx == 0) continue;
                color = bus.read16(0x05000200 + pal * 32 + palIdx * 2);
            }

            objLineBuffer[screenX] = color & 0x7FFF;
            objPriorityBuffer[screenX] = spritePrio;
            objSemiTransBuffer[screenX] = (gfxMode == 1);
        }
    }
}

// ---------------------------------------------------------------------------
// renderScanLine
// ---------------------------------------------------------------------------
void PPU::renderScanLine(uint8_t scanline) {
    // in renderScanLine, after the layer buffer clears
    if (scanline == 0) {
        static int frameCount = 0;
        frameCount++;
        if (frameCount == 120) {
            for (int i = 0; i < 10; i++) {
                uint16_t attr0 = bus.read16(0x07000000 + i * 8);
                uint16_t attr1 = bus.read16(0x07000002 + i * 8);
                uint16_t attr2 = bus.read16(0x07000004 + i * 8);
                int objX = attr1 & 0x1FF; if (objX >= 256) objX -= 512;
                int objY = attr0 & 0xFF;
                dbg << "OAM[" << i << "] X=" << std::dec << objX
                    << " Y=" << objY
                    << " attr0=0x" << std::hex << attr0
                    << " attr1=0x" << attr1
                    << " attr2=0x" << attr2 << "\n";
            }
            dbg.flush();
        }
    }
    uint16_t dispcnt = bus.read16(REG_DISPCNT);
    uint8_t  mode = dispcnt & 0x7;
    bool     forceBlank = (dispcnt >> 7) & 1;

    for (int i = 0; i < 4; i++)
        std::fill(bgLineBuffer[i], bgLineBuffer[i] + 240, (uint16_t)0x8000);
    std::fill(objLineBuffer, objLineBuffer + 240, (uint16_t)0x8000);
    std::fill(objPriorityBuffer, objPriorityBuffer + 240, (uint8_t)4);
    std::fill(objSemiTransBuffer, objSemiTransBuffer + 240, false);

    if (forceBlank) {
        for (int x = 0; x < 240; x++)
            frameBuffer[scanline * 240 + x] = 0xFFFFFFFF;
        return;
    }

    static bool mapDumped = false;
    static int frameCount2 = 0;
    if (scanline == 0) frameCount2++;
    if (!mapDumped && frameCount2 == 180) {
        mapDumped = true;
        uint32_t mapBase = 0x06000000 + 23 * 0x800;
        for (int i = 0; i < 32 * 32; i++) {
            uint16_t entry = bus.read16(mapBase + i * 2);
            if (entry != 0)
                dbg << "[MAP3] row=" << (i / 32) << " col=" << (i % 32)
                << " entry=0x" << std::hex << entry << "\n";
        }
        dbg.flush();
    }

    switch (mode) {
    case 0: renderMode0(scanline); break;
    case 1: renderMode1(scanline); break;
    case 2: renderMode2(scanline); break;
    case 3: renderMode3(scanline); break;
    case 4: renderMode4(scanline); break;
    case 5: renderMode5(scanline); break;
    }

    if (dispcnt & (1 << 12))
        renderSprites(scanline);

    composeScanLine(scanline);
}