/* Navigation.cpp
Copyright (c) 2026 by ZJSS

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Navigation.h"

#include "../Screen.h"

#include <cmath>

using namespace std;

namespace {
	vector<Rectangle> targets;
	bool isFree = false;
}



void Navigation::BeginPanel()
{
	targets.clear();
	isFree = false;
}



void Navigation::Add(const Rectangle &area)
{
	// Areas covering most of the screen ("click anywhere") and empty ones are not places to go.
	if(area.Width() < 2. || area.Height() < 2.)
		return;
	if(area.Width() * area.Height() > .4 * Screen::Width() * Screen::Height())
		return;
	for(const Rectangle &known : targets)
		if(known.Center().Distance(area.Center()) < 1. && fabs(known.Width() - area.Width()) < 1.)
			return;
	targets.push_back(area);
}



void Navigation::SetFree()
{
	isFree = true;
}



bool Navigation::IsFree()
{
	return isFree;
}



const vector<Rectangle> &Navigation::Targets()
{
	return targets;
}



bool Navigation::Next(const Point &from, int direction, Rectangle &result)
{
	static const Point DIRECTIONS[] = {Point(0., -1.), Point(0., 1.), Point(-1., 0.), Point(1., 0.)};
	const Point d = DIRECTIONS[direction];
	const bool vertical = direction < 2;
	double best = INFINITY;
	for(const Rectangle &area : targets)
	{
		if(area.Contains(from))
			continue;
		const Point offset = area.Center() - from;
		const double along = offset.Dot(d);
		// Nearest edge along the direction must be ahead of us.
		const double nearEdge = along - (vertical ? area.Height() : area.Width()) / 2.;
		if(along <= 2. || nearEdge < -2.)
			continue;
		// Sideways distance: none when the area spans our line of travel.
		const double sideCenter = vertical ? offset.X() : offset.Y();
		const double halfSide = (vertical ? area.Width() : area.Height()) / 2.;
		const double side = max(0., fabs(sideCenter) - halfSide);
		const double score = max(0., nearEdge) + 3. * side + .01 * fabs(sideCenter);
		if(score < best)
		{
			best = score;
			result = area;
		}
	}
	return best < INFINITY;
}



bool Navigation::At(const Point &point, Rectangle &result)
{
	double smallest = INFINITY;
	for(const Rectangle &area : targets)
		if(area.Contains(point) && area.Width() * area.Height() < smallest)
		{
			smallest = area.Width() * area.Height();
			result = area;
		}
	return smallest < INFINITY;
}
