/* FogShader.cpp
Software version of FogShader for builds without a GPU.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../shader/FogShader.h"

#include "../GameData.h"
#include "../PlayerInfo.h"
#include "../Point.h"
#include "../Screen.h"
#include "../System.h"
#include "Raster.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

using namespace std;

namespace {
	// Same grid as the GL version: 16-unit cells, chamfer distances 5 and 7,
	// padded far enough that fog reaches full strength off screen.
	const int GRID = 16;
	const int ORTH = 5;
	const int DIAG = 7;
	const int LIMIT = 255;
	const int PAD = LIMIT / ORTH;

	vector<uint8_t> fog;
	Point previousCenter;
	double previousZoom = 0.;
	double previousLeft = 0.;
	double previousTop = 0.;
	int previousColumns = 0;
	int previousRows = 0;
}



void FogShader::Init()
{
}



void FogShader::Redraw()
{
	previousZoom = 0.;
}



void FogShader::Draw(const Point &center, double zoom, const PlayerInfo &player)
{
	double left = Screen::Left() - GRID * PAD * zoom + fmod(center.X(), GRID) * zoom;
	double top = Screen::Top() - GRID * PAD * zoom + fmod(center.Y(), GRID) * zoom;
	int columns = ceil(Screen::Width() / (GRID * zoom)) + 1 + 2 * PAD;
	int rows = ceil(Screen::Height() / (GRID * zoom)) + 1 + 2 * PAD;
	columns = (columns + 3) & ~3;

	if(zoom != previousZoom || center.X() != previousCenter.X() || center.Y() != previousCenter.Y()
		|| left != previousLeft || top != previousTop || columns != previousColumns || rows != previousRows)
	{
		previousZoom = zoom;
		previousCenter = center;
		previousLeft = left;
		previousTop = top;
		previousColumns = columns;
		previousRows = rows;

		// Zero at every system the player can see, then two chamfer passes spread
		// the distance outward; the fog starts some way out and ramps up quickly.
		fog.assign(static_cast<size_t>(rows) * columns, LIMIT);
		for(const auto &it : GameData::Systems())
		{
			const System &system = it.second;
			if(!system.IsValid() || !player.CanView(system))
				continue;
			Point pos = zoom * (system.Position() + center);
			int x = round((pos.X() - left) / (GRID * zoom));
			int y = round((pos.Y() - top) / (GRID * zoom));
			if(x >= 0 && y >= 0 && x < columns && y < rows)
				fog[x + y * columns] = 0;
		}
		// Like the GL version, x + 1 and x - 1 at a row edge read the neighbouring row; both stay in the buffer.
		auto at = [&](int x, int y) -> int { return fog[x + y * columns]; };
		for(int y = 1; y < rows; ++y)
			for(int x = 1; x < columns; ++x)
				fog[x + y * columns] = min(at(x, y), min(ORTH + min(at(x - 1, y), at(x, y - 1)),
					DIAG + min(at(x - 1, y - 1), at(x + 1, y - 1))));
		for(int y = rows - 2; y >= 0; --y)
			for(int x = columns - 2; x >= 0; --x)
				fog[x + y * columns] = min(at(x, y), min(ORTH + min(at(x + 1, y), at(x, y + 1)),
					DIAG + min(at(x - 1, y + 1), at(x + 1, y + 1))));
		for(uint8_t &value : fog)
			value = max(0, min(LIMIT, (value - 60) * 4));
	}

	// The texture spans columns + 1 by rows + 1 cells, from half a cell before the first.
	Raster::Darken(left - .5 * GRID * zoom, top - .5 * GRID * zoom,
		GRID * zoom * (columns + 1.), GRID * zoom * (rows + 1.), fog.data(), columns, rows);
}
