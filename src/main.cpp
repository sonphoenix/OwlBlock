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
    bus.loadROM("roms/Super Mario Advance 4 - Super Mario Bros. 3 (USA, Australia) (Rev 1).gba");
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
    InitWindow(240 * 3 + 810, 600, "gba emulator + debugger");
    apu.init();
    SetTargetFPS(60);
    Image img = GenImageColor(240, 160, BLACK);
    Texture2D texture = LoadTextureFromImage(img);
    UnloadImage(img);
    bool paused = true;  // start paused so you can step from the very first instruction

    // ---- Run-to-breakpoint state ----
    char bpInputBuf[9] = { 0 };
    bool bpInputActive = false;
    bool bpEnabled = false;
    uint32_t bpAddress = 0;
    bool runningToBp = false;

    while (!WindowShouldClose()) {
        // ---- Breakpoint address text entry (active any time, doesn't need pause) ----
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

        if (IsKeyPressed(KEY_P)) paused = !paused;  // P toggles pause

        // ---- Run-to-breakpoint trigger: R while paused, with a breakpoint set ----
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
                for (int i = 0; i < cycles; i++) {
                    bus.tick();
                    bus.checkIRQ();
                }
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
                for (int i = 0; i < cycles; i++) {
                    bus.tick();
                    bus.checkIRQ();
                }
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

        // --- Gamepad (new) ---
        const int GAMEPAD_ID = 0;
        if (IsGamepadAvailable(GAMEPAD_ID)) {
            // Face buttons -> A / B
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_FACE_DOWN))  keys &= ~(1 << 0); // A
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT)) keys &= ~(1 << 1); // B

            // Select / Start
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_MIDDLE_LEFT))  keys &= ~(1 << 2); // Select
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_MIDDLE_RIGHT)) keys &= ~(1 << 3); // Start

            // D-pad
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_RIGHT)) keys &= ~(1 << 4); // Right
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_LEFT))  keys &= ~(1 << 5); // Left
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_UP))    keys &= ~(1 << 6); // Up
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_FACE_DOWN))  keys &= ~(1 << 7); // Down

            // L / R shoulder buttons
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_LEFT_TRIGGER_1))  keys &= ~(1 << 8); // L
            if (IsGamepadButtonDown(GAMEPAD_ID, GAMEPAD_BUTTON_RIGHT_TRIGGER_1)) keys &= ~(1 << 9); // R

            // Optional: left analog stick as an alternative D-pad, with deadzone
            const float DEADZONE = 0.4f;
            float axisX = GetGamepadAxisMovement(GAMEPAD_ID, GAMEPAD_AXIS_LEFT_X);
            float axisY = GetGamepadAxisMovement(GAMEPAD_ID, GAMEPAD_AXIS_LEFT_Y);
            if (axisX > DEADZONE) keys &= ~(1 << 4); // Right
            if (axisX < -DEADZONE) keys &= ~(1 << 5); // Left
            if (axisY > DEADZONE) keys &= ~(1 << 7); // Down
            if (axisY < -DEADZONE) keys &= ~(1 << 6); // Up
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
                for (int i = 0; i < cycles; i++) {
                    bus.tick();
                    bus.checkIRQ();
                }
                frame_cycles += cycles;
            }
            UpdateTexture(texture, ppu.frameBuffer.data());
        }
        BeginDrawing();
        ClearBackground({ 10, 10, 20, 255 });
        DrawTextureEx(texture, { 0, 0 }, 0.0f, 3.0f, WHITE);
        dbgView.Draw(240 * 3 + 5, 0, 810, 600);
        DrawFPS(10, 10);
        if (runningToBp) {
            char buf[80];
            snprintf(buf, sizeof(buf), "RUNNING TO 0x%08X — [ESC] cancel", bpAddress);
            DrawText(buf, 10, 10, 16, ORANGE);
        }
        else if (paused) {
            DrawText("PAUSED — [P] resume  [O] step  [hold O] fast-step  [SHIFT+O] turbo (100x)", 10, 10, 16, YELLOW);
        }

        // Breakpoint box, always visible
        {
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