#include <fstream>
#include <cstring>
#include <cctype>
#include <cstdio>
#include "vendor/raylib.h"
#include "core/cpu.h"
#include "core/bus.h"
#include "video/PPU.h"
#include "audio/apu.h"
#include "debug/DebugView.h"

std::ofstream dbg("debug.txt");

const int GAME_W = 240, GAME_H = 160;
const int DEBUG_PANEL_W = 810;

void ApplyLayout(bool debugMode, float& gameScale) {
    if (debugMode) {
        gameScale = 3.0f;
        SetWindowSize(GAME_W * (int)gameScale + DEBUG_PANEL_W, 600);
    }
    else {
        // fill the window with just the game view, bigger scale since there's no panel to share with
        gameScale = 4.0f;
        SetWindowSize(GAME_W * (int)gameScale, GAME_H * (int)gameScale);
    }
}

int main() {
    Bus bus;
    CPU cpu(bus);
    dbg << "[AFTER_CONSTRUCT] PC=0x" << std::hex << cpu.reg[15] << "\n";
    PPU ppu(bus);
    APU apu(bus);
    DebugView dbgView(bus, cpu);
    bus.cpuptr = &cpu;
    bus.ppuptr = &ppu;
    bus.apuptr = &apu;
    bus.loadBIOS("bios/gba_bios.bin");
    bus.loadROM("roms/redfire.gba");
    bus.init();
    dbg << "[LOOP_BYTES] ";
    for (uint32_t a = 0x800f19c; a <= 0x800f1a4; a += 2) {
        uint16_t instr = bus.read16(a);
        dbg << "0x" << std::hex << a << ":" << instr << " ";
    }
    dbg << "\n";
    dbg.flush();
    cpu.skipBIOS();
    dbg << "[AFTER_SKIPBIOS] PC=0x" << std::hex << cpu.reg[15] << "\n";
    dbg.flush();

    InitWindow(GAME_W * 3 + DEBUG_PANEL_W, 600, "Owl Block");
    apu.init();
    SetTargetFPS(60);
    ToggleFullscreen();

    Image img = GenImageColor(GAME_W, GAME_H, BLACK);
    Texture2D texture = LoadTextureFromImage(img);
    UnloadImage(img);

    bool paused = false;
    bool debugMode = false;
    float gameScale = 3.0f;
    ApplyLayout(debugMode, gameScale);

    char bpInputBuf[9] = { 0 };
    bool bpInputActive = false;
    bool bpEnabled = false;
    uint32_t bpAddress = 0;
    bool runningToBp = false;

    while (!WindowShouldClose()) {
        if (bpInputActive) {
            int key = GetCharPressed();
            while (key > 0) {
                bool isHex = (key >= '0' && key <= '9') || (key >= 'a' && key <= 'f') || (key >= 'A' && key <= 'F');
                if (isHex) {
                    int len = (int)strlen(bpInputBuf);
                    if (len < 8) {
                        bpInputBuf[len] = (char)toupper(key);
                        bpInputBuf[len + 1] = '\0';
                    }
                }
                key = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE)) {
                int len = (int)strlen(bpInputBuf);
                if (len > 0) bpInputBuf[len - 1] = '\0';
            }
            if (IsKeyPressed(KEY_ENTER)) {
                uint32_t addr = 0;
                if (sscanf_s(bpInputBuf, "%X", &addr) == 1) {
                    bpAddress = addr;
                    bpEnabled = true;
                }
                bpInputActive = false;
            }
            if (IsKeyPressed(KEY_ESCAPE)) bpInputActive = false;
        }
        else {
            if (IsKeyPressed(KEY_B)) {
                memset(bpInputBuf, 0, sizeof(bpInputBuf));
                bpInputActive = true;
            }
        }

        if (IsKeyPressed(KEY_P)) paused = !paused;

        if (IsKeyPressed(KEY_F1)) {
            debugMode = !debugMode;
            ApplyLayout(debugMode, gameScale);
        }

        if (paused && !bpInputActive && bpEnabled && IsKeyPressed(KEY_R)) {
            runningToBp = true;
        }

        if (runningToBp) {
            const int MAX_STEPS_PER_FRAME = 200000;
            int stepped = 0;
            while (stepped < MAX_STEPS_PER_FRAME) {
                bool thumbNow = (cpu.cpsr >> 5) & 1;
                uint32_t curInstrAddr = cpu.reg[15] - (thumbNow ? 2 : 4);
                if (curInstrAddr == bpAddress) {
                    runningToBp = false;
                    paused = true;
                    break;
                }
                int cycles = cpu.halted ? 1 : cpu.Step();
                dbgView.hasSteppedOnce = true;
                cycles += bus.dmaController.pendingCycles;
                bus.dmaController.pendingCycles = 0;
                bus.advance(cycles);
                bus.checkIRQ();
                stepped++;
            }
            if (IsKeyPressed(KEY_ESCAPE)) { runningToBp = false; paused = true; }
        }
        else if (paused && !bpInputActive) {
            static float holdTimer = 0.0f;
            static float repeatAccum = 0.0f;
            const float INITIAL_DELAY = 0.35f;
            const float REPEAT_RATE = 0.02f;
            const int TURBO_STEPS = 1000;

            bool shiftHeld = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
            int stepsThisTick = 0;

            if (IsKeyPressed(KEY_O)) {
                stepsThisTick = shiftHeld ? TURBO_STEPS : 1;
                holdTimer = 0.0f;
                repeatAccum = 0.0f;
            }
            else if (IsKeyDown(KEY_O)) {
                holdTimer += GetFrameTime();
                if (holdTimer >= INITIAL_DELAY) {
                    repeatAccum += GetFrameTime();
                    if (repeatAccum >= REPEAT_RATE) {
                        stepsThisTick = shiftHeld ? TURBO_STEPS : 1;
                        repeatAccum = 0.0f;
                    }
                }
            }
            else {
                holdTimer = 0.0f;
                repeatAccum = 0.0f;
            }

            for (int s = 0; s < stepsThisTick; s++) {
                int cycles = cpu.halted ? 1 : cpu.Step();
                cycles += bus.dmaController.pendingCycles;
                bus.dmaController.pendingCycles = 0;
                bus.advance(cycles);
                bus.checkIRQ();
            }
        }

        uint16_t keys = 0x03FF;
        if (IsKeyDown(KEY_X))         keys &= ~(1 << 0);
        if (IsKeyDown(KEY_Z))         keys &= ~(1 << 1);
        if (IsKeyDown(KEY_BACKSPACE)) keys &= ~(1 << 2);
        if (IsKeyDown(KEY_ENTER))     keys &= ~(1 << 3);
        if (IsKeyDown(KEY_RIGHT))     keys &= ~(1 << 4);
        if (IsKeyDown(KEY_LEFT))      keys &= ~(1 << 5);
        if (IsKeyDown(KEY_UP))        keys &= ~(1 << 6);
        if (IsKeyDown(KEY_DOWN))      keys &= ~(1 << 7);
        if (IsKeyDown(KEY_S))         keys &= ~(1 << 8);
        if (IsKeyDown(KEY_A))         keys &= ~(1 << 9);

        const int GAMEPAD_ID = 0;
        if (IsGamepadAvailable(GAMEPAD_ID)) {
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))  keys &= ~(1 << 0);
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) keys &= ~(1 << 1);
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_MIDDLE_LEFT))  keys &= ~(1 << 2);
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_MIDDLE_RIGHT)) keys &= ~(1 << 3);
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) keys &= ~(1 << 4);
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  keys &= ~(1 << 5);
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_UP))    keys &= ~(1 << 6);
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_DOWN))  keys &= ~(1 << 7);
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_TRIGGER_1))  keys &= ~(1 << 8);
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) keys &= ~(1 << 9);

            const float DEADZONE = 0.4f;
            float axisX = GetGamepadAxisMovement(GAMEPAD_ID, GAMEPAD_AXIS_LEFT_X);
            float axisY = GetGamepadAxisMovement(GAMEPAD_ID, GAMEPAD_AXIS_LEFT_Y);
            if (axisX > DEADZONE) keys &= ~(1 << 4);
            if (axisX < -DEADZONE) keys &= ~(1 << 5);
            if (axisY > DEADZONE) keys &= ~(1 << 7);
            if (axisY < -DEADZONE) keys &= ~(1 << 6);
        }

        bus.io[0x130] = keys & 0xFF;
        bus.io[0x131] = (keys >> 8) & 0xFF;

        if (!paused) {
            bus.frameCount++;
            int frame_cycles = 0;
            while (frame_cycles < 280896) {
                int cycles = cpu.halted ? 1 : cpu.Step();
                cycles += bus.dmaController.pendingCycles;
                bus.dmaController.pendingCycles = 0;
                bus.advance(cycles);
                bus.checkIRQ();
                frame_cycles += cycles;
            }
            UpdateTexture(texture, ppu.frameBuffer.data());
        }

        BeginDrawing();
        ClearBackground({ 10, 10, 20, 255 });
        DrawTextureEx(texture, { 0, 0 }, 0.0f, gameScale, WHITE);

        if (debugMode) {
            dbgView.Draw((int)(GAME_W * gameScale) + 5, 0, DEBUG_PANEL_W, 600);
        }

        DrawFPS(10, 10);
        if (runningToBp) {
            char buf[80];
            snprintf(buf, sizeof(buf), "RUNNING TO 0x%08X — [ESC] cancel", bpAddress);
            DrawText(buf, 10, 10, 16, ORANGE);
        }
        else if (paused) {
            DrawText("PAUSED - [P] resume  [O] step  [hold O] fast-step  [SHIFT+O] turbo (100x)", 10, 10, 16, YELLOW);
        }

        if (debugMode) {
            char bpLabel[64];
            if (bpInputActive)
                snprintf(bpLabel, sizeof(bpLabel), "BREAKPOINT: %s_", bpInputBuf);
            else if (bpEnabled)
                snprintf(bpLabel, sizeof(bpLabel), "BREAKPOINT: 0x%08X  [R] run-to  [B] edit", bpAddress);
            else
                snprintf(bpLabel, sizeof(bpLabel), "BREAKPOINT: (none)  [B] set address");
            Color bpColor = bpInputActive ? WHITE : (bpEnabled ? Color{ 120,220,255,255 } : Color{ 120,120,130,255 });
            DrawText(bpLabel, 10, 32, 14, bpColor);
        }

        EndDrawing();
    }
    bus.save.saveToDisk("game.sav");
    UnloadTexture(texture);
    CloseWindow();
    return 0;
}