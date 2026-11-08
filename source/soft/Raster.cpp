/* Raster.cpp
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

#include "Raster.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <type_traits>
#include <unordered_map>
#include <vector>

using namespace std;
using Raster::BlendPixel;
using Raster::ScalePixel;

bool Raster::profOn = getenv("ES_PROFILE") != nullptr;
double Raster::profTime[PROF_COUNT] = {};
int Raster::profCalls[PROF_COUNT] = {};
const char *const Raster::profNames[PROF_COUNT] = {"step", "wait", "draw", "stars", "sprite", "batch", "text",
	"line", "ring", "pointer", "fill", "outline", "fog", "composite", "clear", "flip"};

double Raster::Now()
{
	timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec + t.tv_nsec * 1e-9;
}

namespace {
	struct Texture {
		int width = 0;
		int height = 0;
		int layers = 0;
		vector<uint32_t> pixels;
	};

	// The screen, and the target currently drawn into (the screen or a render buffer).
	vector<uint32_t> screen;
	uint32_t *frame = nullptr;
	struct Target {
		uint32_t *pixels;
		int width, height, x0, y0, x1, y1;
	};
	vector<Target> targets;
	int fbWidth = 0;
	int fbHeight = 0;
	double scale = 1.;
	int clipX0 = 0, clipY0 = 0, clipX1 = 0, clipY1 = 0;
	// Sprites blend four texels (GL_LINEAR) unless the "Texture filtering" preference is "nearest".
	bool linearFiltering = true;

	// Slot 0 is never used so that 0 means "no texture", as in GL.
	vector<Texture> textures(1);
	vector<uint32_t> freeIds;
	uint64_t textureBytes = 0;

	void ForgetOutlines(uint32_t id);

}



void Raster::Init(int width, int height)
{
	fbWidth = width;
	fbHeight = height;
	screen.assign(static_cast<size_t>(width) * height, 0xFF000000);
	frame = screen.data();
	targets.clear();
	ResetClip();
	SetView(width, height);
}



int Raster::Width()
{
	return fbWidth;
}



int Raster::Height()
{
	return fbHeight;
}



uint32_t *Raster::Pixels()
{
	return frame;
}



void Raster::SetView(double logicalWidth, double logicalHeight)
{
	scale = min(fbWidth / logicalWidth, fbHeight / logicalHeight);
}



double Raster::Scale()
{
	return scale;
}



void Raster::SetLinearFiltering(bool linear)
{
	linearFiltering = linear;
}



void Raster::SetClip(int x, int y, int width, int height)
{
	clipX0 = clamp(x, 0, fbWidth);
	clipY0 = clamp(y, 0, fbHeight);
	clipX1 = clamp(x + width, 0, fbWidth);
	clipY1 = clamp(y + height, 0, fbHeight);
}



void Raster::GetClip(int &x0, int &y0, int &x1, int &y1)
{
	x0 = clipX0;
	y0 = clipY0;
	x1 = clipX1;
	y1 = clipY1;
}



void Raster::ResetClip()
{
	SetClip(0, 0, fbWidth, fbHeight);
}



void Raster::Clear(uint32_t argb)
{
	ProfScope prof(PROF_CLEAR);
	for(int y = clipY0; y < clipY1; ++y)
		fill(frame + y * fbWidth + clipX0, frame + y * fbWidth + clipX1, argb);
}



uint32_t Raster::CreateTexture(int width, int height, int layers, const uint32_t *pixels)
{
	uint32_t id;
	if(!freeIds.empty())
	{
		id = freeIds.back();
		freeIds.pop_back();
	}
	else
	{
		id = textures.size();
		textures.emplace_back();
	}
	Texture &t = textures[id];
	t.width = width;
	t.height = height;
	t.layers = layers;
	size_t count = static_cast<size_t>(width) * height * layers;
	t.pixels.resize(count);
	// 0xAABBGGRR -> 0xAARRGGBB
	for(size_t i = 0; i < count; ++i)
	{
		uint32_t p = pixels[i];
		t.pixels[i] = (p & 0xFF00FF00) | ((p & 0xFF) << 16) | ((p >> 16) & 0xFF);
	}
	textureBytes += count * 4;
	return id;
}



void Raster::DeleteTexture(uint32_t id)
{
	if(!id || id >= textures.size() || textures[id].pixels.empty())
		return;
	textureBytes -= textures[id].pixels.size() * 4;
	ForgetOutlines(id);
	textures[id] = Texture();
	freeIds.push_back(id);
}



uint64_t Raster::TextureBytes()
{
	return textureBytes;
}



void Raster::Fill(double x, double y, double width, double height, const float color[4])
{
	ProfScope prof(PROF_FILL);
	int x0 = max(clipX0, static_cast<int>(lround((x - width / 2.) * scale + fbWidth / 2.)));
	int x1 = min(clipX1, static_cast<int>(lround((x + width / 2.) * scale + fbWidth / 2.)));
	int y0 = max(clipY0, static_cast<int>(lround((y - height / 2.) * scale + fbHeight / 2.)));
	int y1 = min(clipY1, static_cast<int>(lround((y + height / 2.) * scale + fbHeight / 2.)));
	uint32_t src = PackColor(color[0], color[1], color[2], color[3]);
	for(int py = y0; py < y1; ++py)
	{
		uint32_t *row = &frame[py * fbWidth];
		for(int px = x0; px < x1; ++px)
			row[px] = BlendPixel(row[px], src);
	}
}



namespace {
	// One textured parallelogram: pixel P = origin + s * axisS + t * axisT, s in [0, 1],
	// t in [0, tMax]; texture coordinate (s, v0 + t * dv). Everything else as in SpriteDraw.
	struct Quad {
		const Texture *tex = nullptr;
		float frame = 0.f;
		int frameCount = 1;
		double ox = 0., oy = 0., sx = 0., sy = 0., tx = 0., ty = 0.;
		double v0 = 0., dv = 1., tMax = 1.;
		// Texture u = s0 + s * ds (a motion-blurred sprite's quad reaches past the texture).
		double s0 = 0., ds = 1.;
		// Motion blur, in texture coordinates: samples at -blur, 0 and +blur, weighted 1:2:1.
		double blurU = 0., blurV = 0.;
		const float *swizzle = nullptr;
		const Texture *mask = nullptr;
		bool uniqueMaskFrames = false;
		float alpha = 1.f;
	};

	// Inverse of a parallelogram mapping: (s, t) of a pixel centre, s in [0, 1), t in [0, tMax).
	struct Mapping {
		double ox, oy, isX, isY, itX, itY, tMax;

		bool Inside(int px, int py) const
		{
			const double dx = px + .5 - ox, dy = py + .5 - oy;
			const double s = isX * dx + isY * dy, t = itX * dx + itY * dy;
			return s >= 0. && s < 1. && t >= 0. && t < tMax;
		}

		// The pixels of row py inside the shape, as [xa, xb), within [x0, x1). The shape is
		// convex, so solve each bound for x, widen by a pixel, then trim with the exact test.
		bool Span(int py, int x0, int x1, int &xa, int &xb) const
		{
			const double dy = py + .5 - oy;
			double lo = x0, hi = x1;
			auto bound = [&](double k, double c, double low, double high)
			{
				// low <= k * (px + .5 - ox) + c < high
				if(fabs(k) < 1e-12)
				{
					if(c < low || c >= high)
						hi = lo - 1.;
					return;
				}
				double a = (low - c) / k + ox - .5, b = (high - c) / k + ox - .5;
				if(k < 0.)
					swap(a, b);
				lo = max(lo, a);
				hi = min(hi, b);
			};
			bound(isX, isY * dy, 0., 1.);
			bound(itX, itY * dy, 0., tMax);
			if(hi < lo)
				return false;
			xa = max(x0, static_cast<int>(floor(lo)) - 1);
			xb = min(x1, static_cast<int>(ceil(hi)) + 2);
			while(xa < xb && !Inside(xa, py))
				++xa;
			while(xb > xa && !Inside(xb - 1, py))
				--xb;
			return xa < xb;
		}
	};

	// Per-channel mix of two premultiplied pixels, fade in 0..256.
	inline uint32_t MixPixel(uint32_t a, uint32_t b, uint32_t fade)
	{
		return ScalePixel(a, 256 - fade) + ScalePixel(b, fade);
	}

	// One texel at 16.16 fixed-point texel coordinates: the nearest, or the four around it
	// blended as GL_LINEAR does, clamped to the edges.
	template <bool LINEAR>
	inline uint32_t Sample(const uint32_t *layer, int w, int h, int32_t u, int32_t v)
	{
		if constexpr(!LINEAR)
			return layer[static_cast<size_t>(min(h - 1, max(0, v >> 16))) * w + min(w - 1, max(0, u >> 16))];
		else
		{
			u -= 0x8000;
			v -= 0x8000;
			const int x = u >> 16, y = v >> 16;
			const uint32_t fx = (u >> 8) & 0xFF, fy = (v >> 8) & 0xFF;
			const int x0 = min(w - 1, max(0, x)), x1 = min(w - 1, max(0, x + 1));
			const uint32_t *row0 = layer + static_cast<size_t>(min(h - 1, max(0, y))) * w;
			const uint32_t *row1 = layer + static_cast<size_t>(min(h - 1, max(0, y + 1))) * w;
			return MixPixel(MixPixel(row0[x0], row0[x1], fx), MixPixel(row1[x0], row1[x1], fx), fy);
		}
	}

	// Pixel loop for one quad; the template flags drop the work a sprite doesn't need.
	template <bool FADE, bool SWIZZLE, bool ALPHA, bool LINEAR, bool BLUR>
	void QuadRows(const Quad &q, const Mapping &m, int x0, int x1, int y0, int y1, const uint32_t *layerA,
		const uint32_t *layerB, int fade, const int *mat, const uint32_t *maskLayer, int alpha)
	{
		const Texture &tex = *q.tex;
		const int w = tex.width, h = tex.height;
		// 16.16 fixed-point texel coordinates, stepped along each row.
		const double uScale = w * 65536., vScale = h * 65536.;
		const int32_t du = static_cast<int32_t>(lround(m.isX * q.ds * uScale));
		const int32_t dv = static_cast<int32_t>(lround(m.itX * q.dv * vScale));
		const int32_t blurU = static_cast<int32_t>(lround(q.blurU * uScale));
		const int32_t blurV = static_cast<int32_t>(lround(q.blurV * vScale));
		const int mw = maskLayer ? q.mask->width : 0, mh = maskLayer ? q.mask->height : 0;
		const int32_t dmu = static_cast<int32_t>(lround(m.isX * q.ds * mw * 65536.));
		const int32_t dmv = static_cast<int32_t>(lround(m.itX * q.dv * mh * 65536.));
		for(int py = y0; py < y1; ++py)
		{
			int xa, xb;
			if(!m.Span(py, x0, x1, xa, xb))
				continue;
			const double dx = xa + .5 - m.ox, dy = py + .5 - m.oy;
			const double s = m.isX * dx + m.isY * dy, t = m.itX * dx + m.itY * dy;
			const double v = q.v0 + t * q.dv, textureS = q.s0 + s * q.ds;
			int32_t u = static_cast<int32_t>(textureS * uScale), vv = static_cast<int32_t>(v * vScale);
			int32_t mu = static_cast<int32_t>(textureS * mw * 65536.), mv = static_cast<int32_t>(v * mh * 65536.);
			uint32_t *out = &frame[py * fbWidth];
			// Unrotated sprites read one texture row per screen row (most of the interface).
			if constexpr(!FADE && !SWIZZLE && !LINEAR && !BLUR)
				if(!dv)
				{
					const uint32_t *source = layerA + static_cast<size_t>(min(h - 1, max(0, vv >> 16))) * w;
					for(int px = xa; px < xb; ++px, u += du)
					{
						uint32_t p = source[min(w - 1, max(0, u >> 16))];
						if(!p)
							continue;
						if constexpr(ALPHA)
							p = ScalePixel(p, alpha);
						out[px] = Raster::BlendPixel(out[px], p);
					}
					continue;
				}
			for(int px = xa; px < xb; ++px, u += du, vv += dv)
			{
				auto texel = [&](int32_t su, int32_t sv)
				{
					uint32_t texel = Sample<LINEAR>(layerA, w, h, su, sv);
					if constexpr(FADE)
						texel = MixPixel(texel, Sample<LINEAR>(layerB, w, h, su, sv), fade);
					return texel;
				};
				uint32_t p;
				if constexpr(BLUR)
					p = ScalePixel(texel(u - blurU, vv - blurV), 64) + ScalePixel(texel(u, vv), 128)
						+ ScalePixel(texel(u + blurU, vv + blurV), 64);
				else
					p = texel(u, vv);
				if constexpr(SWIZZLE)
				{
					if(p)
					{
						// GLSL "color * matrix", column-major: out[j] = dot(color, column j), in 8.8 fixed point.
						const int c[4] = {static_cast<int>((p >> 16) & 0xFF), static_cast<int>((p >> 8) & 0xFF),
							static_cast<int>(p & 0xFF), static_cast<int>(p >> 24)};
						int o[4];
						for(int j = 0; j < 4; ++j)
							o[j] = (c[0] * mat[4 * j] + c[1] * mat[4 * j + 1] + c[2] * mat[4 * j + 2] + c[3] * mat[4 * j + 3] + 128) >> 8;
						if(maskLayer)
						{
							const int mx = min(mw - 1, max(0, mu >> 16));
							const int my = min(mh - 1, max(0, mv >> 16));
							// The mask's red channel keeps the original colour.
							int f = (maskLayer[my * mw + mx] >> 16) & 0xFF;
							f += f >> 7;
							for(int j = 0; j < 4; ++j)
								o[j] = (c[j] * f + o[j] * (256 - f)) >> 8;
						}
						p = static_cast<uint32_t>(clamp(o[3], 0, 255)) << 24 | static_cast<uint32_t>(clamp(o[0], 0, 255)) << 16
							| static_cast<uint32_t>(clamp(o[1], 0, 255)) << 8 | static_cast<uint32_t>(clamp(o[2], 0, 255));
					}
					if(maskLayer)
					{
						mu += dmu;
						mv += dmv;
					}
				}
				if(!p)
					continue;
				if constexpr(ALPHA)
					p = ScalePixel(p, alpha);
				out[px] = Raster::BlendPixel(out[px], p);
			}
		}
	}

	void DrawQuad(const Quad &q)
	{
		const Texture &tex = *q.tex;
		const double det = q.sx * q.ty - q.tx * q.sy;
		if(fabs(det) < 1e-9 || q.alpha <= 0.f)
			return;

		double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
		for(double cs : {0., 1.})
			for(double ct : {0., q.tMax})
			{
				double x = q.ox + cs * q.sx + ct * q.tx, y = q.oy + cs * q.sy + ct * q.ty;
				minX = min(minX, x); maxX = max(maxX, x);
				minY = min(minY, y); maxY = max(maxY, y);
			}
		int x0 = max(clipX0, static_cast<int>(floor(minX)));
		int x1 = min(clipX1, static_cast<int>(ceil(maxX)));
		int y0 = max(clipY0, static_cast<int>(floor(minY)));
		int y1 = min(clipY1, static_cast<int>(ceil(maxY)));
		if(x0 >= x1 || y0 >= y1)
			return;

		// Inverse of [sx tx; sy ty]: (s, t) from a pixel offset.
		const Mapping m{q.ox, q.oy, q.ty / det, -q.tx / det, -q.sy / det, q.sx / det, q.tMax};

		const int frameCount = max(1, q.frameCount);
		int first = static_cast<int>(floor(q.frame)) % frameCount;
		int second = static_cast<int>(ceil(q.frame)) % frameCount;
		const int fade = static_cast<int>((q.frame - floor(q.frame)) * 256.f);
		first = min(first, tex.layers - 1);
		second = min(second, tex.layers - 1);
		const size_t layerSize = static_cast<size_t>(tex.width) * tex.height;
		const uint32_t *layerA = &tex.pixels[first * layerSize];
		const uint32_t *layerB = &tex.pixels[second * layerSize];

		const uint32_t *maskLayer = nullptr;
		if(q.mask)
		{
			int maskFrame = q.uniqueMaskFrames ? min(first, q.mask->layers - 1) : 0;
			maskLayer = &q.mask->pixels[maskFrame * static_cast<size_t>(q.mask->width) * q.mask->height];
		}
		int mat[16];
		if(q.swizzle)
			for(int i = 0; i < 16; ++i)
				mat[i] = static_cast<int>(lround(q.swizzle[i] * 256.f));
		const int alpha = static_cast<int>(clamp(q.alpha, 0.f, 1.f) * 256.f);

		const bool fades = fade && first != second;
		const bool swizzles = q.swizzle != nullptr;
		const bool alphas = alpha < 256;
		const int variant = fades * 4 + swizzles * 2 + alphas;
		const bool blurs = q.blurU || q.blurV;
		auto pick = [&](auto linear, auto blur)
		{
			constexpr bool L = decltype(linear)::value, B = decltype(blur)::value;
			auto run = [&](auto f) { f(q, m, x0, x1, y0, y1, layerA, layerB, fade, mat, maskLayer, alpha); };
			switch(variant)
			{
				case 0: run(QuadRows<false, false, false, L, B>); break;
				case 1: run(QuadRows<false, false, true, L, B>); break;
				case 2: run(QuadRows<false, true, false, L, B>); break;
				case 3: run(QuadRows<false, true, true, L, B>); break;
				case 4: run(QuadRows<true, false, false, L, B>); break;
				case 5: run(QuadRows<true, false, true, L, B>); break;
				case 6: run(QuadRows<true, true, false, L, B>); break;
				default: run(QuadRows<true, true, true, L, B>); break;
			}
		};
		if(linearFiltering)
		{
			if(blurs)
				pick(true_type{}, true_type{});
			else
				pick(true_type{}, false_type{});
		}
		else if(blurs)
			pick(false_type{}, true_type{});
		else
			pick(false_type{}, false_type{});
	}

	const Texture *Find(uint32_t id)
	{
		return (id && id < textures.size() && !textures[id].pixels.empty()) ? &textures[id] : nullptr;
	}
}



void Raster::DrawSprite(const SpriteDraw &item)
{
	ProfScope prof(PROF_SPRITE);
	Quad q;
	q.tex = Find(item.texture);
	if(!q.tex)
		return;
	// Screen pixel = (M * v + position) * scale + centre, for v in the unit square
	// (-0.5..0.5). GLSL mat2 is column-major: column 0 = (t0, t1), column 1 = (t2, t3).
	// A blurred sprite's quad grows by the blur on each side, its texture coordinates reaching
	// past the edges, as the GL vertex shader does.
	const double growU = 2. * fabs(item.blur[0]), growV = 2. * fabs(item.blur[1]);
	q.sx = item.transform[0] * scale * (1. + growU);
	q.sy = item.transform[1] * scale * (1. + growU);
	q.tx = item.transform[2] * scale * (1. + growV);
	q.ty = item.transform[3] * scale * (1. + growV);
	q.ox = item.position[0] * scale + fbWidth / 2. - .5 * (q.sx + q.tx);
	q.oy = item.position[1] * scale + fbHeight / 2. - .5 * (q.sy + q.ty);
	q.s0 = -.5 * growU;
	q.ds = 1. + growU;
	q.v0 = -.5 * growV;
	q.dv = 1. + growV;
	q.blurU = item.blur[0];
	q.blurV = item.blur[1];
	q.tMax = item.clip;
	q.frame = item.frame;
	q.frameCount = item.frameCount;
	q.swizzle = item.swizzle;
	if(item.swizzle)
		q.mask = Find(item.swizzleMask);
	q.uniqueMaskFrames = item.uniqueSwizzleMaskFrames;
	q.alpha = item.alpha;
	DrawQuad(q);
}



void Raster::DrawBatchQuad(uint32_t texture, float frame, int frameCount, const double corner[2],
	const double width[2], const double height[2], float clip, float alpha)
{
	ProfScope prof(PROF_BATCH);
	Quad q;
	q.tex = Find(texture);
	if(!q.tex)
		return;
	q.ox = corner[0] * scale + fbWidth / 2.;
	q.oy = corner[1] * scale + fbHeight / 2.;
	q.sx = width[0] * scale;
	q.sy = width[1] * scale;
	// The height vector already covers only the unclipped part.
	q.tx = height[0] * scale;
	q.ty = height[1] * scale;
	q.v0 = 1.;
	q.dv = -clip;
	q.frame = frame;
	q.frameCount = frameCount;
	q.alpha = alpha;
	DrawQuad(q);
}



void Raster::Darken(double left, double top, double width, double height,
	const uint8_t *grid, int columns, int rows)
{
	ProfScope prof(PROF_FOG);
	if(columns <= 0 || rows <= 0 || width == 0. || height == 0. || clipX1 <= clipX0)
		return;
	// Bilinear, clamp to edge, in 8-bit fixed point. Texel centres sit at (i + 0.5) / count.
	// The grid cell and weight of each screen column are the same on every row: work them out once.
	const int span = clipX1 - clipX0;
	vector<int> c0(span), c1(span), fx(span);
	int first = columns - 1, last = 0;
	for(int i = 0; i < span; ++i)
	{
		double u = ((clipX0 + i + .5 - fbWidth / 2.) / scale - left) / width * columns - .5;
		int cell = static_cast<int>(floor(u));
		c0[i] = clamp(cell, 0, columns - 1);
		c1[i] = clamp(cell + 1, 0, columns - 1);
		fx[i] = u < 0. ? 0 : static_cast<int>((u - cell) * 256. + .5);
		first = min(first, c0[i]);
		last = max(last, c1[i]);
	}
	// One row of the grid interpolated vertically, times 256.
	vector<int> line(columns);
	for(int py = clipY0; py < clipY1; ++py)
	{
		double v = ((py + .5 - fbHeight / 2.) / scale - top) / height * rows - .5;
		int cell = static_cast<int>(floor(v));
		const uint8_t *r0 = grid + clamp(cell, 0, rows - 1) * columns;
		const uint8_t *r1 = grid + clamp(cell + 1, 0, rows - 1) * columns;
		const int fy = v < 0. ? 0 : static_cast<int>(clamp(v - cell, 0., 1.) * 256. + .5);
		bool any = false;
		for(int c = first; c <= last; ++c)
		{
			line[c] = r0[c] * (256 - fy) + r1[c] * fy;
			any |= line[c] != 0;
		}
		if(!any)
			continue;
		uint32_t *row = &frame[py * fbWidth + clipX0];
		for(int i = 0; i < span; ++i)
		{
			const uint32_t a = (line[c0[i]] * (256 - fx[i]) + line[c1[i]] * fx[i] + (1 << 15)) >> 16;
			if(a)
				row[i] = BlendPixel(row[i], min(a, 255u) << 24);
		}
	}
}



namespace {
	// GL_LINEAR, clamp-to-edge sample of one layer, as weighted luminance-plus-alpha
	// (.4 R + .4 G + .4 B + A) the way the outline shader reads it.
	float WeightedSample(const Texture &tex, const uint32_t *layer, double s, double t)
	{
		double u = s * tex.width - .5, v = t * tex.height - .5;
		int x0 = clamp(static_cast<int>(floor(u)), 0, tex.width - 1);
		int y0 = clamp(static_cast<int>(floor(v)), 0, tex.height - 1);
		int x1 = clamp(static_cast<int>(floor(u)) + 1, 0, tex.width - 1);
		int y1 = clamp(static_cast<int>(floor(v)) + 1, 0, tex.height - 1);
		double fx = clamp(u - floor(u), 0., 1.), fy = clamp(v - floor(v), 0., 1.);
		auto w = [&](int x, int y) -> double
		{
			uint32_t p = layer[y * tex.width + x];
			return (.4 * (((p >> 16) & 0xFF) + ((p >> 8) & 0xFF) + (p & 0xFF)) + (p >> 24)) / 255.;
		};
		double a = w(x0, y0) + (w(x1, y0) - w(x0, y0)) * fx;
		double b = w(x0, y1) + (w(x1, y1) - w(x0, y1)) * fx;
		return static_cast<float>(a + (b - a) * fy);
	}

	float Sobel(const Texture &tex, const uint32_t *layer, double s, double t, double ox, double oy)
	{
		float sum = 0.f;
		for(int dy = -1; dy <= 1; ++dy)
			for(int dx = -1; dx <= 1; ++dx)
			{
				double cs = s + .618034 * ox * dx, ct = t + .618034 * oy * dy;
				auto at = [&](double ix, double iy) { return WeightedSample(tex, layer, cs + ix * ox, ct + iy * oy); };
				float nw = at(-1, -1), ne = at(1, -1), sw = at(-1, 1), se = at(1, 1);
				float h = nw + sw - ne - se + 2.f * (at(-1, 0) - at(1, 0));
				float v = nw + ne - sw - se + 2.f * (at(0, -1) - at(0, 1));
				sum += h * h + v * v;
			}
		return sum;
	}
}



namespace {
	// Outlines depend only on the sprite frame and its drawn size, not on rotation or
	// position, so each is computed once into an upright grid and reused while it lasts.
	struct OutlineImage {
		uint32_t texture;
		int layer, width, height;
		float off[2];
		// 0..255 for an outline strength of 0..2.
		vector<uint8_t> strength;
		uint64_t lastUsed;
	};
	vector<OutlineImage> outlines;
	uint64_t outlineClock = 0;
	constexpr size_t MAX_OUTLINES = 8;

	void ForgetOutlines(uint32_t id)
	{
		outlines.erase(remove_if(outlines.begin(), outlines.end(),
			[id](const OutlineImage &o) { return o.texture == id; }), outlines.end());
	}

	const OutlineImage &GetOutline(uint32_t id, const Texture &tex, int layer, int width, int height, const float off[2])
	{
		++outlineClock;
		for(OutlineImage &o : outlines)
			if(o.texture == id && o.layer == layer && o.width == width && o.height == height
					&& o.off[0] == off[0] && o.off[1] == off[1])
			{
				o.lastUsed = outlineClock;
				return o;
			}
		if(outlines.size() >= MAX_OUTLINES)
			outlines.erase(min_element(outlines.begin(), outlines.end(),
				[](const OutlineImage &a, const OutlineImage &b) { return a.lastUsed < b.lastUsed; }));
		OutlineImage o{id, layer, width, height, {off[0], off[1]}, vector<uint8_t>(static_cast<size_t>(width) * height),
			outlineClock};
		const uint32_t *pixels = &tex.pixels[layer * static_cast<size_t>(tex.width) * tex.height];
		for(int y = 0; y < height; ++y)
			for(int x = 0; x < width; ++x)
			{
				const float k = sqrt(Sobel(tex, pixels, (x + .5) / width, (y + .5) / height, off[0], off[1]) / 180.f);
				o.strength[y * width + x] = static_cast<uint8_t>(min(255.f, k * 127.5f + .5f));
			}
		outlines.push_back(std::move(o));
		return outlines.back();
	}
}



void Raster::DrawOutline(uint32_t texture, float frameIndex, int frameCount, const float position[2],
	const float transform[4], const float off[2], const float color[4])
{
	ProfScope prof(PROF_OUTLINE);
	const Texture *tex = Find(texture);
	if(!tex)
		return;
	const double sx = transform[0] * scale, sy = transform[1] * scale;
	const double tx = transform[2] * scale, ty = transform[3] * scale;
	const double ox = position[0] * scale + fbWidth / 2. - .5 * (sx + tx);
	const double oy = position[1] * scale + fbHeight / 2. - .5 * (sy + ty);
	const double det = sx * ty - tx * sy;
	if(fabs(det) < 1e-9)
		return;
	double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
	for(double cs : {0., 1.})
		for(double ct : {0., 1.})
		{
			double x = ox + cs * sx + ct * tx, y = oy + cs * sy + ct * ty;
			minX = min(minX, x); maxX = max(maxX, x);
			minY = min(minY, y); maxY = max(maxY, y);
		}
	const int x0 = max(clipX0, static_cast<int>(floor(minX))), x1 = min(clipX1, static_cast<int>(ceil(maxX)));
	const int y0 = max(clipY0, static_cast<int>(floor(minY))), y1 = min(clipY1, static_cast<int>(ceil(maxY)));
	if(x0 >= x1 || y0 >= y1)
		return;

	// shortcut: animated sprites use the nearest frame instead of blending two outlines.
	const int count = max(1, frameCount);
	const int layer = min(static_cast<int>(lround(frameIndex)) % count, tex->layers - 1);
	const int width = max(1, static_cast<int>(lround(hypot(sx, sy))));
	const int height = max(1, static_cast<int>(lround(hypot(tx, ty))));
	const OutlineImage &o = GetOutline(texture, *tex, layer, width, height, off);

	int c[4];
	for(int i = 0; i < 4; ++i)
		c[i] = static_cast<int>(clamp(color[i], 0.f, 1.f) * 255.f + .5f);
	const Mapping m{ox, oy, ty / det, -tx / det, -sy / det, sx / det, 1.};
	const int32_t du = static_cast<int32_t>(lround(m.isX * width * 65536.));
	const int32_t dv = static_cast<int32_t>(lround(m.itX * height * 65536.));
	for(int py = y0; py < y1; ++py)
	{
		int xa, xb;
		if(!m.Span(py, x0, x1, xa, xb))
			continue;
		const double dx = xa + .5 - ox, dy = py + .5 - oy;
		int32_t u = static_cast<int32_t>((m.isX * dx + m.isY * dy) * width * 65536.);
		int32_t v = static_cast<int32_t>((m.itX * dx + m.itY * dy) * height * 65536.);
		uint32_t *row = &frame[py * fbWidth];
		for(int px = xa; px < xb; ++px, u += du, v += dv)
		{
			const int k = o.strength[min(height - 1, max(0, v >> 16)) * width + min(width - 1, max(0, u >> 16))];
			if(!k)
				continue;
			auto ch = [&](int i) { return static_cast<uint32_t>(min(255, (c[i] * k + 64) >> 7)); };
			row[px] = BlendPixel(row[px], ch(3) << 24 | ch(0) << 16 | ch(1) << 8 | ch(2));
		}
	}
}



namespace {
	// A glyph rendered once at its on-screen size and sub-pixel position (in eighths).
	struct GlyphImage {
		int left, top, width, height;
		vector<uint8_t> alpha;
	};
	unordered_map<uint64_t, GlyphImage> glyphCache;
	unordered_map<const uint8_t *, uint32_t> atlasIds;
	// About 5,000 glyphs of a few hundred bytes each; past that, start over.
	constexpr size_t MAX_GLYPHS = 6000;

	// Bilinear, clamped to the glyph's cell, sampled at pixel centres: the GL text shader's look.
	GlyphImage RenderGlyph(const uint8_t *atlas, int atlasWidth, int atlasHeight, int glyphs, int glyph,
		double left, double top, double pixelWidth, double pixelHeight)
	{
		GlyphImage g;
		g.left = static_cast<int>(floor(left));
		g.top = static_cast<int>(floor(top));
		g.width = static_cast<int>(ceil(left + pixelWidth)) - g.left;
		g.height = static_cast<int>(ceil(top + pixelHeight)) - g.top;
		g.alpha.assign(static_cast<size_t>(g.width) * g.height, 0);
		const double cellWidth = static_cast<double>(atlasWidth) / glyphs;
		const double cellLeft = glyph * cellWidth;
		const int minCol = static_cast<int>(cellLeft);
		const int maxCol = min(atlasWidth - 1, static_cast<int>(cellLeft + cellWidth) - 1);
		for(int y = 0; y < g.height; ++y)
		{
			const double v = (g.top + y + .5 - top) / pixelHeight * atlasHeight - .5;
			const int r0 = clamp(static_cast<int>(floor(v)), 0, atlasHeight - 1);
			const int r1 = clamp(static_cast<int>(floor(v)) + 1, 0, atlasHeight - 1);
			const double wy = clamp(v - floor(v), 0., 1.);
			for(int x = 0; x < g.width; ++x)
			{
				const double u = cellLeft + (g.left + x + .5 - left) / pixelWidth * cellWidth - .5;
				const int c0 = clamp(static_cast<int>(floor(u)), minCol, maxCol);
				const int c1 = clamp(static_cast<int>(floor(u)) + 1, minCol, maxCol);
				const double wx = clamp(u - floor(u), 0., 1.);
				const double a = atlas[r0 * atlasWidth + c0] + (atlas[r0 * atlasWidth + c1] - atlas[r0 * atlasWidth + c0]) * wx;
				const double b = atlas[r1 * atlasWidth + c0] + (atlas[r1 * atlasWidth + c1] - atlas[r1 * atlasWidth + c0]) * wx;
				g.alpha[y * g.width + x] = static_cast<uint8_t>(clamp(a + (b - a) * wy + .5, 0., 255.));
			}
		}
		// Positions become offsets from the pixel the glyph's origin falls in.
		g.left -= static_cast<int>(floor(left));
		g.top -= static_cast<int>(floor(top));
		return g;
	}
}



void Raster::DrawGlyph(const uint8_t *atlas, int atlasWidth, int atlasHeight, int glyphs, int glyph,
	double x, double y, double width, double height, const float color[4])
{
	if(!atlas || width <= 0. || height <= 0.)
		return;
	const double left = x * scale + fbWidth / 2., top = y * scale + fbHeight / 2.;
	const double pixelWidth = width * scale, pixelHeight = height * scale;
	int baseX = static_cast<int>(floor(left)), baseY = static_cast<int>(floor(top));
	int phaseX = static_cast<int>(lround((left - baseX) * 8.)), phaseY = static_cast<int>(lround((top - baseY) * 8.));
	if(phaseX == 8)
		++baseX, phaseX = 0;
	if(phaseY == 8)
		++baseY, phaseY = 0;

	auto atlasIt = atlasIds.try_emplace(atlas, static_cast<uint32_t>(atlasIds.size())).first;
	const uint64_t key = static_cast<uint64_t>(atlasIt->second) << 56 | static_cast<uint64_t>(glyph & 0xFF) << 48
		| static_cast<uint64_t>(lround(pixelWidth * 64.) & 0xFFFF) << 32 | static_cast<uint64_t>(lround(pixelHeight * 64.) & 0xFFFF) << 16
		| phaseX << 8 | phaseY;
	auto it = glyphCache.find(key);
	if(it == glyphCache.end())
	{
		if(glyphCache.size() >= MAX_GLYPHS)
			glyphCache.clear();
		it = glyphCache.emplace(key, RenderGlyph(atlas, atlasWidth, atlasHeight, glyphs, glyph,
			phaseX / 8., phaseY / 8., pixelWidth, pixelHeight)).first;
	}
	const GlyphImage &g = it->second;

	int c[4];
	for(int i = 0; i < 4; ++i)
		c[i] = static_cast<int>(clamp(color[i], 0.f, 1.f) * 255.f + .5f);
	const uint32_t packed = static_cast<uint32_t>(c[3]) << 24 | c[0] << 16 | c[1] << 8 | c[2];
	const int gx = baseX + g.left, gy = baseY + g.top;
	const int x0 = max(clipX0, gx), x1 = min(clipX1, gx + g.width);
	const int y0 = max(clipY0, gy), y1 = min(clipY1, gy + g.height);
	for(int py = y0; py < y1; ++py)
	{
		uint32_t *row = &frame[py * fbWidth];
		const uint8_t *a = g.alpha.data() + (py - gy) * g.width;
		for(int px = x0; px < x1; ++px)
			if(const uint32_t k = a[px - gx])
				row[px] = BlendPixel(row[px], k == 255 ? packed : ScalePixel(packed, k + (k >> 7)));
	}
}



void Raster::PushTarget(uint32_t *pixels, int width, int height)
{
	targets.push_back({frame, fbWidth, fbHeight, clipX0, clipY0, clipX1, clipY1});
	frame = pixels;
	fbWidth = width;
	fbHeight = height;
	ResetClip();
}



void Raster::PopTarget()
{
	if(targets.empty())
		return;
	const Target &t = targets.back();
	frame = t.pixels;
	fbWidth = t.width;
	fbHeight = t.height;
	clipX0 = t.x0;
	clipY0 = t.y0;
	clipX1 = t.x1;
	clipY1 = t.y1;
	targets.pop_back();
}



void Raster::Composite(const uint32_t *pixels, int width, int height, const double size[2],
	const double position[2], const double clip[2], const double source[2], const float fade[4])
{
	ProfScope prof(PROF_COMPOSITE);
	if(!pixels || width <= 0 || height <= 0 || clip[0] <= 0. || clip[1] <= 0.)
		return;
	const int x0 = max(clipX0, static_cast<int>(floor((position[0] - clip[0] / 2.) * scale + fbWidth / 2.)));
	const int x1 = min(clipX1, static_cast<int>(ceil((position[0] + clip[0] / 2.) * scale + fbWidth / 2.)));
	const int y0 = max(clipY0, static_cast<int>(floor((position[1] - clip[1] / 2.) * scale + fbHeight / 2.)));
	const int y1 = min(clipY1, static_cast<int>(ceil((position[1] + clip[1] / 2.) * scale + fbHeight / 2.)));
	if(x0 >= x1 || y0 >= y1)
		return;
	// Fade distances as fractions of the shown window: top, bottom, then the two sides.
	const double eps = .001;
	auto toWeight = [](double w) { return static_cast<int>(clamp(w, 0., 1.) * 256.); };
	// Per column: the source pixel (or -1 outside the window) and the side fade, 0..256.
	vector<int> columnSource(x1 - x0), columnWeight(x1 - x0);
	for(int px = x0; px < x1; ++px)
	{
		const double vx = ((px + .5 - fbWidth / 2.) / scale - position[0]) / clip[0] + .5;
		const double tx = (vx * clip[0] + source[0]) / size[0];
		const bool inside = vx >= 0. && vx < 1. && tx > 0. && tx < 1.;
		columnSource[px - x0] = inside ? min(width - 1, static_cast<int>(tx * width)) : -1;
		columnWeight[px - x0] = toWeight(min((vx + eps) / (fade[2] / clip[0] + eps),
			(1. - vx + eps) / (fade[3] / clip[0] + eps)));
	}
	for(int py = y0; py < y1; ++py)
	{
		const double vy = ((py + .5 - fbHeight / 2.) / scale - position[1]) / clip[1] + .5;
		const double ty = (vy * clip[1] + source[1]) / size[1];
		if(vy < 0. || vy >= 1. || ty <= 0. || ty >= 1.)
			continue;
		const int sy = min(height - 1, static_cast<int>(ty * height));
		const int weightY = toWeight(min((vy + eps) / (fade[0] / clip[1] + eps),
			(1. - vy + eps) / (fade[1] / clip[1] + eps)));
		if(!weightY)
			continue;
		uint32_t *row = &frame[py * fbWidth + x0];
		const uint32_t *src = &pixels[sy * width];
		for(int i = 0; i < x1 - x0; ++i)
		{
			const int sx = columnSource[i];
			if(sx < 0)
				continue;
			uint32_t p = src[sx];
			if(!p)
				continue;
			const int weight = min(weightY, columnWeight[i]);
			if(weight < 256)
				p = ScalePixel(p, weight);
			row[i] = BlendPixel(row[i], p);
		}
	}
}
