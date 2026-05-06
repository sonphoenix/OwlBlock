#include "../include/raylib.h"
#include<fstream>
#include "../include/cpu.h"
#include "../include/bus.h"
#include "../include/PPU.h"

std::ofstream dbg("debug.txt");
int main() {
	Bus bus;
	CPU cpu(bus);
	PPU ppu(bus);
	bus.cpuptr = &cpu;
	bus.loadBIOS("bios/gba_bios.bin");
	bus.loadROM("roms/bm_modes.gba");
	cpu.skipBIOS();

	InitWindow(240 * 3, 160 * 3, "gba emulator");
	SetTargetFPS(60);

	Image img = GenImageColor(240, 160, BLACK);
	Texture2D texture = LoadTextureFromImage(img);
	UnloadImage(img);
	while (!WindowShouldClose()) {
		for (int scanline = 0;scanline < 228;scanline++) {
			for (int i = 0;i < 280;i++) {
				cpu.Step();
				bus.tick();
			}
			if (scanline < 160) {
				ppu.renderScanLine(scanline);
			}
		}
		UpdateTexture(texture, ppu.frameBuffer.data());

		BeginDrawing();
		ClearBackground(BLACK);
		DrawTextureEx(texture, { 0,0 }, 0.0f, 3.0f, WHITE);
		EndDrawing();
	}
	UnloadTexture(texture);
	CloseWindow();
	return 0;
}