/* StarField.cpp
Copyright (c) 2014 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "StarField.h"

#include "../Angle.h"
#include "../Body.h"
#include "DrawList.h"
#include "../GameData.h"
#include "../Interface.h"
#include "../pi.h"
#include "../Preferences.h"
#include "../Random.h"
#include "../Screen.h"
#include "../image/Sprite.h"
#include "../image/SpriteSet.h"
#include "../System.h"

#ifdef ES_SOFT
#include "../soft/Raster.h"
#endif

#include <algorithm>
#include <cmath>
#include <numeric>

using namespace std;

namespace {
	const int TILE_SIZE = 256;
	// The star field tiles in 4000 pixel increments. Have the tiling of the haze
	// field be as different from that as possible. (Note: this may need adjusting
	// in the future if monitors larger than this width ever become commonplace.)
	const double HAZE_WRAP = 6627.;
	// Don't let two haze patches be closer to each other than this distance. This
	// avoids having very bright haze where several patches overlap.
	const double HAZE_DISTANCE = 1200.;
	// This is how many haze fields should be drawn.
	const size_t HAZE_COUNT = 16;
	// This is how fast the crossfading of previous haze and current haze is
	const double FADE_PER_FRAME = 0.01;
	// An additional zoom factor applied to stars/haze on top of the base zoom, to simulate parallax.
	const double STAR_ZOOM = 0.70;
	const double HAZE_ZOOM = 0.90;

	void AddHaze(DrawList &drawList, const vector<Body> &haze,
		const Point &topLeft, const Point &bottomRight, double transparency)
	{
		for(auto &&it : haze)
		{
			// Figure out the position of the first instance of this haze that is to
			// the right of and below the top left corner of the screen.
			double startX = fmod(it.Position().X() - topLeft.X(), HAZE_WRAP);
			startX += topLeft.X() + HAZE_WRAP * (startX < 0.);
			double startY = fmod(it.Position().Y() - topLeft.Y(), HAZE_WRAP);
			startY += topLeft.Y() + HAZE_WRAP * (startY < 0.);

			// Draw any instances of this haze that are on screen.
			for(double y = startY; y < bottomRight.Y(); y += HAZE_WRAP)
				for(double x = startX; x < bottomRight.X(); x += HAZE_WRAP)
					drawList.Add(it, Point(x, y), transparency);
		}
	}
}



void StarField::Init(int stars, int width)
{
#ifndef ES_SOFT
	SetUpGraphics();
#endif
	MakeStars(stars, width);

	lastSprite = SpriteSet::Get("_menu/haze");
	for(size_t i = 0; i < HAZE_COUNT; ++i)
	{
		Point next;
		bool overlaps = true;
		while(overlaps)
		{
			next = Point(Random::Real() * HAZE_WRAP, Random::Real() * HAZE_WRAP);
			overlaps = false;
			for(const Body &other : haze[0])
			{
				Point previous = other.Position();
				double dx = remainder(previous.X() - next.X(), HAZE_WRAP);
				double dy = remainder(previous.Y() - next.Y(), HAZE_WRAP);
				if(dx * dx + dy * dy < HAZE_DISTANCE * HAZE_DISTANCE)
				{
					overlaps = true;
					break;
				}
			}
		}
		haze[0].emplace_back(lastSprite, next, Point(), Angle::Random(), 8.);
	}
	haze[1].assign(haze[0].begin(), haze[0].end());
}



void StarField::FinishLoading()
{
	const Interface *constants = GameData::Interfaces().Get("starfield");
	fixedZoom = constants->GetValue("fixed zoom");
	velocityReducer = constants->GetValue("velocity reducer");

	minZoom = max(0., constants->GetValue("minimum zoom"));
	zoomClamp = constants->GetValue("start clamping zoom");
	clampSlope = max(0., (zoomClamp - minZoom) / zoomClamp);
}



const Point &StarField::Position() const
{
	return pos;
}



void StarField::SetPosition(const Point &position)
{
	pos = position;
}



void StarField::SetHaze(const Sprite *sprite, bool allowAnimation)
{
	// If no sprite is given, set the default one.
	if(!sprite)
		sprite = SpriteSet::Get("_menu/haze");

	for(Body &body : haze[0])
		body.SetSprite(sprite);

	if(allowAnimation && sprite != lastSprite)
	{
		transparency = 1.;
		for(Body &body : haze[1])
			body.SetSprite(lastSprite);
	}
	lastSprite = sprite;
}



void StarField::Step(Point vel, double zoom)
{
	if(Preferences::Has(Preferences::FIXED_STARFIELD_ZOOM))
	{
		baseZoom = fixedZoom;
		vel /= velocityReducer;
	}
	else if(zoom < zoomClamp)
	{
		// When the player's view zoom gets too small, the starfield begins to take up
		// an extreme amount of system resources, and the tiling becomes very obvious.
		// If the view zoom gets below the zoom clamp value (default 0.25), start zooming
		// the starfield at a different rate, and don't go below the minimum zoom value
		// (default 0.15) for the starfield's zoom. 0.25 is the vanilla minimum zoom, so
		// this only applies when the "main view" interface has been modified to allow
		// lower zoom values.
		baseZoom = clampSlope * zoom + minZoom;
		// Reduce the movement of the background by the same adjustment as the zoom
		// so that the background doesn't appear like it's moving way quicker than
		// the player is.
		vel /= baseZoom / zoom;
	}
	else
		baseZoom = zoom;

	pos += vel;
}



void StarField::Draw(const Point &blur, const System *system) const
{
#ifdef ES_SOFT
	Raster::ProfScope prof(Raster::PROF_STARS);
#endif
	double density = system ? system->StarfieldDensity() : 1.;

	// Check preferences for the parallax quality.
	const auto parallaxSetting = Preferences::GetBackgroundParallax();
	int layers = (parallaxSetting == Preferences::BackgroundParallax::FANCY) ? 3 : 1;
	bool isParallax = (parallaxSetting == Preferences::BackgroundParallax::FANCY ||
						parallaxSetting == Preferences::BackgroundParallax::FAST);

	// Draw the starfield unless it is disabled in the preferences.
	double zoom = baseZoom;
	if(Preferences::Has(Preferences::DRAW_STARFIELD) && density > 0.)
	{
#ifndef ES_SOFT
		glUseProgram(shader->Object());
		if(OpenGL::HasVaoSupport())
			glBindVertexArray(vao);
		else
		{
			glBindBuffer(GL_ARRAY_BUFFER, vbo);
			EnableAttribArrays();
		}
#endif

		for(int pass = 1; pass <= layers; pass++)
		{
			// Modify zoom for the first parallax layer.
			if(isParallax)
				zoom = baseZoom * STAR_ZOOM * pow(pass, 0.2);

			float length = blur.Length();
			Point unit = length ? blur.Unit() : Point(1., 0.);
			// Don't zoom the stars at the same rate as the field; otherwise, at the
			// farthest out zoom they are too small to draw well.
			unit /= pow(zoom, .75);

#ifdef ES_SOFT
			const double elongation = length * zoom;
			const double brightness = min(1., pow(zoom, .5));
			const double unitSquared = unit.Dot(unit);
			// Drawn at 0.625 scale, stars shrink below a pixel; bigger and brighter keeps them visible.
			constexpr double SIZE_BOOST = 1.6;
			constexpr double BRIGHTNESS_BOOST = 1.5;
			const double pixelScale = Raster::Scale();
			const double halfWidth = Raster::Width() / 2., halfHeight = Raster::Height() / 2.;
			const int screenWidth = Raster::Width();
			uint32_t *pixels = Raster::Pixels();
			int clipX0, clipY0, clipX1, clipY1;
			Raster::GetClip(clipX0, clipY0, clipX1, clipY1);
			const double toLogical = 1. / (pixelScale * zoom * unitSquared);
			const float ax = unit.Y() * toLogical, ay = -unit.X() * toLogical;
			const float bx = unit.X() * toLogical, by = unit.Y() * toLogical;
			const float det = ax * by - ay * bx;
			// How far (pixels, up or down) the biggest star reaches from its centre: tiles and
			// stars beyond that from a band of rows can be skipped without any other work.
			constexpr float MAX_STAR_SIZE = 35 * .0625f * SIZE_BOOST;
			const float maxReach = max(fabs(MAX_STAR_SIZE * bx / det),
				fabs((MAX_STAR_SIZE + static_cast<float>(elongation)) * ax / det)) + 2.f;
			const double screenPerWorld = zoom * pixelScale;
			// Corner directions of a star of size 1 (A) and of stretch 1 (B), in pixels.
			const float cornerDirA[2] = {by / det, -bx / det};
			const float cornerDirB[2] = {-ay / det, ax / det};
#else
			float baseZoom = static_cast<float>(2. * zoom);
			GLfloat scale[2] = {baseZoom / Screen::Width(), -baseZoom / Screen::Height()};
			glUniform2fv(scaleI, 1, scale);

			GLfloat rotate[4] = {
				static_cast<float>(unit.Y()), static_cast<float>(-unit.X()),
				static_cast<float>(unit.X()), static_cast<float>(unit.Y())};
			glUniformMatrix2fv(rotateI, 1, false, rotate);

			glUniform1f(elongationI, length * zoom);
			glUniform1f(brightnessI, min(1., pow(zoom, .5)));
#endif

			// Stars this far beyond the border may still overlap the screen.
			double borderX = fabs(blur.X()) + 1.;
			double borderY = fabs(blur.Y()) + 1.;
			// Find the absolute bounds of the star field we must draw.
			int minX = pos.X() + (Screen::Left() - borderX) / zoom;
			int minY = pos.Y() + (Screen::Top() - borderY) / zoom;
			int maxX = pos.X() + (Screen::Right() + borderX) / zoom;
			int maxY = pos.Y() + (Screen::Bottom() + borderY) / zoom;
			// Round down to the start of the nearest tile.
			minX &= ~(TILE_SIZE - 1l);
			minY &= ~(TILE_SIZE - 1l);

#ifdef ES_SOFT
			// Two bands of rows at once (Raster::ParallelRows): each visits every star, but only
			// draws the rows in its band.
			Raster::ParallelRows(clipY0, clipY1, static_cast<int64_t>(clipY1 - clipY0) * (clipX1 - clipX0),
				[&](int bandY0, int bandY1)
			{
#endif
			for(int gy = minY; gy < maxY; gy += TILE_SIZE)
			{
#ifdef ES_SOFT
				const double tileTop = (gy - pos.Y()) * screenPerWorld + halfHeight;
				if(tileTop - maxReach >= bandY1 || tileTop + TILE_SIZE * screenPerWorld + maxReach < bandY0)
					continue;
#endif
				for(int gx = minX; gx < maxX; gx += TILE_SIZE)
				{
					Point off = Point(gx, gy) - pos;
#ifndef ES_SOFT
					GLfloat translate[2] = {
						static_cast<float>(off.X()),
						static_cast<float>(off.Y())
					};
					glUniform2fv(translateI, 1, translate);
#endif

					int index = (gx & widthMod) / TILE_SIZE + ((gy & widthMod) / TILE_SIZE) * tileCols;
					int first = tileIndex[index];
					int count = (tileIndex[index + 1] - first) * density / layers;
#ifdef ES_SOFT
					// Each star is a diamond, stretched along the motion and fading to its edges:
					// alpha = fade * (1 - |ea| / size - |eb| / (size + elongation)), where ea and eb
					// are linear in the pixel offset from the star's centre. Single precision, and
					// nothing divided per pixel or row: this runs for a thousand stars a frame.
					const float tileX = static_cast<float>(off.X() * screenPerWorld + halfWidth);
					const float tileY = static_cast<float>(off.Y() * screenPerWorld + halfHeight);
					const float perWorld = static_cast<float>(screenPerWorld);
					const int begin = first + (pass - 1) * count;
					for(int i = begin; i < begin + count / pass; ++i)
					{
						const float *star = &starData[3 * i];
						const float cy = tileY + star[1] * perWorld;
						if(cy + maxReach < bandY0 || cy - maxReach >= bandY1)
							continue;
						const float size = star[2] * static_cast<float>(SIZE_BOOST);
						const float stretched = size + static_cast<float>(elongation);
						const float fade = static_cast<float>(BRIGHTNESS_BOOST * (brightness * (4. / (4. + elongation)) * star[2] * .2 + .05));
						const float invA = 1.f / size, invB = 1.f / stretched;
						const float cx = tileX + star[0] * perWorld;
						// The diamond's corners (ea = +-size or eb = +-stretched), as pixel offsets.
						const float cornerA[2] = {size * cornerDirA[0], size * cornerDirA[1]};
						const float cornerB[2] = {stretched * cornerDirB[0], stretched * cornerDirB[1]};
						const float extentX = max(fabsf(cornerA[0]), fabsf(cornerB[0]));
						const float extentY = max(fabsf(cornerA[1]), fabsf(cornerB[1]));
						if(Raster::profOn)
							Raster::profStars.fetch_add(1, memory_order_relaxed);
						const int y0 = max(bandY0, static_cast<int>(floorf(cy - extentY)));
						const int y1 = min(bandY1, static_cast<int>(ceilf(cy + extentY)) + 1);
						// Small stars: every pixel of the bounding box (the alpha test trims it). Long
						// streaks: only the pixels between the row's edges.
						const bool streak = extentX * extentY > 16.f;
						const float corners[4][2] = {{cornerA[0], cornerA[1]}, {cornerB[0], cornerB[1]},
							{-cornerA[0], -cornerA[1]}, {-cornerB[0], -cornerB[1]}};
						for(int py = y0; py < y1; ++py)
						{
							const float dy = py + .5f - cy;
							float left = -extentX, right = extentX;
							if(streak)
							{
								left = 1e9f;
								right = -1e9f;
								for(int e = 0; e < 4; ++e)
								{
									const float *a = corners[e], *b = corners[(e + 1) % 4];
									if((a[1] - dy) * (b[1] - dy) > 0.f || a[1] == b[1])
										continue;
									const float x = a[0] + (b[0] - a[0]) * (dy - a[1]) / (b[1] - a[1]);
									left = min(left, x);
									right = max(right, x);
								}
								if(left > right)
									continue;
							}
							// A pixel of margin; the alpha test below trims it.
							const int x0 = max(clipX0, static_cast<int>(floorf(cx + left - .5f)));
							const int x1 = min(clipX1, static_cast<int>(ceilf(cx + right + .5f)));
							if(x0 >= x1)
								continue;
							if(Raster::profOn)
								Raster::profStarPixels.fetch_add(x1 - x0, memory_order_relaxed);
							uint32_t *row = pixels + py * screenWidth;
							// ea and eb step by ax and bx from one pixel to the next.
							const float dx0 = x0 + .5f - cx;
							float ea = ax * dx0 + ay * dy, eb = bx * dx0 + by * dy;
							for(int px = x0; px < x1; ++px, ea += ax, eb += bx)
							{
								const float alpha = fade * (1.f - fabsf(ea) * invA - fabsf(eb) * invB);
								if(alpha <= 0.f)
									continue;
								// White, premultiplied: every channel equals the alpha.
								const uint32_t a = alpha >= 1.f ? 255u : static_cast<uint32_t>(alpha * 255.f + .5f);
								row[px] = Raster::BlendPixel(row[px], a * 0x01010101u);
							}
						}
					}
#else
					glDrawArrays(GL_TRIANGLES, 6 * (first + (pass - 1) * count), 6 * (count / pass));
#endif
				}
			}
#ifdef ES_SOFT
			// Each piece visits every star: a few big pieces, not many small ones.
			}, 60);
#endif
		}
#ifndef ES_SOFT
		if(OpenGL::HasVaoSupport())
			glBindVertexArray(0);
		else
		{
			glDisableVertexAttribArray(offsetI);
			glDisableVertexAttribArray(sizeI);
			glDisableVertexAttribArray(cornerI);
			glBindBuffer(GL_ARRAY_BUFFER, 0);
		}
		glUseProgram(0);
#endif
	}

	// Draw the background haze unless it is disabled in the preferences.
	if(!Preferences::Has(Preferences::DRAW_BACKGROUND_HAZE))
		return;

	// Modify zoom for the second parallax layer.
	if(isParallax)
		zoom = baseZoom * HAZE_ZOOM;

	DrawList drawList;
	drawList.Clear(0, zoom);
	drawList.SetCenter(pos);

	if(transparency > FADE_PER_FRAME)
		transparency -= FADE_PER_FRAME;
	else
		transparency = 0.;

	// Any object within this range must be drawn. Some haze sprites may repeat
	// more than once if the view covers a very large area.
	Point size = Point(1., 1.) * haze[0].front().Radius();
	Point topLeft = pos + Screen::TopLeft() / zoom - size;
	Point bottomRight = pos + Screen::BottomRight() / zoom + size;
	if(transparency > 0.)
		AddHaze(drawList, haze[1], topLeft, bottomRight, 1 - transparency);
	AddHaze(drawList, haze[0], topLeft, bottomRight, transparency);

#ifdef ES_SOFT
	Raster::SetSmoothingAllowed(false);
#endif
	drawList.Draw();
#ifdef ES_SOFT
	Raster::SetSmoothingAllowed(true);
#endif
}



void StarField::EnableAttribArrays() const
{
#ifndef ES_SOFT
	// Connect the xy to the "vert" attribute of the vertex shader.
	constexpr auto stride = 4 * sizeof(GLfloat);
	glEnableVertexAttribArray(offsetI);
	glVertexAttribPointer(offsetI, 2, GL_FLOAT, GL_FALSE,
		stride, nullptr);

	glEnableVertexAttribArray(sizeI);
	glVertexAttribPointer(sizeI, 1, GL_FLOAT, GL_FALSE,
		stride, reinterpret_cast<const GLvoid *>(2 * sizeof(GLfloat)));

	glEnableVertexAttribArray(cornerI);
	glVertexAttribPointer(cornerI, 1, GL_FLOAT, GL_FALSE,
		stride, reinterpret_cast<const GLvoid *>(3 * sizeof(GLfloat)));
#endif
}



void StarField::SetUpGraphics()
{
#ifndef ES_SOFT
	shader = GameData::Shaders().Get("starfield");
	if(!shader->Object())
		throw runtime_error("Could not find starfield shader!");

	// make and bind the VAO
	if(OpenGL::HasVaoSupport())
	{
		glGenVertexArrays(1, &vao);
		glBindVertexArray(vao);
	}

	// make and bind the VBO
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);

	offsetI = shader->Attrib("offset");
	sizeI = shader->Attrib("size");
	cornerI = shader->Attrib("corner");

	scaleI = shader->Uniform("scale");
	rotateI = shader->Uniform("rotate");
	elongationI = shader->Uniform("elongation");
	translateI = shader->Uniform("translate");
	brightnessI = shader->Uniform("brightness");
#endif
}



void StarField::MakeStars(int stars, int width)
{
	// We can only work with power-of-two widths above 256.
	if(width < TILE_SIZE || (width & (width - 1)))
		return;

	widthMod = width - 1;

	tileCols = (width / TILE_SIZE);
	tileIndex.clear();
	tileIndex.resize(static_cast<size_t>(tileCols) * tileCols, 0);

	vector<int> off;
	static const int MAX_OFF = 50;
	static const int MAX_D = MAX_OFF * MAX_OFF;
	static const int MIN_D = MAX_D / 4;
	off.reserve(MAX_OFF * MAX_OFF * 5);
	for(int x = -MAX_OFF; x <= MAX_OFF; ++x)
		for(int y = -MAX_OFF; y <= MAX_OFF; ++y)
		{
			int d = x * x + y * y;
			if(d < MIN_D || d > MAX_D)
				continue;

			off.push_back(x);
			off.push_back(y);
		}

	// Generate random points in a temporary vector.
	// Keep track of how many fall into each tile, for sorting out later.
	vector<int> temp;
	temp.reserve(2 * stars);

	int x = Random::Int(width);
	int y = Random::Int(width);
	for(int i = 0; i < stars; ++i)
	{
		for(int j = 0; j < 10; ++j)
		{
			int index = Random::Int(static_cast<uint32_t>(off.size())) & ~1;
			x += off[index];
			y += off[index + 1];
			x &= widthMod;
			y &= widthMod;
		}
		temp.push_back(x);
		temp.push_back(y);
		int index = (x / TILE_SIZE) + (y / TILE_SIZE) * tileCols;
		++tileIndex[index];
	}

	// Accumulate item counts so that tileIndex[i] is the index in the array of
	// the first star that falls within tile i, and tileIndex.back() == stars.
	tileIndex.insert(tileIndex.begin(), 0);
	tileIndex.pop_back();
	partial_sum(tileIndex.begin(), tileIndex.end(), tileIndex.begin());

	// Each star consists of six vertices, each with four data elements.
#ifdef ES_SOFT
	starData.assign(3 * stars, 0.f);
#else
	vector<GLfloat> data(6 * 4 * stars, 0.f);
#endif
	for(auto it = temp.begin(); it != temp.end(); )
	{
		// Figure out what tile this star is in.
		int x = *it++;
		int y = *it++;
		int index = (x / TILE_SIZE) + (y / TILE_SIZE) * tileCols;

		// Randomize its sub-pixel position and its size / brightness.
		int random = Random::Int(4096);
		float fx = (x & (TILE_SIZE - 1)) + (random & 15) * 0.0625f;
		float fy = (y & (TILE_SIZE - 1)) + (random >> 8) * 0.0625f;
		float size = (((random >> 4) & 15) + 20) * 0.0625f;

#ifdef ES_SOFT
		float *starIt = &starData[3 * tileIndex[index]++];
		starIt[0] = fx;
		starIt[1] = fy;
		starIt[2] = size;
#else
		// Fill in the data array.
		auto dataIt = data.begin() + 6 * 4 * tileIndex[index]++;
		const float CORNER[6] = {
			static_cast<float>(0. * PI),
			static_cast<float>(.5 * PI),
			static_cast<float>(1.5 * PI),
			static_cast<float>(.5 * PI),
			static_cast<float>(1.5 * PI),
			static_cast<float>(1. * PI)
		};
		for(float corner : CORNER)
		{
			*dataIt++ = fx;
			*dataIt++ = fy;
			*dataIt++ = size;
			*dataIt++ = corner;
		}
#endif
	}
	// Adjust the tile indices so that tileIndex[i] is the start of tile i.
	tileIndex.insert(tileIndex.begin(), 0);

#ifndef ES_SOFT
	glBufferData(GL_ARRAY_BUFFER, sizeof(data.front()) * data.size(), data.data(), GL_STATIC_DRAW);

	if(OpenGL::HasVaoSupport())
		EnableAttribArrays();

	// unbind the VBO and VAO
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	if(OpenGL::HasVaoSupport())
		glBindVertexArray(0);
#endif
}
