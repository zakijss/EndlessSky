// Self-check for source/soft/Navigation (D-pad jumps between buttons):
// c++ -std=c++20 -I source miyoo/tests/navigation_test.cpp source/soft/Navigation.cpp
//   source/Rectangle.cpp source/Point.cpp source/Screen.cpp -o /tmp/nt && /tmp/nt
#include "soft/Navigation.h"
#include "CustomEvents.h"
#include "Screen.h"

#include <cassert>
#include <cstdio>

// Screen::SetRaw announces a resize; nothing listens here.
void CustomEvents::SendResize() {}

namespace {
	Rectangle Box(double x, double y, double w = 100., double h = 30.)
	{
		return Rectangle(Point(x, y), Point(w, h));
	}

	Point Jump(const Point &from, int direction)
	{
		Rectangle result;
		const bool found = Navigation::Next(from, direction, result);
		assert(found);
		(void)found;
		return result.Center();
	}

	bool None(const Point &from, int direction)
	{
		Rectangle result;
		return !Navigation::Next(from, direction, result);
	}
}

int main()
{
	Screen::SetRaw(800, 600, true);

	// A column of three buttons and one off to the right.
	Navigation::BeginPanel();
	Navigation::Add(Box(0., -100.));
	Navigation::Add(Box(0., 0.));
	Navigation::Add(Box(0., 100.));
	Navigation::Add(Box(250., 20.));
	assert(Navigation::Targets().size() == 4);

	// Up and down stay in the column, nearest first.
	assert(Jump(Point(0., 0.), 1) == Point(0., 100.));
	assert(Jump(Point(0., 0.), 0) == Point(0., -100.));
	assert(None(Point(0., 100.), 1));
	// Right reaches the button beside, left from there comes back to the nearest row.
	assert(Jump(Point(0., 0.), 3) == Point(250., 20.));
	assert(Jump(Point(250., 20.), 2) == Point(0., 0.));
	assert(None(Point(0., 0.), 2));

	// A row beside the line of travel loses to one straight ahead, even if a little closer.
	Navigation::BeginPanel();
	Navigation::Add(Box(0., 60., 40., 20.));
	Navigation::Add(Box(200., 40., 40., 20.));
	assert(Jump(Point(0., 0.), 1) == Point(0., 60.));

	// Duplicates, empty areas and areas covering most of the screen are not added.
	Navigation::BeginPanel();
	Navigation::Add(Box(0., 0.));
	Navigation::Add(Box(0., 0.));
	Navigation::Add(Box(10., 10., 0., 0.));
	Navigation::Add(Box(0., 0., 800., 600.));
	assert(Navigation::Targets().size() == 1);

	// At(): the smallest area under the point.
	Navigation::Add(Box(0., 0., 20., 10.));
	Rectangle area;
	const bool under = Navigation::At(Point(0., 0.), area);
	assert(under && area.Width() == 20.);
	assert(!Navigation::At(Point(300., 300.), area));

	// A map: free pointer.
	Navigation::SetFree();
	assert(Navigation::IsFree());
	Navigation::BeginPanel();
	assert(!Navigation::IsFree() && Navigation::Targets().empty());

	std::puts("navigation ok");
	return 0;
}
