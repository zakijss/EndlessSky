/* SpriteShader.cpp
Software version of SpriteShader for builds without a GPU.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../shader/SpriteShader.h"

#include "../image/Sprite.h"
#include "../image/SpriteLoadManager.h"
#include "Raster.h"

using namespace std;



void SpriteShader::Init()
{
}



void SpriteShader::Draw(const Sprite *sprite, const Point &position, float zoom,
	const Swizzle *swizzle, float frame, const Point &unit)
{
	if(!sprite)
		return;

	Bind();
	Add(Prepare(sprite, position, zoom, swizzle, frame, unit));
	Unbind();
}



SpriteShader::Item SpriteShader::Prepare(const Sprite *sprite, const Point &position,
	float zoom, const Swizzle *swizzle, float frame, const Point &unit)
{
	if(!sprite)
		return {};
	SpriteLoadManager::UseSprite(sprite);

	Item item;
	item.texture = sprite->Texture();
	item.swizzleMask = sprite->SwizzleMask();
	item.frame = frame;
	item.frameCount = sprite->Frames();
	item.uniqueSwizzleMaskFrames = sprite->SwizzleMaskFrames() > 1;
	item.position[0] = static_cast<float>(position.X());
	item.position[1] = static_cast<float>(position.Y());

	// The columns of the transform are the sprite's width and height vectors.
	Point scaledUnit = unit * zoom;
	Point uw = scaledUnit * sprite->Width();
	Point uh = scaledUnit * sprite->Height();
	item.transform[0] = static_cast<float>(-uw.Y());
	item.transform[1] = static_cast<float>(uw.X());
	item.transform[2] = static_cast<float>(-uh.X());
	item.transform[3] = static_cast<float>(-uh.Y());

	item.swizzle = swizzle;
	return item;
}



void SpriteShader::Bind()
{
}



void SpriteShader::Add(const Item &item, bool withBlur)
{
	// shortcut: motion blur takes 3 samples (weights 1:2:1) where the GL shader takes 11.
	Raster::SpriteDraw draw;
	draw.texture = item.texture;
	uint32_t swizzleMask = item.swizzleMask;
	// Draw-list items: mark the sprite as used (loading it if needed) on this, the main,
	// thread, and take its current texture in case it was reloaded since the list was built.
	if(item.sprite)
	{
		SpriteLoadManager::UseSprite(item.sprite);
		draw.texture = item.sprite->Texture();
		swizzleMask = item.sprite->SwizzleMask();
	}
	if(item.swizzle && !item.swizzle->IsIdentity())
	{
		draw.swizzle = item.swizzle->MatrixPtr();
		// Full-colour swizzles recolour the whole sprite, so they ignore the mask.
		draw.swizzleMask = item.swizzle->OverrideMask() ? 0 : swizzleMask;
	}
	draw.frame = item.frame;
	draw.frameCount = static_cast<int>(item.frameCount);
	draw.uniqueSwizzleMaskFrames = item.uniqueSwizzleMaskFrames;
	draw.position[0] = item.position[0];
	draw.position[1] = item.position[1];
	for(int i = 0; i < 4; ++i)
		draw.transform[i] = item.transform[i];
	draw.clip = item.clip;
	draw.alpha = item.alpha;
	if(withBlur)
	{
		draw.blur[0] = item.blur[0];
		draw.blur[1] = item.blur[1];
	}
	Raster::DrawSprite(draw);
}



void SpriteShader::Unbind()
{
}
