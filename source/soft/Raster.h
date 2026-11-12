/* Raster.h
Software renderer for builds without a GPU (Miyoo Mini Plus).

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <atomic>
#include <functional>

#if defined(__ARM_NEON)
#include <arm_neon.h>
#endif



// Everything is drawn into one 32-bit framebuffer with premultiplied alpha:
// dst = src + dst * (1 - srcAlpha), the blend mode the GL renderer uses.
// Pixels are 0xAARRGGBB (the device framebuffer's order). Textures are
// converted to that order when they are created.
namespace Raster {
	// A sprite placed the way SpriteShader places it, in logical screen units
	// with the origin at the screen centre.
	struct SpriteDraw {
		uint32_t texture = 0;
		uint32_t swizzleMask = 0;
		// Row-major 4x4 colour matrix applied as color * matrix, or null for none.
		const float *swizzle = nullptr;
		float frame = 0.f;
		int frameCount = 1;
		bool uniqueSwizzleMaskFrames = false;
		float position[2] = {0.f, 0.f};
		// Maps the unit square (-0.5..0.5) to screen units: [a b; c d] as in GLSL mat2 column order.
		float transform[4] = {0.f, 0.f, 0.f, 0.f};
		float clip = 1.f;
		float alpha = 1.f;
		// Motion blur in texture coordinates (SpriteShader's blur), or zero.
		float blur[2] = {0.f, 0.f};
	};

	void Init(int width, int height);
	int Width();
	int Height();
	uint32_t *Pixels();

	// The logical screen size the game draws in (e.g. 1024x768 shown at 640x480).
	void SetView(double logicalWidth, double logicalHeight);
	double Scale();
	// Sprites sample four texels (true, the default) or the nearest one.
	void SetLinearFiltering(bool linear);
	bool LinearFiltering();
	// Soft, large art (the background haze): always nearest, and its time is not counted in
	// TakeSpriteSeconds, since smoothing it would look the same.
	void SetSmoothingAllowed(bool allowed);

	// Pixels are drawn only inside this rectangle (framebuffer pixels).
	void SetClip(int x, int y, int width, int height);
	void GetClip(int &x0, int &y0, int &x1, int &y1);
	void ResetClip();

	void Clear(uint32_t argb = 0xFF000000);

	// Draw into another pixel buffer (a RenderBuffer) until PopTarget. The logical
	// scale stays the same; the origin moves to the centre of the new target.
	void PushTarget(uint32_t *pixels, int width, int height);
	void PopTarget();
	// Show a window of a render buffer: the buffer covers size (logical units);
	// a clip-sized window starting at source is drawn centred on position, its
	// edges faded over fade = {top, bottom, left, right} logical units.
	void Composite(const uint32_t *pixels, int width, int height, const double size[2],
		const double position[2], const double clip[2], const double source[2], const float fade[4]);

	// Texture layers from ImageBuffer: premultiplied, 0xAABBGGRR. Returns an id > 0.
	uint32_t CreateTexture(int width, int height, int layers, const uint32_t *pixels);
	void DeleteTexture(uint32_t id);
	// Bytes held by all textures, for the sprite cache.
	uint64_t TextureBytes();

	// Fill a rectangle given in logical units (centre origin) with a premultiplied colour.
	void Fill(double x, double y, double width, double height, const float color[4]);
	void DrawSprite(const SpriteDraw &item);
	// Sobel edge outline of one sprite frame (the HUD target thumbnail). position and
	// transform as in SpriteDraw; off is the sample step in texture coordinates.
	void DrawOutline(uint32_t texture, float frame, int frameCount, const float position[2],
		const float transform[4], const float off[2], const float color[4]);
	// One glyph: an alpha atlas of `glyphs` equal cells in a row, cell `glyph` drawn
	// over the logical rectangle (x, y, width, height) in a premultiplied colour.
	void DrawGlyph(const uint8_t *atlas, int atlasWidth, int atlasHeight, int glyphs, int glyph,
		double x, double y, double width, double height, const float color[4]);
	// Darken the screen by a grid of alphas (0..255) stretched over a rectangle in
	// logical units, sampled like a GL_LINEAR, clamp-to-edge texture (the map fog).
	void Darken(double left, double top, double width, double height,
		const uint8_t *grid, int columns, int rows);
	// One sprite from a BatchDrawList: corner, width and height vectors in logical
	// units; the texture runs from v = 1 at the corner to v = 1 - clip.
	void DrawBatchQuad(uint32_t texture, float frame, int frameCount, const double corner[2],
		const double width[2], const double height[2], float clip, float alpha);

	// Run body(from, to) over the rows [y0, y1). With a second core and enough pixels
	// (area), the bottom half runs on a helper thread at the same time; body must only
	// write the rows it is given. ES_THREADS=1 keeps everything on one core.
	// band: rows per piece handed out (bigger when each call of body has a high fixed cost).
	void ParallelRows(int y0, int y1, int64_t area, const std::function<void(int, int)> &body, int band = 8);
	// Turn the helper thread off or back on (tests compare the two).
	void SetThreads(bool on);
	// Seconds spent drawing sprites since the last call (for choosing the filtering).
	double TakeSpriteSeconds();

	// ES_PROFILE: time per kind of drawing, logged with the frame rate. Nested scopes overlap.
	enum ProfSlot { PROF_STEP, PROF_WAIT, PROF_DRAW, PROF_STARS, PROF_SPRITE, PROF_BATCH, PROF_TEXT, PROF_LINE,
		PROF_RING, PROF_POINTER, PROF_FILL, PROF_OUTLINE, PROF_FOG, PROF_COMPOSITE, PROF_CLEAR, PROF_FLIP, PROF_COUNT };
	extern bool profOn;
	// Stars drawn and pixels they touched (both threads), for the profile log.
	extern std::atomic<long> profStars, profStarPixels;
	extern double profTime[PROF_COUNT];
	extern int profCalls[PROF_COUNT];
	extern const char *const profNames[PROF_COUNT];
	double Now();
	struct ProfScope {
		int slot;
		double start;
		explicit ProfScope(int slot) : slot(slot), start(profOn ? Now() : 0.) {}
		~ProfScope() { if(profOn) { profTime[slot] += Now() - start; ++profCalls[slot]; } }
	};

	// dst = src + dst * (1 - srcAlpha), per channel, saturating. Two channels per multiply.
	inline uint32_t BlendPixel(uint32_t dst, uint32_t src)
	{
		const uint32_t a = src >> 24;
		if(a == 255)
			return src;
		// 255 - a, rescaled to 0..256 so the divide is a shift.
		const uint32_t inv = 256 - a - (a >> 7);
		uint32_t rb = ((((dst & 0x00FF00FF) * inv + 0x00800080) >> 8) & 0x00FF00FF) + (src & 0x00FF00FF);
		uint32_t ag = ((((dst >> 8) & 0x00FF00FF) * inv + 0x00800080) >> 8 & 0x00FF00FF) + ((src >> 8) & 0x00FF00FF);
		// Each 16-bit lane holds at most 510; a set bit 8 means it overflowed, so saturate it.
		rb |= ((rb >> 8) & 0x00010001) * 0xFF;
		ag |= ((ag >> 8) & 0x00010001) * 0xFF;
		return (rb & 0x00FF00FF) | ((ag & 0x00FF00FF) << 8);
	}

	// BlendPixel on four pixels at once: the same result, bit for bit (a zero source pixel
	// leaves the destination as it was). dst and src point to four pixels each.
	inline void BlendPixels4(uint32_t *dst, const uint32_t *src)
	{
		// Four opaque pixels just replace what is there (most of a ship's body).
		if((src[0] & src[1] & src[2] & src[3]) >= 0xFF000000u)
		{
			dst[0] = src[0];
			dst[1] = src[1];
			dst[2] = src[2];
			dst[3] = src[3];
			return;
		}
#if defined(__ARM_NEON)
		const uint32x4_t s = vld1q_u32(src);
		const uint32x4_t a = vshrq_n_u32(s, 24);
		const uint16x4_t inv = vmovn_u32(vsubq_u32(vsubq_u32(vdupq_n_u32(256), a), vshrq_n_u32(a, 7)));
		// Each pixel's factor on all four of its channels.
		const uint16x4x2_t pairs = vzip_u16(inv, inv);
		const uint16x4x2_t first = vzip_u16(pairs.val[0], pairs.val[0]);
		const uint16x4x2_t second = vzip_u16(pairs.val[1], pairs.val[1]);
		const uint8x16_t d = vreinterpretq_u8_u32(vld1q_u32(dst));
		const uint16x8_t low = vmulq_u16(vmovl_u8(vget_low_u8(d)), vcombine_u16(first.val[0], first.val[1]));
		const uint16x8_t high = vmulq_u16(vmovl_u8(vget_high_u8(d)), vcombine_u16(second.val[0], second.val[1]));
		const uint8x16_t scaled = vcombine_u8(vrshrn_n_u16(low, 8), vrshrn_n_u16(high, 8));
		vst1q_u32(dst, vreinterpretq_u32_u8(vqaddq_u8(scaled, vreinterpretq_u8_u32(s))));
#else
		for(int i = 0; i < 4; ++i)
			dst[i] = BlendPixel(dst[i], src[i]);
#endif
	}

	// Every channel of a premultiplied colour times k / 256, k in 0..256.
	inline uint32_t ScalePixel(uint32_t p, uint32_t k)
	{
		return ((((p & 0x00FF00FF) * k) >> 8) & 0x00FF00FF) | ((((p >> 8) & 0x00FF00FF) * k) & 0xFF00FF00);
	}

	inline uint32_t PackColor(float r, float g, float b, float a)
	{
		auto c = [](float v) { return static_cast<uint32_t>(std::clamp(v, 0.f, 1.f) * 255.f + .5f); };
		return c(a) << 24 | c(r) << 16 | c(g) << 8 | c(b);
	}

	// Blend a premultiplied colour into pixels whose centre, in logical units, lies inside
	// the box. coverage(x, y, rgba) returns 0..1 and may change rgba for that pixel
	// (gradients). spans(y, x[4]) returns how many [x[2i], x[2i+1]] ranges of row y
	// can be covered; pixels outside them are skipped.
	template <class Coverage, class Spans>
	void CoverSpans(double minX, double minY, double maxX, double maxY, const float color[4], Coverage coverage,
		Spans spans)
	{
		const double s = Scale(), cx = Width() / 2., cy = Height() / 2.;
		int x0, y0, x1, y1;
		GetClip(x0, y0, x1, y1);
		x0 = std::max(x0, static_cast<int>(std::floor(minX * s + cx)));
		x1 = std::min(x1, static_cast<int>(std::ceil(maxX * s + cx)));
		y0 = std::max(y0, static_cast<int>(std::floor(minY * s + cy)));
		y1 = std::min(y1, static_cast<int>(std::ceil(maxY * s + cy)));
		uint32_t *pixels = Pixels();
		const int width = Width();
		// coverage and spans are pure functions of their arguments, so both halves can run at once.
		ParallelRows(y0, y1, static_cast<int64_t>(y1 - y0) * (x1 - x0), [&](int from, int to)
		{
			for(int py = from; py < to; ++py)
			{
				const double ly = (py + .5 - cy) / s;
				uint32_t *row = pixels + py * width;
				double range[4];
				const int count = spans(ly, range);
				for(int i = 0; i < count; ++i)
				{
					const int xa = std::max(x0, static_cast<int>(std::floor(range[2 * i] * s + cx - .5)));
					const int xb = std::min(x1, static_cast<int>(std::ceil(range[2 * i + 1] * s + cx - .5)) + 1);
					for(int px = xa; px < xb; ++px)
					{
						const double lx = (px + .5 - cx) / s;
						float rgba[4] = {color[0], color[1], color[2], color[3]};
						float alpha = coverage(lx, ly, rgba);
						if(alpha <= 0.f)
							continue;
						alpha = std::min(alpha, 1.f);
						row[px] = BlendPixel(row[px], PackColor(rgba[0] * alpha, rgba[1] * alpha, rgba[2] * alpha, rgba[3] * alpha));
					}
				}
			}
		});
	}

	template <class Coverage>
	void Cover(double minX, double minY, double maxX, double maxY, const float color[4], Coverage coverage)
	{
		CoverSpans(minX, minY, maxX, maxY, color, coverage,
			[minX, maxX](double, double *range) { range[0] = minX; range[1] = maxX; return 1; });
	}
}
