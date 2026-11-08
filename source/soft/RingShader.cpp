/* RingShader.cpp
Software version of RingShader for builds without a GPU.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../shader/RingShader.h"

#include "../Color.h"
#include "../Point.h"
#include "Raster.h"

#include <algorithm>
#include <cmath>
#include <numbers>

using namespace std;

namespace {
	constexpr double TWO_PI = 2. * numbers::pi;

	// GLSL mod(): the result has the sign of the divisor.
	double Mod(double a, double b)
	{
		return a - b * floor(a / b);
	}
}



void RingShader::Init()
{
}



void RingShader::Draw(const Point &pos, float out, float in, const Color &color)
{
	Add(pos, out, in, color);
}



void RingShader::Draw(const Point &pos, float radius, float width, float fraction,
	const Color &color, float dash, float startAngle)
{
	Add(pos, radius, width, fraction, color, dash, startAngle);
}



void RingShader::Bind()
{
}



void RingShader::Add(const Point &pos, float out, float in, const Color &color)
{
	float width = .5f * (1.f + out - in);
	Add(pos, out - width, width, 1.f, color);
}



// An annulus of the given radius and half-width, cut to an arc of "fraction" of a
// turn starting at startAngle degrees, optionally split into "dash" dashes.
void RingShader::Add(const Point &pos, float radius, float width, float fraction,
	const Color &color, float dash, float startAngle)
{
	Raster::ProfScope prof(Raster::PROF_RING);
	const double angle = fraction * TWO_PI;
	const double start = startAngle * numbers::pi / 180.;
	const double dashArc = dash ? TWO_PI / dash : 0.;
	const double reach = radius + width;
	// Only the band within width of the radius can be covered; a pixel of margin either side.
	const double margin = 1. / Raster::Scale();
	const double outer = reach + margin, inner = radius - width - margin;
	const bool fullCircle = fraction >= 1.f && !dash;
	Raster::CoverSpans(pos.X() - reach, pos.Y() - reach, pos.X() + reach, pos.Y() + reach, color.Get(),
		[&](double x, double y, float *) -> float
		{
			x -= pos.X();
			y -= pos.Y();
			double lenFalloff = width - fabs(sqrt(x * x + y * y) - radius);
			// A whole undashed ring has an arc falloff of at least 1, the most a pixel can take.
			if(fullCircle || lenFalloff <= 0.)
				return static_cast<float>(lenFalloff);
			double arc = Mod(atan2(x, y) + numbers::pi + start, TWO_PI);
			double arcFalloff = 1. - min(TWO_PI - arc, arc - angle) * radius;
			if(dashArc)
			{
				double d = Mod(arc, dashArc);
				arcFalloff = min(arcFalloff, min(d, dashArc - d) * radius);
			}
			return static_cast<float>(min(arcFalloff, lenFalloff));
		},
		[&](double y, double *range) -> int
		{
			const double dy = y - pos.Y();
			if(fabs(dy) >= outer)
				return 0;
			const double xo = sqrt(outer * outer - dy * dy);
			if(inner <= 0. || fabs(dy) >= inner)
			{
				range[0] = pos.X() - xo;
				range[1] = pos.X() + xo;
				return 1;
			}
			const double xi = sqrt(inner * inner - dy * dy);
			range[0] = pos.X() - xo;
			range[1] = pos.X() - xi;
			range[2] = pos.X() + xi;
			range[3] = pos.X() + xo;
			return 2;
		});
}



void RingShader::Unbind()
{
}
