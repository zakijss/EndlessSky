/* BatchShader.cpp
Software version of BatchShader for builds without a GPU.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "../shader/BatchShader.h"

#include "../image/Sprite.h"
#include "../image/SpriteLoadManager.h"
#include "Raster.h"

using namespace std;

namespace {
	// BatchDrawList pushes six vertices of (x, y, s, t, frame, alpha) per sprite:
	// top-left twice, top-right, bottom-left, bottom-right twice.
	constexpr size_t FLOATS_PER_VERTEX = 6;
	constexpr size_t FLOATS_PER_SPRITE = 6 * FLOATS_PER_VERTEX;
}



void BatchShader::Init()
{
}



void BatchShader::Bind()
{
}



void BatchShader::Add(const Sprite *sprite, const vector<float> &data)
{
	if(!sprite)
		return;
	SpriteLoadManager::UseSprite(sprite);
	for(size_t i = 0; i + FLOATS_PER_SPRITE <= data.size(); i += FLOATS_PER_SPRITE)
	{
		const float *topLeft = &data[i + FLOATS_PER_VERTEX];
		const float *topRight = topLeft + FLOATS_PER_VERTEX;
		const float *bottomLeft = topRight + FLOATS_PER_VERTEX;
		const double corner[2] = {topLeft[0], topLeft[1]};
		const double width[2] = {topRight[0] - topLeft[0], topRight[1] - topLeft[1]};
		const double height[2] = {bottomLeft[0] - topLeft[0], bottomLeft[1] - topLeft[1]};
		const float clip = topLeft[3] - bottomLeft[3];
		Raster::DrawBatchQuad(sprite->Texture(), topLeft[4], sprite->Frames(), corner, width, height,
			clip, topLeft[5]);
	}
}



void BatchShader::Unbind()
{
}
