// Timing of the renderer's big drawing calls on one core and on two (run it on the device):
// zig c++ -target arm-linux-gnueabihf.2.28 -mcpu=cortex_a7 -O3 -std=c++20 -I source
//   miyoo/tests/raster_bench.cpp source/soft/Raster.cpp -lpthread -o raster_bench
#include "soft/Raster.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {
	double Time(int repeats, void (*draw)())
	{
		draw();
		const double start = Raster::Now();
		for(int i = 0; i < repeats; ++i)
			draw();
		return (Raster::Now() - start) / repeats * 1000.;
	}

	uint32_t texture = 0;

	void Clear() { Raster::Clear(0xFF000000); }
	void Fill()
	{
		const float c[4] = {.1f, .1f, .1f, .5f};
		Raster::Fill(0., 0., 800., 600., c);
	}
	void Sprite()
	{
		Raster::SpriteDraw d;
		d.texture = texture;
		// 520x520 units turned 30 degrees: about the area of a big ship up close.
		d.transform[0] = 450.f; d.transform[1] = 260.f; d.transform[2] = -260.f; d.transform[3] = 450.f;
		Raster::DrawSprite(d);
	}
	void Ring()
	{
		const float white[4] = {1.f, 1.f, 1.f, 1.f};
		Raster::Cover(-300., -250., 300., 250., white, [](double x, double y, float *) {
			return static_cast<float>(1. - std::abs(std::sqrt(x * x + y * y) - 200.) / 4.); });
	}
}

int main()
{
	Raster::Init(640, 480);
	Raster::SetView(800, 600);
	std::vector<uint32_t> texels(256 * 256);
	for(size_t i = 0; i < texels.size(); ++i)
		texels[i] = 0xFF000000u | static_cast<uint32_t>(i * 2654435761u >> 8);
	texture = Raster::CreateTexture(256, 256, 1, texels.data());
	struct Case { const char *name; void (*draw)(); };
	const Case cases[] = {{"clear", Clear}, {"fill", Fill}, {"sprite", Sprite}, {"ring", Ring}};
	for(bool linear : {false, true})
	{
		Raster::SetLinearFiltering(linear);
		for(const Case &c : cases)
		{
			if(linear && c.draw != Sprite)
				continue;
			Raster::SetThreads(false);
			const double one = Time(30, c.draw);
			Raster::SetThreads(true);
			const double two = Time(30, c.draw);
			printf("%-14s one core %6.2f ms  two cores %6.2f ms\n",
				(std::string(c.name) + (linear ? " linear" : "")).c_str(), one, two);
		}
	}
	return 0;
}
