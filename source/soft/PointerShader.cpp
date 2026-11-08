/* PointerShader.cpp
Software version of PointerShader for builds without a GPU.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../shader/PointerShader.h"

#include "../Color.h"
#include "../Point.h"
#include "Raster.h"

#include <algorithm>
#include <cmath>

using namespace std;



void PointerShader::Init()
{
}



void PointerShader::Draw(const Point &center, const Point &angle, float width, float height, float offset,
	const Color &color)
{
	Add(center, angle, width, height, offset, color);
}



void PointerShader::Bind()
{
}



// A tapered triangle pointing along "angle". The GL version maps the triangle
// (0,0) (0,1) (1,0) to the screen; here each pixel is mapped back to it.
void PointerShader::Add(const Point &center, const Point &angle, float width, float height, float offset,
	const Color &color)
{
	Raster::ProfScope prof(Raster::PROF_POINTER);
	const Point side(angle.Y(), -angle.X());
	const Point origin = center + angle * offset;
	const Point a = -angle * height + side * (width * .5);
	const Point b = -angle * height - side * (width * .5);
	const double det = a.Cross(b);
	if(fabs(det) < 1e-9)
		return;
	const Point corners[3] = {origin, origin + a, origin + b};
	double minX = corners[0].X(), maxX = minX, minY = corners[0].Y(), maxY = minY;
	for(const Point &c : corners)
	{
		minX = min(minX, c.X()); maxX = max(maxX, c.X());
		minY = min(minY, c.Y()); maxY = max(maxY, c.Y());
	}
	Raster::Cover(minX, minY, maxX, maxY, color.Get(),
		[&](double x, double y, float *) -> float
		{
			const Point d = Point(x, y) - origin;
			const double u = d.Cross(b) / det;
			const double v = a.Cross(d) / det;
			if(u < 0. || v < 0. || u + v > 1.)
				return 0.f;
			const double cx = u * width, cy = v * width;
			double h = (cx + cy) / width;
			double taper = h * h * h;
			taper *= taper * .5 * width;
			double alpha = clamp(.8 * min(cx, cy) - taper, 0., 1.);
			return static_cast<float>(alpha * clamp(1.8 * (1. - h), 0., 1.));
		});
}



void PointerShader::Unbind()
{
}
