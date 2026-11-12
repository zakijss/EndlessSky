/* Navigation.h
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

#pragma once

#include "../Point.h"
#include "../Rectangle.h"

#include <vector>



// D-pad navigation for handhelds: the clickable areas of the top panel, collected while it
// draws (buttons, zones, list rows), so the D-pad can jump the pointer from one to the next.
namespace Navigation {
	// Called before each panel of the UI stack draws: only the last (top) panel's areas count.
	void BeginPanel();
	// A clickable area, in screen coordinates (origin at the centre).
	void Add(const Rectangle &area);
	// This panel is a map: the pointer moves freely, nothing to jump between.
	void SetFree();

	bool IsFree();
	const std::vector<Rectangle> &Targets();
	// The area to jump to from the given point, in a direction (0 up, 1 down, 2 left, 3 right);
	// false if there is none that way.
	bool Next(const Point &from, int direction, Rectangle &result);
	// The area containing the point, if any (smallest first).
	bool At(const Point &point, Rectangle &result);
}
