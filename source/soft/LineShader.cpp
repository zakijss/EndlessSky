/* LineShader.cpp
Software version of LineShader for builds without a GPU.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../shader/LineShader.h"

#include "../Color.h"
#include "../Point.h"
#include "Raster.h"

#include <algorithm>
#include <cmath>

using namespace std;



void LineShader::Init()
{
}



void LineShader::Draw(const Point &from, const Point &to, float width, const Color &color, bool roundCap)
{
	DrawGradient(from, to, width, color, color, roundCap);
}



void LineShader::DrawDashed(const Point &from, const Point &to, const Point &unit, float width,
	const Color &color, double dashLength, double spaceLength, bool roundCap)
{
	DrawGradientDashed(from, to, unit, width, color, color, dashLength, spaceLength, roundCap);
}



// Round caps: coverage = width - distance to the segment (width is the radius).
// Square caps: coverage = 1 - signed distance to a box of thickness width.
// Colour runs from fromColor to toColor along the quad the GL version draws.
void LineShader::DrawGradient(const Point &from, const Point &to, float width,
	const Color &fromColor, const Color &toColor, bool roundCap)
{
	Raster::ProfScope prof(Raster::PROF_LINE);
	const Point ab = to - from;
	const double length = ab.Length();
	if(length <= 0.)
		return;
	const Point unit = ab / length;
	const double capOffset = roundCap ? width + 1. : 1.;
	const double reach = max<double>(width + 1., capOffset);
	const Point quadStart = from - unit * capOffset;
	const double quadLength = length + 2. * capOffset;
	const float *c0 = fromColor.Get();
	const float *c1 = toColor.Get();

	Raster::Cover(min(from.X(), to.X()) - reach, min(from.Y(), to.Y()) - reach,
		max(from.X(), to.X()) + reach, max(from.Y(), to.Y()) + reach, c0,
		[&](double x, double y, float *rgba) -> float
		{
			const Point p(x, y);
			const Point ap = p - from;
			float coverage;
			if(roundCap)
			{
				double h = clamp(ap.Dot(ab) / ab.Dot(ab), 0., 1.);
				coverage = width - (ap - ab * h).Length();
			}
			else
			{
				// Box centred on the segment, axis along the line.
				const Point q = p - (from + to) * .5;
				double along = fabs(q.Dot(unit)) - length * .5;
				double across = fabs(q.Cross(unit)) - width * .5;
				double outside = Point(max(along, 0.), max(across, 0.)).Length();
				coverage = 1. - (outside + min(max(along, across), 0.));
			}
			if(coverage <= 0.f)
				return 0.f;
			float t = static_cast<float>(clamp((p - quadStart).Dot(unit) / quadLength, 0., 1.));
			for(int i = 0; i < 4; ++i)
				rgba[i] = c0[i] + (c1[i] - c0[i]) * t;
			return coverage;
		});
}



void LineShader::DrawGradientDashed(const Point &from, const Point &to, const Point &unit, float width,
	const Color &fromColor, const Color &toColor, double dashLength, double spaceLength, bool roundCap)
{
	const double length = (to - from).Length();
	const double patternLength = dashLength + spaceLength;
	int segments = length / patternLength;
	// Draw at least two dashes, shrinking the gaps to fit.
	if(segments < 2)
	{
		segments = 2;
		spaceLength *= length / (segments * patternLength);
	}
	spaceLength /= 2.;
	const double capOffset = roundCap ? width : 0.;
	for(int i = 0; i < segments; ++i)
	{
		float p0 = static_cast<float>(i) / segments;
		float p1 = static_cast<float>(i + 1) / segments;
		DrawGradient(from + unit * (i * length / segments + spaceLength + capOffset),
			from + unit * ((i + 1) * length / segments - spaceLength - capOffset), width,
			Color::Combine(1. - p0, fromColor, p0, toColor), Color::Combine(1. - p1, fromColor, p1, toColor), roundCap);
	}
}
