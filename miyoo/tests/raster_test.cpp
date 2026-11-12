// Self-check for source/soft/Raster:
// c++ -std=c++20 -I source miyoo/tests/raster_test.cpp source/soft/Raster.cpp -lpthread
#include "soft/Raster.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <vector>

// A full-size scene that uses every drawing call split across cores: the result must be the
// same, pixel for pixel, on one core and on two.
static std::vector<uint32_t> Scene(bool threads, bool linear)
{
	Raster::SetThreads(threads);
	Raster::Init(640, 480);
	Raster::SetView(800, 600);
	Raster::SetLinearFiltering(linear);
	std::vector<uint32_t> texels(64 * 32);
	for(size_t i = 0; i < texels.size(); ++i)
		texels[i] = (i % 7 ? 0xFF000000u : 0x80000000u) | static_cast<uint32_t>(i * 2654435761u >> 8 & 0x7F7F7F);
	const uint32_t tex = Raster::CreateTexture(64, 32, 1, texels.data());
	Raster::Clear(0xFF102030);
	const float half[4] = {.2f, .1f, .05f, .5f};
	Raster::Fill(-50., 20., 500., 300., half);
	Raster::SpriteDraw d;
	d.texture = tex;
	d.position[0] = 30.f;
	d.position[1] = -10.f;
	// 400x200 units, turned about 30 degrees.
	d.transform[0] = 346.f; d.transform[1] = 200.f; d.transform[2] = -100.f; d.transform[3] = 173.f;
	d.alpha = .8f;
	Raster::DrawSprite(d);
	const uint8_t grid[6] = {0, 80, 200, 255, 30, 0};
	Raster::Darken(-300., -200., 600., 400., grid, 3, 2);
	std::vector<uint32_t> canvas(100 * 80, 0x80402010);
	const double size[2] = {500., 400.}, pos[2] = {0., 0.}, clip[2] = {450., 350.}, src[2] = {20., 20.};
	const float fade[4] = {30.f, 30.f, 20.f, 20.f};
	Raster::Composite(canvas.data(), 100, 80, size, pos, clip, src, fade);
	const float white[4] = {1.f, 1.f, 1.f, 1.f};
	Raster::Cover(-200., -150., 200., 150., white, [](double x, double y, float *) {
		return static_cast<float>(1. - std::abs(std::sqrt(x * x + y * y) - 120.) / 3.); });
	Raster::DeleteTexture(tex);
	return std::vector<uint32_t>(Raster::Pixels(), Raster::Pixels() + 640 * 480);
}

static uint32_t At(int x, int y) { return Raster::Pixels()[y * Raster::Width() + x]; }

int main()
{
	for(bool linear : {false, true})
		assert(Scene(false, linear) == Scene(true, linear));
	Raster::SetThreads(true);

	Raster::Init(8, 8);
	// The exact pixel checks below use nearest sampling; linear is checked at the end.
	Raster::SetLinearFiltering(false);
	Raster::SetView(16, 16); // logical 16x16 shown at 8x8: scale 0.5
	assert(Raster::Scale() == .5);

	// 2x1 texture, 0xAABBGGRR: left opaque red, right opaque blue.
	const uint32_t px[2] = {0xFF0000FF, 0xFFFF0000};
	uint32_t tex = Raster::CreateTexture(2, 1, 1, px);
	assert(tex == 1 && Raster::TextureBytes() == 8);

	// 8x4 logical units at the centre, unrotated: covers pixels x 2..5, y 3..4.
	Raster::SpriteDraw d;
	d.texture = tex;
	d.transform[0] = 8.f; d.transform[3] = 4.f;
	Raster::Clear();
	Raster::DrawSprite(d);
	assert(At(2, 3) == 0xFFFF0000); // red in 0xAARRGGBB
	assert(At(5, 4) == 0xFF0000FF); // blue
	assert(At(1, 3) == 0xFF000000 && At(2, 2) == 0xFF000000);

	// Rotated 90 degrees: the red half moves to the top.
	d.transform[0] = 0.f; d.transform[1] = 8.f; d.transform[2] = -4.f; d.transform[3] = 0.f;
	Raster::Clear();
	Raster::DrawSprite(d);
	assert(At(3, 2) == 0xFFFF0000 && At(3, 5) == 0xFF0000FF);

	// Half alpha over white: premultiplied red at half strength plus half the white.
	d.transform[0] = 8.f; d.transform[1] = 0.f; d.transform[2] = 0.f; d.transform[3] = 4.f;
	d.alpha = .5f;
	Raster::Clear(0xFFFFFFFF);
	Raster::DrawSprite(d);
	uint32_t p = At(2, 3);
	assert((p >> 24) == 0xFF && ((p >> 16) & 0xFF) >= 0xFE && ((p >> 8) & 0xFF) >= 0x7F && ((p >> 8) & 0xFF) <= 0x81);

	// Clip rectangle stops drawing outside it.
	d.alpha = 1.f;
	Raster::Clear();
	Raster::SetClip(4, 0, 4, 8);
	Raster::DrawSprite(d);
	assert(At(2, 3) == 0xFF000000 && At(5, 4) == 0xFF0000FF);
	Raster::ResetClip();

	// Swizzle that swaps red and blue (column-major, applied as color * M).
	const float swap[16] = {0,0,1,0, 0,1,0,0, 1,0,0,0, 0,0,0,1};
	d.swizzle = swap;
	Raster::Clear();
	Raster::DrawSprite(d);
	assert(At(2, 3) == 0xFF0000FF && At(5, 4) == 0xFFFF0000);

	// Fill: opaque green over 2x2 logical at the centre = pixel (4,4) region of 1x1.
	const float green[4] = {0.f, 1.f, 0.f, 1.f};
	Raster::Clear();
	Raster::Fill(1, 1, 2, 2, green);
	assert(At(4, 4) == 0xFF00FF00 && At(3, 3) == 0xFF000000);

	// Batch quad: the texture runs from v = 1 at the corner (bottom row) upward.
	const uint32_t col[2] = {0xFF0000FF, 0xFFFF0000}; // 1x2: top red, bottom blue
	uint32_t tex2 = Raster::CreateTexture(1, 2, 1, col);
	const double corner[2] = {-4., -4.}, w[2] = {8., 0.}, h[2] = {0., 8.};
	Raster::Clear();
	Raster::DrawBatchQuad(tex2, 0.f, 1, corner, w, h, 1.f, 1.f);
	assert(At(2, 2) == 0xFF0000FF && At(5, 5) == 0xFFFF0000);
	// Clip 0.5: only the bottom half of the texture, stretched over the shortened height.
	const double hClip[2] = {0., 4.};
	Raster::Clear();
	Raster::DrawBatchQuad(tex2, 0.f, 1, corner, w, hClip, .5f, 1.f);
	assert(At(2, 2) == 0xFF0000FF && At(2, 3) == 0xFF0000FF && At(2, 4) == 0xFF000000);
	Raster::DeleteTexture(tex2);

	// Render target: draw into a 4x4 canvas, then composite it back unfaded.
	uint32_t canvas[16] = {};
	Raster::PushTarget(canvas, 4, 4);
	assert(Raster::Width() == 4 && Raster::Pixels() == canvas);
	Raster::Fill(0, 0, 8, 8, green); // logical 8x8 = the whole 4x4 canvas at scale 0.5
	Raster::PopTarget();
	assert(Raster::Width() == 8 && canvas[0] == 0xFF00FF00 && canvas[15] == 0xFF00FF00);
	const double size[2] = {8., 8.}, pos[2] = {0., 0.}, clipSize[2] = {8., 8.}, src[2] = {0., 0.};
	const float noFade[4] = {0.f, 0.f, 0.f, 0.f};
	Raster::Clear();
	Raster::Composite(canvas, 4, 4, size, pos, clipSize, src, noFade);
	// 8 logical units at scale 0.5 = screen pixels 2..5; nothing outside.
	assert(At(2, 2) == 0xFF00FF00 && At(5, 5) == 0xFF00FF00);
	assert(At(1, 1) == 0xFF000000 && At(6, 6) == 0xFF000000);
	// A fade of the full height on top leaves the top row darker than the bottom.
	const float topFade[4] = {8.f, 0.f, 0.f, 0.f};
	Raster::Clear();
	Raster::Composite(canvas, 4, 4, size, pos, clipSize, src, topFade);
	assert(((At(3, 2) >> 8) & 0xFF) < ((At(3, 5) >> 8) & 0xFF));

	Raster::DeleteTexture(tex);
	assert(Raster::TextureBytes() == 0);
	// Two-lane blend: within 1 of the exact premultiplied formula, saturating on overflow.
	for(uint32_t a = 0; a < 256; a += 5)
		for(uint32_t d = 0; d < 256; d += 3)
			for(uint32_t c : {0u, a / 2, a, 255u})
			{
				uint32_t out = Raster::BlendPixel(d * 0x01010101u, a << 24 | c * 0x00010101u);
				double exact = std::min(255., d * (255. - a) / 255. + c);
				for(int shift = 0; shift < 24; shift += 8)
					assert(std::abs(static_cast<double>((out >> shift) & 0xFF) - exact) <= 1.);
			}
	assert(Raster::ScalePixel(0xFFFFFFFF, 256) == 0xFFFFFFFF && Raster::ScalePixel(0xFF804020, 128) == 0x7F402010);

	// Linear filtering blends neighbouring texels: a quarter of the way from the red texel's
	// centre towards the blue one is mostly red with some blue.
	{
		Raster::SetLinearFiltering(true);
		Raster::Clear();
		const uint32_t pair[2] = {0xFF0000FF, 0xFFFF0000};
		Raster::SpriteDraw red;
		red.texture = Raster::CreateTexture(2, 1, 1, pair);
		red.transform[0] = 8.f;
		red.transform[3] = 4.f;
		Raster::DrawSprite(red);
		const uint32_t mixed = At(3, 3);
		const uint32_t r = (mixed >> 16) & 0xFF, b = mixed & 0xFF;
		assert(r > b && b > 0 && (mixed >> 24) == 0xFF);
		Raster::SetLinearFiltering(false);
	}

	// Fog: a 3x2 grid of alphas stretched over the whole view darkens white by the
	// bilinear value at each pixel (the floating-point formula it replaced), within 2.
	{
		const uint8_t grid[6] = {0, 128, 255, 255, 60, 0};
		Raster::Clear(0xFFFFFFFF);
		Raster::Darken(-8., -8., 16., 16., grid, 3, 2);
		for(int y = 0; y < 8; ++y)
			for(int x = 0; x < 8; ++x)
			{
				auto sample = [&](double t, int count, int &i0, int &i1) {
					i0 = std::clamp(static_cast<int>(std::floor(t)), 0, count - 1);
					i1 = std::clamp(static_cast<int>(std::floor(t)) + 1, 0, count - 1);
					return t < 0. ? 0. : t - std::floor(t);
				};
				int c0, c1, r0, r1;
				double fx = sample((x + .5) / 8. * 3 - .5, 3, c0, c1);
				double fy = std::min(1., sample((y + .5) / 8. * 2 - .5, 2, r0, r1));
				double t = grid[r0 * 3 + c0] + (grid[r0 * 3 + c1] - grid[r0 * 3 + c0]) * fx;
				double b = grid[r1 * 3 + c0] + (grid[r1 * 3 + c1] - grid[r1 * 3 + c0]) * fx;
				int expected = 255 - static_cast<int>(t + (b - t) * fy + .5);
				int got = At(x, y) & 0xFF;
				assert(std::abs(got - expected) <= 2);
			}
	}

	puts("raster ok");
}
