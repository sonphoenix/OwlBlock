#include "PPU.h"
#include "bus.h"
#include "gba_registers.h"
#include <algorithm>
extern std::ofstream dbg;

PPU::PPU(Bus& b) : bus(b), frameBuffer(240 * 160, 0xFF000000) {

}


inline uint32_t gba15ToRGBA32(uint16_t col16) {
    uint8_t r5 = col16 & 0x1F;
    uint8_t g5 = (col16 >> 5) & 0x1F;
    uint8_t b5 = (col16 >> 10) & 0x1F;
    return (255u << 24)
        | ((uint8_t)((b5 << 3) | (b5 >> 2)) << 16)
        | ((uint8_t)((g5 << 3) | (g5 >> 2)) << 8)
        | ((uint8_t)((r5 << 3) | (r5 >> 2)));
}



void PPU::renderMode0(uint8_t scanline) {

    uint16_t dispcnt = bus.read16(REG_DISPCNT);

    if (dispcnt & (1 << 7)) {
        for (int x = 0; x < 240; x++)
            frameBuffer[scanline * 240 + x] = 0xFFFFFFFF;
        return;
    }

    uint16_t backdrop = bus.read16(MEM_PRAM);
    for (int x = 0; x < 240; x++)
        frameBuffer[scanline * 240 + x] = gba15ToRGBA32(backdrop);

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
            if (bgs[a].priority < bgs[b].priority)
                std::swap(bgs[a], bgs[b]);

    // ---- Window setup (once per scanline) ----
    bool win0_active = (dispcnt >> 13) & 1;
    bool win1_active = (dispcnt >> 14) & 1;
    bool any_win = win0_active || win1_active;

    uint16_t win0h = bus.win0h_scanline[scanline];
    uint16_t win0v = bus.read16(0x04000044);
    uint8_t  win0_left = win0h >> 8;
    uint8_t  win0_right = win0h & 0xFF;
    uint8_t  win0_top = win0v >> 8;
    uint8_t  win0_bot = win0v & 0xFF;
    uint8_t  winin = bus.read16(0x04000048) & 0x3F;
    uint8_t  winout = bus.read16(0x0400004A) & 0x3F;

    if (scanline == 80) {
        static int logCount = 0;
        if (logCount++ % 60 == 0) {
            dbg << "[WIN0H_80]=0x" << std::hex << win0h
                << " left=" << std::dec << (int)win0_left
                << " right=" << (int)win0_right
                << " winin=0x" << std::hex << (int)winin
                << " winout=0x" << (int)winout << "\n";
            dbg.flush();
        }
    }
    // ------------------------------------------

    for (int bi = 0; bi < bgCount; bi++) {
        int      i = bgs[bi].index;
        uint8_t  bgPrio = bgs[bi].priority;
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
            // ---- Per-pixel window check ----
            if (any_win) {
                dbg << "window check saar \n"; dbg.flush();
                bool inside = win0_active
                    && (x >= win0_left && x < win0_right)
                    && (scanline >= win0_top && scanline < win0_bot);
                uint8_t mask = inside ? winin : winout;

                if (scanline == 80 && x == 120 && i == 1) {
                    static int n = 0;
                    if (n++ < 5) {
                        dbg << "[WINCHECK] BG1 x=120 sl=80"
                            << " inside=" << inside
                            << " mask=0x" << std::hex << (int)mask
                            << " bit=" << (int)(mask & (1 << i))
                            << " win0_active=" << win0_active
                            << " left=" << std::dec << (int)win0_left
                            << " right=" << (int)win0_right << "\n";
                        dbg.flush();
                    }
                }

                if (!(mask & (1 << i))) continue;
            }
            // --------------------------------

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

            frameBuffer[scanline * 240 + x] = gba15ToRGBA32(color);
            bgPrioBuffer[x] = bgPrio;

            // Debug: log BG priority at x=120 on scanline 80
            if (scanline == 80 && x == 120) {
                static int n = 0;
                if (n++ < 5)
                    dbg << "[BGPRIO_AT_120] bg=" << i
                    << " prio=" << (int)bgPrio << "\n";
                dbg.flush();
            }
        }
    }
}


void PPU::renderMode3(uint8_t scanline) {
    for (int i = 0; i < 240; i++) {
        uint32_t address = MEM_VRAM + (scanline * 240 + i) * 2;
        uint16_t col16 = bus.read16(address);

        uint8_t b5 = (col16 >> 10) & 0x1F;
        uint8_t g5 = (col16 >> 5) & 0x1F;
        uint8_t r5 = col16 & 0x1F;

        uint8_t b8 = (b5 << 3) | (b5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t r8 = (r5 << 3) | (r5 >> 2);

        frameBuffer[240 * scanline + i] = (255u << 24) | (b8 << 16) | (g8 << 8) | r8;
        // Mode 3 is BG2 (bitmap); treat as priority 3 so sprites can draw over it
        bgPrioBuffer[i] = 3;                           // <-- record BG priority
    }
}


void PPU::renderMode4(uint8_t scanline) {

    uint32_t base = (bus.read16(REG_DISPCNT) & 0x10) ? 0x0600A000 : 0x06000000;

    for (int i = 0; i < 240; i++) {
        if (scanline == 0 && i == 0) {
            dbg << "[PRAM0] = 0x" << std::hex << bus.read16(0x05000000) << "\n";
            dbg.flush();
        }
        uint8_t  index = bus.read8(base + (scanline * 240 + i));
        uint16_t col16 = bus.read16(MEM_PRAM + index * 2);

        uint8_t b5 = (col16 >> 10) & 0x1F;
        uint8_t g5 = (col16 >> 5) & 0x1F;
        uint8_t r5 = col16 & 0x1F;

        uint8_t b8 = (b5 << 3) | (b5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t r8 = (r5 << 3) | (r5 >> 2);

        frameBuffer[240 * scanline + i] = (255u << 24) | (b8 << 16) | (g8 << 8) | r8;
        bgPrioBuffer[i] = 3;                           // <-- record BG priority
    }
}


void PPU::renderMode5(uint8_t scanline) {
    bool frame = (bus.read16(REG_DISPCNT)) & 0x10;

    for (int i = 0; i < 240; i++) {
        uint16_t col16;
        if (i >= 160 || scanline >= 128) {
            col16 = bus.read16(MEM_PRAM);
        }
        else {
            uint32_t address = (frame ? 0x0600A000 : 0x06000000) + (scanline * 160 + i) * 2;
            col16 = bus.read16(address);
        }

        uint8_t b5 = (col16 >> 10) & 0x1F;
        uint8_t g5 = (col16 >> 5) & 0x1F;
        uint8_t r5 = col16 & 0x1F;

        uint8_t b8 = (b5 << 3) | (b5 >> 2);
        uint8_t g8 = (g5 << 3) | (g5 >> 2);
        uint8_t r8 = (r5 << 3) | (r5 >> 2);

        frameBuffer[240 * scanline + i] = (255u << 24) | (b8 << 16) | (g8 << 8) | r8;
        bgPrioBuffer[i] = 3;                           // <-- record BG priority
    }
}

void PPU::renderSprites(uint8_t scanline) {
    uint16_t dispcnt = bus.read16(REG_DISPCNT);
    uint8_t  mode = dispcnt & 0x7;
    bool     oneDim = (dispcnt >> 6) & 1;

    uint32_t objBase = (mode >= 3) ? 0x06014000 : 0x06010000;

    static const uint8_t W[3][4] = { {8,16,32,64},{16,32,32,64},{ 8, 8,16,32} };
    static const uint8_t H[3][4] = { {8,16,32,64},{ 8, 8,16,32},{16,32,32,64} };

    struct SpriteOrder { int idx; uint8_t prio; };
    SpriteOrder order[128];
    int count = 0;

    for (int i = 0; i < 128; i++) {
        uint16_t attr0 = bus.read16(0x07000000 + i * 8);
        if (((attr0 >> 8) & 0x3) == 2) continue;
        uint16_t attr2 = bus.read16(0x07000004 + i * 8);
        order[count++] = { i, (uint8_t)((attr2 >> 10) & 0x3) };
    }

    std::stable_sort(order, order + count, [](const SpriteOrder& a, const SpriteOrder& b) {
        if (a.prio != b.prio) return a.prio > b.prio;
        return a.idx > b.idx;
        });

    // Debug: log all visible sprites on scanline 80 with their priorities
    {
        static int firstScanlineLogged = -1;
        bool hasVisible = false;
        for (int oi = 0; oi < count && !hasVisible; oi++) {
            int i = order[oi].idx;
            uint16_t attr0 = bus.read16(0x07000000 + i * 8);
            uint16_t attr1 = bus.read16(0x07000002 + i * 8);
            uint8_t shape = (attr0 >> 14) & 0x3;
            uint8_t size = (attr1 >> 14) & 0x3;
            if (shape == 3) continue;
            int objY = attr0 & 0xFF;
            int localY = (int)scanline - (int)objY;      // FIXED
            if (localY < 0) localY += 256;               // FIXED
            int drawH = H[shape][size];
            uint8_t objMode = (attr0 >> 8) & 0x3;
            if (objMode == 3) drawH *= 2;
            if (localY >= 0 && localY < drawH) hasVisible = true;  // FIXED
        }

        if (hasVisible && firstScanlineLogged != (int)scanline) {
            static int totalLogs = 0;
            if (totalLogs++ < 3) {
                firstScanlineLogged = scanline;
                dbg << "[SPRPRIO_DUMP] scanline=" << (int)scanline
                    << " draw order (last=on top):\n";
                for (int oi = 0; oi < count; oi++) {
                    int i = order[oi].idx;
                    uint16_t attr0 = bus.read16(0x07000000 + i * 8);
                    uint16_t attr1 = bus.read16(0x07000002 + i * 8);
                    uint8_t shape = (attr0 >> 14) & 0x3;
                    uint8_t size = (attr1 >> 14) & 0x3;
                    if (shape == 3) continue;
                    int objY = attr0 & 0xFF;
                    int objX = attr1 & 0x1FF;
                    if (objX >= 240) objX -= 512;
                    int localY = (int)scanline - (int)objY;   // FIXED
                    if (localY < 0) localY += 256;            // FIXED
                    int drawH = H[shape][size];
                    uint8_t objMode = (attr0 >> 8) & 0x3;
                    if (objMode == 3) drawH *= 2;
                    if (localY < 0 || localY >= drawH) continue;  // FIXED
                    dbg << "  draw_order=" << oi
                        << " oam_idx=" << i
                        << " prio=" << (int)order[oi].prio
                        << " X=" << objX << " Y=" << objY
                        << " w=" << (int)W[shape][size]
                        << " h=" << (int)H[shape][size] << "\n";
                }
                dbg.flush();
            }
        }
    }

    for (int oi = 0; oi < count; oi++) {
        int     i = order[oi].idx;
        uint8_t spritePrio = order[oi].prio;

        uint32_t oamAddr = 0x07000000 + i * 8;
        uint16_t attr0 = bus.read16(oamAddr + 0);
        uint16_t attr1 = bus.read16(oamAddr + 2);
        uint16_t attr2 = bus.read16(oamAddr + 4);

        uint8_t objMode = (attr0 >> 8) & 0x3;
        if (objMode == 2) continue;

        uint8_t shape = (attr0 >> 14) & 0x3;
        uint8_t size = (attr1 >> 14) & 0x3;
        if (shape == 3) continue;

        uint8_t w = W[shape][size];
        uint8_t h = H[shape][size];

        bool isAffine = (objMode == 1 || objMode == 3);
        bool doubleSize = (objMode == 3);

        int drawW = doubleSize ? w * 2 : w;
        int drawH = doubleSize ? h * 2 : h;

        int objY = attr0 & 0xFF;
        int localY = (int)scanline - (int)objY;   // FIXED
        if (localY < 0) localY += 256;            // FIXED
        if (localY < 0 || localY >= drawH) continue;  // FIXED

        int objX = attr1 & 0x1FF;
        if (objX >= 240) objX -= 512;

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

            int tileX = texX / 8;
            int tileY = texY / 8;
            int pixX = texX % 8;
            int pixY = texY % 8;

            uint8_t  palIdx;
            uint16_t color;

            if (is8bpp) {
                int tileNum;
                if (oneDim)
                    tileNum = tileIdx + (tileY * (w / 8) + tileX) * 2;
                else
                    tileNum = (tileIdx & ~1) + tileY * 32 + tileX * 2;

                uint32_t tileAddr = objBase + tileNum * 32 + pixY * 8 + pixX;
                palIdx = bus.read8(tileAddr);
                if (palIdx == 0) continue;
                color = bus.read16(0x05000200 + palIdx * 2);
            }
            else {
                int tileNum;
                if (oneDim)
                    tileNum = tileIdx + tileY * (w / 8) + tileX;
                else
                    tileNum = tileIdx + tileY * 32 + tileX;

                uint32_t tileAddr = objBase + tileNum * 32 + pixY * 4 + pixX / 2;
                uint8_t  nibbles = bus.read8(tileAddr);
                palIdx = (pixX & 1) ? (nibbles >> 4) : (nibbles & 0xF);
                if (palIdx == 0) continue;
                color = bus.read16(0x05000200 + pal * 32 + palIdx * 2);
            }

            if (spritePrio <= bgPrioBuffer[screenX])
                frameBuffer[scanline * 240 + screenX] = gba15ToRGBA32(color);
        }
    }
}
void PPU::renderMode2(uint8_t scanline) {
    uint16_t dispcnt = bus.read16(REG_DISPCNT);
    if (dispcnt & (1 << 7)) {
        for (int x = 0; x < 240; x++)
            frameBuffer[scanline * 240 + x] = 0xFFFFFFFF;
        return;
    }
    uint16_t backdrop = bus.read16(MEM_PRAM);
    for (int x = 0; x < 240; x++)
        frameBuffer[scanline * 240 + x] = gba15ToRGBA32(backdrop);

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
            if (bgs[a].priority < bgs[b].priority)
                std::swap(bgs[a], bgs[b]);

    for (int bi = 0; bi < bgCount; bi++) {
        int     bgNum = bgs[bi].index;
        uint8_t bgPrio = bgs[bi].priority;            // <-- capture priority

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

        uint32_t refBase = (bgNum == 2) ? 0x04000028 : 0x04000038;
        int32_t refX = (int32_t)bus.read32(refBase + 0);
        int32_t refY = (int32_t)bus.read32(refBase + 4);
        if (refX & 0x08000000) refX |= 0xF0000000; else refX &= 0x0FFFFFFF;
        if (refY & 0x08000000) refY |= 0xF0000000; else refY &= 0x0FFFFFFF;

        int32_t startX = refX + (int32_t)PB * scanline;
        int32_t startY = refY + (int32_t)PD * scanline;

        if (scanline == 80 && bgNum == 3) {
            static int logCount = 0;
            if (logCount++ % 600 == 0) {
                dbg << "[BG3_RENDER] charBlock=" << (int)charBlock
                    << " screenBlock=" << (int)screenBlock
                    << " mapSize=" << std::dec << mapSize
                    << " mapBase=0x" << std::hex << mapBase
                    << " tileDataBase=0x" << tileDataBase << "\n";
                dbg << "[MAP_DATA] [0]=0x" << (int)bus.read8(mapBase)
                    << " [1]=0x" << (int)bus.read8(mapBase + 1)
                    << " [2]=0x" << (int)bus.read8(mapBase + 2) << "\n";
                dbg << "[TILE_DATA] tile1[0]=0x" << (int)bus.read8(tileDataBase + 64)
                    << " tile1[1]=0x" << (int)bus.read8(tileDataBase + 65) << "\n";
                dbg.flush();
            }
        }

        for (int x = 0; x < 240; x++) {
            int32_t bgX = (startX + (int32_t)PA * x) >> 8;
            int32_t bgY = (startY + (int32_t)PC * x) >> 8;

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

            uint16_t color = bus.read16(MEM_PRAM + palIdx * 2);
            frameBuffer[scanline * 240 + x] = gba15ToRGBA32(color);
            bgPrioBuffer[x] = bgPrio;                  // <-- record BG priority
        }
    }
}

void PPU::renderMode1(uint8_t scanline) {
    uint16_t dispcnt = bus.read16(REG_DISPCNT);

    if (dispcnt & (1 << 7)) {
        for (int x = 0; x < 240; x++)
            frameBuffer[scanline * 240 + x] = 0xFFFFFFFF;
        return;
    }

    uint16_t backdrop = bus.read16(MEM_PRAM);
    for (int x = 0; x < 240; x++)
        frameBuffer[scanline * 240 + x] = gba15ToRGBA32(backdrop);

    struct BGInfo { int index; uint8_t priority; };
    BGInfo bgs[2];
    int bgCount = 0;

    for (int i = 0; i < 2; i++) {
        if (!(dispcnt & (1 << (i + 8)))) continue;
        uint16_t bgcnt = bus.read16(0x04000008 + i * 2);
        bgs[bgCount++] = { i, (uint8_t)(bgcnt & 0x3) };
    }

    for (int a = 0; a < bgCount - 1; a++)
        for (int b = a + 1; b < bgCount; b++)
            if (bgs[a].priority < bgs[b].priority)
                std::swap(bgs[a], bgs[b]);

    for (int bi = 0; bi < bgCount; bi++) {
        int      i = bgs[bi].index;
        uint8_t  bgPrio = bgs[bi].priority;            // <-- capture priority
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

            uint8_t blockX = effectiveX >= 256 ? 1 : 0;
            uint8_t blockY = effectiveY >= 256 ? 1 : 0;

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

            uint8_t col = flipH ? (7 - pixelCol) : pixelCol;
            uint8_t row = flipV ? (7 - pixelRow) : pixelRow;

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

            frameBuffer[scanline * 240 + x] = gba15ToRGBA32(color);
            bgPrioBuffer[x] = bgPrio;                  // <-- record BG priority
        }
    }

    // Render BG2 as affine layer if enabled
    if (dispcnt & (1 << 10)) {
        uint16_t bgcnt = bus.read16(0x0400000C);
        uint8_t  charBlock = (bgcnt >> 2) & 0x3;
        uint8_t  screenBlock = (bgcnt >> 8) & 0x1F;
        uint8_t  screenSize = (bgcnt >> 14) & 0x3;
        bool     overflow = (bgcnt >> 13) & 0x1;
        uint8_t  bgPrio = bgcnt & 0x3;             // <-- BG2 priority

        static const uint16_t mapSizes[4] = { 128, 256, 512, 1024 };
        uint16_t mapSize = mapSizes[screenSize];

        uint32_t tileDataBase = MEM_VRAM + charBlock * 0x4000;
        uint32_t mapBase = MEM_VRAM + screenBlock * 0x800;

        int16_t PA = (int16_t)bus.read16(0x04000020);
        int16_t PB = (int16_t)bus.read16(0x04000022);
        int16_t PC = (int16_t)bus.read16(0x04000024);
        int16_t PD = (int16_t)bus.read16(0x04000026);

        int32_t refX = (int32_t)bus.read32(0x04000028);
        int32_t refY = (int32_t)bus.read32(0x0400002C);
        if (refX & 0x08000000) refX |= 0xF0000000; else refX &= 0x0FFFFFFF;
        if (refY & 0x08000000) refY |= 0xF0000000; else refY &= 0x0FFFFFFF;

        int32_t startX = refX + (int32_t)PB * scanline;
        int32_t startY = refY + (int32_t)PD * scanline;

        for (int x = 0; x < 240; x++) {
            int32_t bgX = (startX + (int32_t)PA * x) >> 8;
            int32_t bgY = (startY + (int32_t)PC * x) >> 8;

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

            uint16_t color = bus.read16(MEM_PRAM + palIdx * 2);
            frameBuffer[scanline * 240 + x] = gba15ToRGBA32(color);
            bgPrioBuffer[x] = bgPrio;                  // <-- record BG priority
        }
    }
}

void PPU::renderScanLine(uint8_t scanline) {
    uint16_t dispcnt = bus.read16(REG_DISPCNT);
    uint8_t  mode = dispcnt & 0x7;
    bool     forceBlank = (dispcnt >> 7) & 1;

    // Initialise priority buffer to 4 (= backdrop).
    // Sprites always draw over the backdrop; BG pixels update this as they are written.
    std::fill(bgPrioBuffer, bgPrioBuffer + 240, (uint8_t)4);   // <-- init priority buffer

    if (scanline == 0) {
        static bool done = false;
        if (!done) {
            done = true;
            dbg << "[TILEMAP_SCAN]\n";
            for (int sb = 0; sb < 32; sb++) {
                uint32_t base = MEM_VRAM + sb * 0x800;
                for (int j = 0; j < 0x800; j++) {
                    if (bus.read8(base + j) != 0) {
                        dbg << "  screenBlock=" << sb
                            << " first nonzero at offset=" << j
                            << " val=0x" << std::hex << (int)bus.read8(base + j) << "\n";
                        break;
                    }
                }
            }
            dbg.flush();
        }
    }

    if (scanline == 80) {
        for (int i = 0; i < 128; i++) {
            uint16_t attr0 = bus.read16(0x07000000 + i * 8);
            uint16_t attr1 = bus.read16(0x07000002 + i * 8);
            uint16_t attr2 = bus.read16(0x07000004 + i * 8);
            uint8_t objMode = (attr0 >> 8) & 0x3;
            if (objMode == 2) continue;
            int objY = attr0 & 0xFF;
            int objX = attr1 & 0x1FF;
            if (objX >= 240) objX -= 512;
            uint8_t shape = (attr0 >> 14) & 0x3;
            uint8_t size = (attr1 >> 14) & 0x3;
            static const uint8_t W[3][4] = { {8,16,32,64},{16,32,32,64},{8,8,16,32} };
            static const uint8_t H[3][4] = { {8,16,32,64},{8,8,16,32},{16,32,32,64} };
            if (shape == 3) continue;
            uint8_t h = H[shape][size];
            int localY = ((int)scanline - objY + 256) & 0xFF;
            if (localY >= h) continue;
            dbg << "[SPR_ON_80] i=" << i
                << " X=" << objX << " Y=" << objY
                << " localY=" << localY
                << " shape=" << (int)shape
                << " size=" << (int)size
                << " tileIdx=" << (int)(attr2 & 0x3FF)
                << " pal=" << (int)((attr2 >> 12) & 0xF)
                << " attr0=0x" << std::hex << attr0
                << " attr1=0x" << attr1 << "\n";
            dbg.flush();
            break;
        }
    }

    if (forceBlank) {
        for (int x = 0; x < 240; x++)
            frameBuffer[scanline * 240 + x] = 0xFFFFFFFF;
        return;
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
}