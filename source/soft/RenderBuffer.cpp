/* RenderBuffer.cpp
Software version of RenderBuffer for builds without a GPU.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../RenderBuffer.h"

#include "Raster.h"

#include <cmath>

using namespace std;



RenderBuffer::RenderTargetGuard::~RenderTargetGuard()
{
	Deactivate();
}



void RenderBuffer::RenderTargetGuard::Deactivate()
{
	buffer.Deactivate();
	screenGuard.Deactivate();
}



RenderBuffer::RenderTargetGuard::RenderTargetGuard(RenderBuffer &b, int screenWidth, int screenHeight)
	: buffer(b), screenGuard(screenWidth, screenHeight)
{
}



RenderBuffer::RenderBuffer(const Point &dimensions)
	: size(dimensions)
{
	pixelWidth = max(1, static_cast<int>(lround(size.X() * Raster::Scale())));
	pixelHeight = max(1, static_cast<int>(lround(size.Y() * Raster::Scale())));
	pixels.assign(static_cast<size_t>(pixelWidth) * pixelHeight, 0);
}



RenderBuffer::~RenderBuffer()
{
	Deactivate();
}



void RenderBuffer::Init()
{
}



RenderBuffer::RenderTargetGuard RenderBuffer::SetTarget()
{
	Raster::PushTarget(pixels.data(), pixelWidth, pixelHeight);
	isTarget = true;
	Raster::Clear(0);
	return RenderTargetGuard(*this, size.X(), size.Y());
}



void RenderBuffer::Draw(const Point &position)
{
	Draw(position, size);
}



void RenderBuffer::Draw(const Point &position, const Point &clipsize, const Point &srcposition)
{
	const double dimensions[2] = {size.X(), size.Y()};
	const double at[2] = {position.X(), position.Y()};
	const double clip[2] = {clipsize.X(), clipsize.Y()};
	const double source[2] = {srcposition.X(), srcposition.Y()};
	Raster::Composite(pixels.data(), pixelWidth, pixelHeight, dimensions, at, clip, source, fadePadding);
}



double RenderBuffer::Top() const
{
	return -size.Y() / 2;
}



double RenderBuffer::Bottom() const
{
	return size.Y() / 2;
}



double RenderBuffer::Left() const
{
	return -size.X() / 2;
}



double RenderBuffer::Right() const
{
	return size.X() / 2;
}



const Point &RenderBuffer::Dimensions() const
{
	return size;
}



double RenderBuffer::Height() const
{
	return size.Y();
}



double RenderBuffer::Width() const
{
	return size.X();
}



// Stored by position, as the GL version does: top, bottom, then the two sides.
void RenderBuffer::SetFadePadding(float top, float bottom, float left, float right)
{
	fadePadding[0] = top;
	fadePadding[1] = bottom;
	fadePadding[2] = left;
	fadePadding[3] = right;
}



void RenderBuffer::Deactivate()
{
	if(!isTarget)
		return;
	Raster::PopTarget();
	isTarget = false;
}
