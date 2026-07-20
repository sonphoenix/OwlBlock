// DebugView.cpp
#include "debug/DebugView.h"
#include "debug/Disassembler.h"
#include <cstdio>
#include <cstring>
#include <cctype>

// ============================================================
//  Colour helpers
// ============================================================
Color DebugView::gba15ToColor(uint16_t c) {
    uint8_t r = ((c & 0x1F) << 3) | ((c & 0x1F) >> 2);
    uint8_t g = (((c >> 5) & 0x1F) << 3) | (((c >> 5) & 0x1F) >> 2);
    uint8_t b = (((c >> 10) & 0x1F) << 3) | (((c >> 10) & 0x1F) >> 2);
    return { r, g, b, 255 };
}
Color DebugView::byteColor(uint8_t v) {
    if (v == 0)   return { 40,  40,  40, 255 };
    if (v < 0x20) return { 50, 160,  80, 255 };
    if (v < 0x80) return { 100, 220, 120, 255 };
    if (v < 0xC0) return { 230, 220,  80, 255 };
    return              { 255,  90,  70, 255 };
}
const char* DebugView::modeName(uint8_t m) {
    switch (m) {
    case 0x10: return "USR";
    case 0x11: return "FIQ";
    case 0x12: return "IRQ";
    case 0x13: return "SVC";
    case 0x17: return "ABT";
    case 0x1B: return "UND";
    case 0x1F: return "SYS";
    default:   return "???";
    }
}

// ============================================================
//  Small UI primitives
// ============================================================
void DebugView::drawLabel(int x, int y, const char* label, Color col) {
    DrawText(label, x, y, 8, col);
}
void DebugView::drawValue(int x, int y, const char* val, Color col) {
    DrawText(val, x, y, 8, col);
}

// Draw a row of individual bits with names underneath
void DebugView::drawBitRow(int x, int y, const char* title,
    uint16_t val, int bits,
    const char* const* names) {
    DrawText(title, x, y, 8, { 100, 130, 180, 255 });
    int bx = x + MeasureText(title, 8) + 6;
    for (int i = bits - 1; i >= 0; i--) {
        bool set = (val >> i) & 1;
        Color bg = set ? Color{ 50,160,80,255 } : Color{ 30,35,50,255 };
        Color fg = set ? WHITE : Color{ 70,80,100,255 };
        DrawRectangle(bx, y - 1, 18, 10, bg);
        char b[3]; snprintf(b, sizeof(b), "%d", set ? 1 : 0);
        DrawText(b, bx + 5, y, 8, fg);
        if (names && names[i])
            DrawText(names[i], bx, y + 10, 6, set ? Color{ 140,220,140,255 } : Color{ 60,70,90,255 });
        bx += 21;
    }
}

// ============================================================
//  GFX sub-panels
// ============================================================
void DebugView::drawPalette(int x, int y) {
    DrawText("BG PAL", x, y, 8, WHITE);
    for (int i = 0; i < 256; i++) {
        uint16_t c = bus.read16(0x05000000 + i * 2);
        DrawRectangle(x + (i % 16) * 8, y + 10 + (i / 16) * 8, 8, 8, gba15ToColor(c));
    }
    DrawText("OBJ PAL", x, y + 150, 8, WHITE);
    for (int i = 0; i < 256; i++) {
        uint16_t c = bus.read16(0x05000200 + i * 2);
        DrawRectangle(x + (i % 16) * 8, y + 160 + (i / 16) * 8, 8, 8, gba15ToColor(c));
    }
}
void DebugView::drawTiles(int x, int y) {
    DrawText("TILES (CB0 4bpp)", x, y, 8, WHITE);
    for (int t = 0; t < 128; t++) {
        int tx = x + (t % 16) * 9, ty = y + 10 + (t / 16) * 9;
        for (int row = 0; row < 8; row++)
            for (int col = 0; col < 8; col++) {
                uint8_t nib = bus.read8(0x06000000 + t * 32 + row * 4 + col / 2);
                uint8_t palIdx = (col & 1) ? (nib >> 4) : (nib & 0xF);
                uint16_t c = bus.read16(0x05000000 + palIdx * 2);
                DrawPixel(tx + col, ty + row, palIdx == 0 ? BLACK : gba15ToColor(c));
            }
    }
}
void DebugView::drawTileMap(int x, int y) {
    uint16_t bgcnt = bus.read16(0x04000008);
    uint8_t  sb = (bgcnt >> 8) & 0x1F;
    uint8_t  cb = (bgcnt >> 2) & 0x3;
    uint8_t  colorMode = (bgcnt >> 7) & 0x1;
    char buf[64];
    snprintf(buf, sizeof(buf), "BG0CNT=0x%04X SB=%d CB=%d", bgcnt, sb, cb);
    DrawText(buf, x, y, 8, WHITE);
    uint32_t mapBase = 0x06000000 + sb * 0x800;
    uint32_t tileBase = 0x06000000 + cb * 0x4000;
    for (int ty = 0;ty < 32;ty++) for (int tx = 0;tx < 32;tx++) {
        uint16_t entry = bus.read16(mapBase + (ty * 32 + tx) * 2);
        uint16_t tileIdx = entry & 0x3FF;
        uint8_t  palette = (entry >> 12) & 0xF;
        Color    col = BLACK;
        if (tileIdx) {
            if (!colorMode) {
                uint8_t nib = bus.read8(tileBase + tileIdx * 32);
                uint8_t pi = nib & 0xF;
                col = pi ? gba15ToColor(bus.read16(0x05000000 + palette * 32 + pi * 2))
                    : Color{ 40,40,40,255 };
            }
            else {
                uint8_t pi = bus.read8(tileBase + tileIdx * 64);
                if (pi) col = gba15ToColor(bus.read16(0x05000000 + pi * 2));
            }
        }
        DrawRectangle(x + tx * 5, y + 12 + ty * 5, 4, 4, col);
    }
}

// ============================================================
//  Tab bar
// ============================================================
void DebugView::drawTabBar(int x, int y, int w) {
    static const char* names[] = { "GFX", "MEMORY", "CPU" };
    DrawRectangle(x, y, w, 20, { 15,15,25,255 });
    int tx = x + 4;
    for (int i = 0; i < 3; i++) {
        int tw = MeasureText(names[i], 9) + 16;
        bool active = (activeTab == i);
        DrawRectangle(tx, y + 1, tw, 18,
            active ? Color{ 50,110,200,255 } : Color{ 28,32,48,255 });
        DrawText(names[i], tx + 8, y + 5, 9,
            active ? WHITE : Color{ 110,130,170,255 });
        if (!active &&
            CheckCollisionPointRec(GetMousePosition(),
                { (float)tx,(float)(y + 1),(float)tw,18 }) &&
            IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
            activeTab = i;
        tx += tw + 2;
    }
    DrawLine(x, y + 19, x + w, y + 19, { 50,55,80,255 });
}

// ============================================================
//  GFX tab
// ============================================================
void DebugView::drawGFXTab(int x, int y, int w, int h) {
    drawPalette(x + 4, y + 4);
    drawTiles(x + 140, y + 4);
    drawTileMap(x + 4, y + 295);
}

// ============================================================
//  Memory viewer input
// ============================================================
void DebugView::handleMemInput() {
    if (activeTab != 1) return;
    if (addrInputActive) {
        int key = GetCharPressed();
        while (key > 0) {
            if (isxdigit(key)) {
                int len = (int)strlen(addrInputBuf);
                if (len < 8) { addrInputBuf[len] = (char)toupper(key); addrInputBuf[len + 1] = '\0'; }
            }
            key = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            int len = (int)strlen(addrInputBuf);
            if (len > 0) addrInputBuf[len - 1] = '\0';
        }
        if (IsKeyPressed(KEY_ENTER)) {
            uint32_t addr = 0;
            (void)sscanf_s(addrInputBuf, "%X", &addr);
            memViewBase = addr & ~0xFu; memViewCursor = addr; addrInputActive = false;
        }
        if (IsKeyPressed(KEY_ESCAPE)) addrInputActive = false;
    }
    else {
        if (IsKeyPressed(KEY_G)) { memset(addrInputBuf, 0, sizeof(addrInputBuf)); addrInputActive = true; }
        if (IsKeyPressed(KEY_DOWN))      memViewBase += MEM_COLS;
        if (IsKeyPressed(KEY_UP) && memViewBase >= (uint32_t)MEM_COLS) memViewBase -= MEM_COLS;
        if (IsKeyPressed(KEY_PAGE_DOWN)) memViewBase += MEM_COLS * 20;
        if (IsKeyPressed(KEY_PAGE_UP) && memViewBase >= (uint32_t)(MEM_COLS * 20)) memViewBase -= MEM_COLS * 20;
    }
}

// ============================================================
//  Memory tab
// ============================================================
void DebugView::drawMemTab(int x, int y, int w, int h) {
    const int F = 9, LH = 13, PAD = 6;
    const int ADDR_W = 75, BYTE_W = 22, HEX_W = MEM_COLS * BYTE_W;
    const int ASCII_X = ADDR_W + HEX_W + 8;
    int cx = x + PAD, cy = y + PAD;

    // preset buttons
    int bx = cx;
    for (auto& p : presets) {
        int bw = MeasureText(p.label, F) + 10;
        bool hover = CheckCollisionPointRec(GetMousePosition(), { (float)bx,(float)cy,(float)bw,(float)LH });
        DrawRectangle(bx, cy, bw, LH, hover ? Color{ 60,110,180,255 } : Color{ 30,36,55,255 });
        DrawText(p.label, bx + 5, cy + 2, F, hover ? WHITE : Color{ 130,160,210,255 });
        if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
            memViewBase = p.addr; memViewCursor = p.addr;
            snprintf(addrInputBuf, sizeof(addrInputBuf), "%08X", p.addr);
        }
        bx += bw + 3;
    }
    cy += LH + 4;

    // goto box
    char lbl[28]; snprintf(lbl, sizeof(lbl), "GOTO: %s%s", addrInputBuf, addrInputActive ? "_" : "  [G]");
    int bw2 = MeasureText(lbl, F) + 12;
    DrawRectangle(cx, cy, bw2, LH + 2, addrInputActive ? Color{ 40,80,150,255 } : Color{ 24,28,44,255 });
    DrawText(lbl, cx + 6, cy + 2, F, addrInputActive ? WHITE : Color{ 110,140,190,255 });
    if (!addrInputActive &&
        CheckCollisionPointRec(GetMousePosition(), { (float)cx,(float)cy,(float)bw2,(float)(LH + 2) }) &&
        IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
    {
        memset(addrInputBuf, 0, sizeof(addrInputBuf)); addrInputActive = true;
    }
    cy += LH + 6;

    // column header
    DrawText("ADDRESS ", cx, cy, F, { 70,80,110,255 });
    for (int col = 0;col < MEM_COLS;col++) {
        char h[4]; snprintf(h, sizeof(h), "%02X", col);
        DrawText(h, cx + ADDR_W + col * BYTE_W, cy, F, { 70,90,130,255 });
    }
    DrawText("ASCII", cx + ASCII_X, cy, F, { 70,90,130,255 });
    cy += LH;
    DrawLine(cx, cy, x + w - PAD, cy, { 40,44,65,255 }); cy++;

    int statusH = LH + 6;
    int visRows = (h - (cy - y) - statusH - PAD) / LH;
    for (int row = 0;row < visRows;row++) {
        uint32_t rowAddr = memViewBase + row * MEM_COLS;
        char ab[12]; snprintf(ab, sizeof(ab), "%08X", rowAddr);
        DrawText(ab, cx, cy, F, { 100,130,210,255 });
        char ascii[17] = {};
        for (int col = 0;col < MEM_COLS;col++) {
            uint32_t addr = rowAddr + col;
            uint8_t  val = bus.read8(addr);
            bool isCursor = (addr == memViewCursor);
            int bx2 = cx + ADDR_W + col * BYTE_W;
            if (isCursor) DrawRectangle(bx2 - 1, cy - 1, BYTE_W - 1, LH, { 55,120,230,200 });
            char hx[4]; snprintf(hx, sizeof(hx), "%02X", val);
            DrawText(hx, bx2, cy, F, isCursor ? WHITE : byteColor(val));
            if (CheckCollisionPointRec(GetMousePosition(), { (float)bx2,(float)cy,(float)(BYTE_W - 1),(float)LH }) &&
                IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) memViewCursor = addr;
            ascii[col] = (val >= 0x20 && val < 0x7F) ? (char)val : '.';
        }
        DrawText(ascii, cx + ASCII_X, cy, F, { 110,180,110,200 });
        cy += LH;
    }

    // status bar
    int sby = y + h - statusH;
    uint8_t cv = bus.read8(memViewCursor);
    char info[100];
    snprintf(info, sizeof(info),
        "  ADDR:%08X  HEX:%02X  DEC:%3u  BIN:%d%d%d%d%d%d%d%d  CHR:%c",
        memViewCursor, cv, (unsigned)cv,
        (cv >> 7) & 1, (cv >> 6) & 1, (cv >> 5) & 1, (cv >> 4) & 1,
        (cv >> 3) & 1, (cv >> 2) & 1, (cv >> 1) & 1, cv & 1,
        (cv >= 0x20 && cv < 0x7F) ? (char)cv : '.');
    DrawRectangle(x, sby, w, statusH + PAD, { 20,25,40,255 });
    DrawText(info, x + PAD, sby + 2, F, { 160,200,255,255 });
}

// ============================================================
//  CPU tab  ← the new one
// ============================================================
void DebugView::drawCPUTab(int x, int y, int w, int h) {
    const int F = 8, LH = 12, PAD = 8;
    int cx = x + PAD, cy = y + PAD;

    // update PC trace
    uint32_t pc = cpu.reg[15];
    if (pc != lastPC) {
        pcTrace.push_back(pc);
        if ((int)pcTrace.size() > PC_TRACE_LEN) pcTrace.pop_front();
        lastPC = pc;
    }

    // ---- REGISTERS (compact 2 columns) ----
    DrawText("-- REGISTERS --", cx, cy, F, { 80,120,200,255 });
    cy += LH + 2;
    static const char* rnames[] = {
        "R0","R1","R2","R3","R4","R5","R6","R7",
        "R8","R9","R10","R11","R12","SP","LR","PC"
    };
    for (int i = 0; i < 16; i++) {
        int rx = cx + (i < 8 ? 0 : 220);
        int ry = cy + (i % 8) * LH;
        char buf[32];
        snprintf(buf, sizeof(buf), "%-3s %08X", rnames[i], cpu.reg[i]);
        Color vc = (i == 15) ? Color{ 120,220,120,255 }
            : (i == 14) ? Color{ 220,180,80,255 }
            : (i == 13) ? Color{ 220,140,80,255 }
        : Color{ 200,200,200,255 };
        DrawText(buf, rx, ry, F, vc);
    }
    cy += 8 * LH + 4;

    // ---- CPSR ----
    uint32_t cpsr = cpu.cpsr;
    char cpsrBuf[64];
    snprintf(cpsrBuf, sizeof(cpsrBuf), "CPSR:%08X MODE=%s N%d Z%d C%d V%d I%d T%d",
        cpsr, modeName(cpsr & 0x1F),
        (cpsr >> 31) & 1, (cpsr >> 30) & 1, (cpsr >> 29) & 1, (cpsr >> 28) & 1,
        (cpsr >> 7) & 1, (cpsr >> 5) & 1);
    DrawText(cpsrBuf, cx, cy, F, { 220,200,100,255 }); cy += LH + 4;

    // ---- IRQ STATE ----
    uint16_t IME = bus.io[0x208] | (bus.io[0x209] << 8);
    uint16_t IE = bus.io[0x200] | (bus.io[0x201] << 8);
    uint16_t IF = bus.io[0x202] | (bus.io[0x203] << 8);
    char irqBuf[64];
    snprintf(irqBuf, sizeof(irqBuf), "IME:%d IE:%04X IF:%04X FIRED:%04X", IME & 1, IE, IF, IE & IF);
    DrawText(irqBuf, cx, cy, F, (IME & 1) ? Color{ 100,220,100,255 } : Color{ 180,60,60,255 }); cy += LH;

    // IRQ bits inline
    static const char* irqNames[] = { "VBL","HBL","VCT","TM0","TM1","TM2","TM3","SIO","DM0","DM1","DM2","DM3","KEY","GBK" };
    int bx = cx;
    for (int i = 0; i < 14; i++) {
        bool ie = (IE >> i) & 1, fi = (IF >> i) & 1;
        if (!ie && !fi) continue;
        char b[12]; snprintf(b, sizeof(b), "%s%s", irqNames[i], (ie && fi) ? "!" : (ie ? "e" : "f"));
        DrawText(b, bx, cy, F, (ie && fi) ? Color{ 80,230,80,255 } : Color{ 180,180,100,255 });
        bx += MeasureText(b, F) + 4;
    }
    cy += LH + 4;

    // ---- KEY WATCHES ----
    DrawText("-- WATCHES --", cx, cy, F, { 80,120,200,255 }); cy += LH + 2;
    struct Watch { const char* label; uint32_t addr; };
    static const Watch watches[] = {
        { "IRQ handler [03007FFC]", 0x03007FFC },
        { "BIOS flags  [03007FF8]", 0x03007FF8 },
        { "DISPCNT     [04000000]", 0x04000000 },
        { "DISPSTAT    [04000004]", 0x04000004 },
        { "HALTCNT     [04000301]", 0x04000301 },
    };
    for (auto& w : watches) {
        uint32_t val = bus.read32(w.addr);
        char wb[64]; snprintf(wb, sizeof(wb), "%-26s %08X", w.label, val);
        DrawText(wb, cx, cy, F, val ? Color{ 180,220,180,255 } : Color{ 80,80,90,255 });
        cy += LH;
    }
    cy += 4;

    // ---- DISASSEMBLY (window around current PC) ----
    DrawText("-- DISASSEMBLY --", cx, cy, F, { 80,120,200,255 });
    cy += LH + 2;

    bool thumbMode = (cpu.cpsr >> 5) & 1;
    int instrSize = thumbMode ? 2 : 4;
    uint32_t curPC = cpu.reg[15];
    uint32_t curInstrAddr = hasSteppedOnce ? (curPC - instrSize) : curPC;

    const int DISAS_BEFORE = 6;
    uint32_t startAddr = curInstrAddr - (uint32_t)(DISAS_BEFORE * instrSize);

    auto rd16 = [&](uint32_t a) -> uint16_t { return bus.read16(a); };
    auto rd32 = [&](uint32_t a) -> uint32_t { return bus.read32(a); };

    int maxLines = (y + h - 10 - cy) / LH;
    for (int i = 0; i < maxLines; i++) {
        uint32_t a = startAddr + (uint32_t)(i * instrSize);
        char mnem[96];
        Disassembler::Decode(a, thumbMode, rd16, rd32, mnem, sizeof(mnem));

        bool isCurrent = (a == curInstrAddr);
        char line[140];
        snprintf(line, sizeof(line), "%s%08X: %s", isCurrent ? "> " : "  ", a, mnem);

        Color lc = isCurrent ? Color{ 255,230,90,255 } : Color{ 170,180,200,255 };
        if (isCurrent) DrawRectangle(cx - 2, cy - 1, 480, LH, { 60,55,20,255 });
        DrawText(line, cx, cy, F, lc);
        cy += LH;
    }
}
// ============================================================
//  Main Draw
// ============================================================
void DebugView::Draw(int ox, int oy, int w, int h) {
    handleMemInput();

    DrawRectangle(ox, oy, w, h, { 18, 18, 28, 255 });

    // header bar
    uint16_t dispcnt = bus.read16(0x04000000);
    uint16_t vcount = bus.read16(0x04000006);
    uint16_t dispstat = bus.read16(0x04000004);
    char buf[256];
    snprintf(buf, sizeof(buf),
        "DISPCNT=%04X mode=%d BG0=%d BG1=%d BG2=%d BG3=%d OBJ=%d | VCOUNT=%d VBLANK=%d",
        dispcnt, dispcnt & 7,
        (dispcnt >> 8) & 1, (dispcnt >> 9) & 1, (dispcnt >> 10) & 1,
        (dispcnt >> 11) & 1, (dispcnt >> 12) & 1, vcount, dispstat & 1);
    DrawText(buf, ox + 4, oy + 4, 8, YELLOW);

    int tabY = oy + 16;
    int contentY = tabY + 22;
    int contentH = h - (contentY - oy) - 2;

    drawTabBar(ox, tabY, w);

    switch (activeTab) {
    case 0: drawGFXTab(ox, contentY, w, contentH); break;
    case 1: drawMemTab(ox, contentY, w, contentH); break;
    case 2: drawCPUTab(ox, contentY, w, contentH); break;
    }
}