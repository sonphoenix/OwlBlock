#include "../include/raylib.h"
#include <fstream>
#include "../include/cpu.h"
#include "../include/bus.h"
#include "../include/PPU.h"
#include "../include/DebugView.h"

std::ofstream dbg("debug.txt");

int main() {

    Bus bus;
    CPU cpu(bus);
    PPU ppu(bus);
    DebugView dbgView(bus, cpu);
    bus.cpuptr = &cpu;
    bus.ppuptr = &ppu;
    bus.loadBIOS("bios/gba_bios.bin");
    bus.loadROM("roms/win_demo.gba");

    //cpu.skipBIOS();

    InitWindow(240 * 3 + 810, 600, "gba emulator + debugger");

    SetTargetFPS(60);

    Image img = GenImageColor(240, 160, BLACK);
    Texture2D texture = LoadTextureFromImage(img);
    UnloadImage(img);

    while (!WindowShouldClose()) {

        // -------------------------------------------------
        // INPUT
        // KEYINPUT is ACTIVE LOW
        // -------------------------------------------------

        uint16_t keys = 0x03FF;

        if (IsKeyDown(KEY_X))         keys &= ~(1 << 0); // A
        if (IsKeyDown(KEY_Z))         keys &= ~(1 << 1); // B
        if (IsKeyDown(KEY_BACKSPACE)) keys &= ~(1 << 2); // Select
        if (IsKeyDown(KEY_ENTER))     keys &= ~(1 << 3); // Start

        if (IsKeyDown(KEY_RIGHT))     keys &= ~(1 << 4); // Right
        if (IsKeyDown(KEY_LEFT))      keys &= ~(1 << 5); // Left
        if (IsKeyDown(KEY_UP))        keys &= ~(1 << 6); // Up
        if (IsKeyDown(KEY_DOWN))      keys &= ~(1 << 7); // Down

        if (IsKeyDown(KEY_S))         keys &= ~(1 << 8); // R
        if (IsKeyDown(KEY_A))         keys &= ~(1 << 9); // L

        bus.io[0x130] = keys & 0xFF;
        bus.io[0x131] = (keys >> 8) & 0xFF;

        // -------------------------------------------------

        bus.frameCount++;

        int frame_cycles = 0;

        while (frame_cycles < 280896) {

            int cycles = cpu.halted ? 1 : cpu.Step();
            for (int i = 0; i < cycles; i++)
                bus.tick();
            frame_cycles += cycles;
        }


        UpdateTexture(texture, ppu.frameBuffer.data());

        BeginDrawing();

        ClearBackground({ 10, 10, 20, 255 });

        DrawTextureEx(texture, { 0, 0 }, 0.0f, 3.0f, WHITE);

        dbgView.Draw(240 * 3 + 5, 0, 810, 600);
        EndDrawing();
    }

    UnloadTexture(texture);

    CloseWindow();

    return 0;
}