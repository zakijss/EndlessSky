/* gl_stub.h
Stand-in for the OpenGL headers in the software-rendered build.

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

#include "Raster.h"

#include <cstdint>

// Headers still name these types; nothing in the soft build calls real GL.
using GLint = int32_t;
using GLuint = uint32_t;
using GLenum = uint32_t;
using GLfloat = float;
using GLsizei = int32_t;

constexpr GLenum GL_COLOR_BUFFER_BIT = 0x4000;

// The panels clear the screen before drawing; that is the only GL call outside the renderer.
inline void glClear(GLenum)
{
	Raster::Clear();
}
