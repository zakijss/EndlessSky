/* FillShader.cpp
Software version of FillShader for builds without a GPU.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../shader/FillShader.h"

#include "../Color.h"
#include "../Point.h"
#include "../Rectangle.h"
#include "Raster.h"



void FillShader::Init()
{
}



void FillShader::Fill(const Rectangle &area, const Color &color)
{
	Fill(area.Center(), area.Dimensions(), color);
}



void FillShader::Fill(const Point &center, const Point &size, const Color &color)
{
	Raster::Fill(center.X(), center.Y(), size.X(), size.Y(), color.Get());
}
