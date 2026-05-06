#include "PPU.h"
#include "bus.h"
#include "gba_registers.h"

PPU::PPU(Bus& b) : bus(b),frameBuffer(240*160,0xFF000000) {

}



void PPU::renderMode3(uint8_t scanline) {
	for (int i = 0;i < 240;i++) {
		uint32_t address = MEM_VRAM + (scanline * 240 + i) * 2;
		uint16_t col16 = bus.read16(address);

		uint8_t b5 = (col16 >> 10) & 0x1F;
		uint8_t g5 = (col16 >> 5) & 0x1F;
		uint8_t r5 = col16 & 0x1F;

		uint8_t b8 = (b5 << 3) | (b5 >> 2);
		uint8_t g8 = (g5 << 3) | (g5 >> 2);
		uint8_t r8 = (r5 << 3) | (r5 >> 2);
		uint8_t alpha = 255;

		uint32_t col32 = (alpha << 24) | (b8 << 16) | (g8 << 8) | r8;
		frameBuffer[240 * scanline + i] = col32;

	}
}

void PPU::renderMode4(uint8_t scanline) {
	uint32_t address;
	uint32_t base;
	bool frame = (bus.read16(REG_DISPCNT)) & 0x10;
	if (frame)
		base = 0x0600A000;
	else
		base = 0x06000000;
	for (int i = 0; i < 240;i++) {
		address = base + (scanline * 240 + i);
		uint8_t index = bus.read8(address);

		uint32_t paletteAdress = MEM_PRAM + index * 2;
		uint16_t col16 = bus.read16(paletteAdress);
		uint8_t b5 = (col16 >> 10) & 0x1F;
		uint8_t g5 = (col16 >> 5) & 0x1F;
		uint8_t r5 = col16 & 0x1F;

		uint8_t b8 = (b5 << 3) | (b5 >> 2);
		uint8_t g8 = (g5 << 3) | (g5 >> 2);
		uint8_t r8 = (r5 << 3) | (r5 >> 2);
		uint8_t alpha = 255;

		uint32_t col32 = (alpha << 24) | (b8 << 16) | (g8 << 8) | r8;
		frameBuffer[240 * scanline + i] = col32;
	}
}


void PPU::renderMode5(uint8_t scanline) {

	uint32_t address;
	bool frame = (bus.read16(REG_DISPCNT)) & 0x10;

	for (int i = 0;i < 240;i++) {
		if (i >= 160 || scanline >= 128) {
			uint16_t col16 = bus.read16(MEM_PRAM);
			uint8_t b5 = (col16 >> 10) & 0x1F;
			uint8_t g5 = (col16 >> 5) & 0x1F;
			uint8_t r5 = col16 & 0x1F;

			uint8_t b8 = (b5 << 3) | (b5 >> 2);
			uint8_t g8 = (g5 << 3) | (g5 >> 2);
			uint8_t r8 = (r5 << 3) | (r5 >> 2);
			uint8_t alpha = 255;

			uint32_t col32 = (alpha << 24) | (b8 << 16) | (g8 << 8) | r8;
			frameBuffer[240 * scanline + i] = col32;
		}
		else {

			if (frame)
				address = 0x0600A000 + (scanline * 160 + i) * 2;
			else
				address = 0x06000000 + (scanline * 160 + i) * 2;

			uint16_t col16 = bus.read16(address);
			uint8_t b5 = (col16 >> 10) & 0x1F;
			uint8_t g5 = (col16 >> 5) & 0x1F;
			uint8_t r5 = col16 & 0x1F;

			uint8_t b8 = (b5 << 3) | (b5 >> 2);
			uint8_t g8 = (g5 << 3) | (g5 >> 2);
			uint8_t r8 = (r5 << 3) | (r5 >> 2);
			uint8_t alpha = 255;

			uint32_t col32 = (alpha << 24) | (b8 << 16) | (g8 << 8) | r8;
			frameBuffer[240 * scanline + i] = col32;


		}
	}
}


void PPU::renderScanLine(uint8_t scanline) {
	uint16_t dispcnt = bus.read16(REG_DISPCNT);
	uint8_t mode = dispcnt & 0x7;
	switch (mode) {
	case 3: renderMode3(scanline);break;
	case 4: renderMode4(scanline); break;
	case 5: renderMode5(scanline);break;
	}
}


