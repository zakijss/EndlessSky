/* OutlineShader.cpp
Software version of OutlineShader for builds without a GPU.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../shader/OutlineShader.h"

#include "../Color.h"
#include "../image/Sprite.h"
#include "../image/SpriteLoadManager.h"
#include "../Point.h"
#include "Raster.h"



void OutlineShader::Init()
{
}



void OutlineShader::Draw(const Sprite *sprite, const Point &pos, const Point &size,
	const Color &color, const Point &unit, float frame)
{
	if(!sprite)
		return;
	SpriteLoadManager::UseSprite(sprite);
	// Sample step: half a logical unit of the drawn size, in texture coordinates.
	const float off[2] = {static_cast<float>(.5 / size.X()), static_cast<float>(.5 / size.Y())};
	const Point uw = unit * size.X();
	const Point uh = unit * size.Y();
	const float transform[4] = {
		static_cast<float>(-uw.Y()), static_cast<float>(uw.X()),
		static_cast<float>(-uh.X()), static_cast<float>(-uh.Y())};
	const float position[2] = {static_cast<float>(pos.X()), static_cast<float>(pos.Y())};
	Raster::DrawOutline(sprite->Texture(), frame, sprite->Frames(), position, transform, off, color.Get());
}
