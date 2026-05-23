#pragma once
#include<cstdint>
#include<vector>
#include <bus.h>

struct Bus;

struct PPU {

	std::vector<uint32_t> frameBuffer;
	uint8_t bgPrioBuffer[240];
	Bus& bus;


	PPU(Bus& b);

	void renderScanLine(uint8_t scanline);
	void renderMode0(uint8_t scanline);
	void renderMode1(uint8_t scanline);
	void renderMode3(uint8_t scanline);
	void renderMode4(uint8_t scanline);
	void renderMode5(uint8_t scanline);
	void renderMode2(uint8_t scanline);
	void renderSprites(uint8_t scanline);
};