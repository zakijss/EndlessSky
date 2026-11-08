/* Font.cpp
Copyright (c) 2014-2020 by Michael Zahniser

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "Font.h"

#include "Alignment.h"
#include "../Color.h"
#ifdef ES_SOFT
#include "../soft/Raster.h"
#endif
#include "DisplayText.h"
#include "../GameData.h"
#include "../image/ImageBuffer.h"
#include "../image/ImageFileData.h"
#include "../Point.h"
#include "../Preferences.h"
#include "../Logger.h"
#include "../Screen.h"
#include "Truncate.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <set>

using namespace std;

namespace {
#ifdef ES_SOFT
	// Thickening for small text (1 = none).
	constexpr double TEXT_GAMMA = .75;
#endif
	bool showUnderlines = false;
	const int KERN = 2;
	const float UNDERLINE_OFFSET = 2.f;

	/// Shared VAO and VBO quad (0,0) -> (1,1)
#ifndef ES_SOFT
	GLuint vao = 0;
	GLuint vbo = 0;

	GLint colorI = 0;
	GLint scaleI = 0;
	GLint glyphSizeI = 0;
	GLint glyphI = 0;
	GLint aspectI = 0;
	GLint positionI = 0;

	GLint vertI;
	GLint cornerI;

	void EnableAttribArrays()
	{
		// Connect the xy to the "vert" attribute of the vertex shader.
		constexpr auto stride = 4 * sizeof(GLfloat);
		glEnableVertexAttribArray(vertI);
		glVertexAttribPointer(vertI, 2, GL_FLOAT, GL_FALSE, stride, nullptr);

		glEnableVertexAttribArray(cornerI);
		glVertexAttribPointer(cornerI, 2, GL_FLOAT, GL_FALSE,
			stride, reinterpret_cast<const GLvoid *>(2 * sizeof(GLfloat)));
	}
#endif

}



Font::Font(const filesystem::path &imagePath)
{
	Load(imagePath);
}



void Font::Load(const filesystem::path &imagePath)
{
	// Load the texture.
	ImageBuffer image;
	if(!image.Read(ImageFileData(imagePath)))
		return;

	LoadTexture(image);
	CalculateAdvances(image);
	SetUpShader(image.Width() / GLYPHS, image.Height());
	widthEllipses = WidthRawString("...");
}



void Font::Draw(const DisplayText &text, const Point &point, const Color &color) const
{
	DrawAliased(text, round(point.X()), round(point.Y()), color);
}



void Font::DrawAliased(const DisplayText &text, double x, double y, const Color &color) const
{
	int width = -1;
	const string truncText = TruncateText(text, width);
	const auto &layout = text.GetLayout();
	if(width >= 0)
	{
		if(layout.align == Alignment::CENTER)
			x += (layout.width - width) / 2;
		else if(layout.align == Alignment::RIGHT)
			x += layout.width - width;
	}
	DrawAliased(truncText, x, y, color);
}



void Font::Draw(const string &str, const Point &point, const Color &color) const
{
	DrawAliased(str, round(point.X()), round(point.Y()), color);
}



void Font::DrawAliased(const string &str, double x, double y, const Color &color) const
{
#ifdef ES_SOFT
	Raster::ProfScope prof(Raster::PROF_TEXT);
#endif
#ifdef ES_SOFT
	// ES_TEXT_CHECK=1: log (once each) text drawn past the screen's edges, to find overflow.
	static const bool checkText = getenv("ES_TEXT_CHECK") != nullptr;
	if(checkText && !str.empty())
	{
		static set<string> reported;
		const double right = x + Width(str), bottom = y + Height();
		if((x < Screen::Left() - 1 || right > Screen::Right() + 1 || y < Screen::Top() - 1 || bottom > Screen::Bottom() + 1)
				&& reported.insert(str).second)
			Logger::Log("Text past the screen edge at (" + to_string(static_cast<int>(x)) + ", " + to_string(static_cast<int>(y))
				+ ") to (" + to_string(static_cast<int>(right)) + ", " + to_string(static_cast<int>(bottom)) + "): " + str.substr(0, 60),
				Logger::Level::WARNING);
	}
	// Same layout as the GL branch below; keep the two in step.
	const float *rgba = color.Get();
	auto drawGlyph = [&](int glyph, float aspect, float gx, float gy)
	{
		Raster::DrawGlyph(atlas.data(), atlasWidth, atlasHeight, GLYPHS, glyph,
			gx, gy, aspect * glyphWidth, glyphHeight, rgba);
	};
	float textPos[2] = {static_cast<float>(x - 1.), static_cast<float>(y)};
	int previous = 0;
	bool isAfterSpace = true;
	bool underlineChar = false;
	bool doubleUnderlineChar = false;
	const int underscoreGlyph = max(0, min(GLYPHS - 1, '_' - 32));
	for(char c : str)
	{
		if(c == '_')
		{
			if(underlineChar)
				doubleUnderlineChar = true;
			underlineChar = showUnderlines;
			continue;
		}
		int glyph = Glyph(c, isAfterSpace);
		if(c != '"' && c != '\'')
			isAfterSpace = !glyph;
		if(!glyph)
		{
			textPos[0] += space;
			continue;
		}
		textPos[0] += advance[previous * GLYPHS + glyph] + KERN;
		drawGlyph(glyph, 1.f, textPos[0], textPos[1]);
		if(underlineChar)
		{
			float aspect = static_cast<float>(advance[glyph * GLYPHS] + KERN)
				/ (advance[underscoreGlyph * GLYPHS] + KERN);
			drawGlyph(underscoreGlyph, aspect, textPos[0], textPos[1]);
			if(doubleUnderlineChar)
			{
				drawGlyph(underscoreGlyph, aspect, textPos[0], textPos[1] + UNDERLINE_OFFSET);
				doubleUnderlineChar = false;
			}
			underlineChar = false;
		}
		previous = glyph;
	}
#else
	glUseProgram(shader->Object());
	glBindTexture(GL_TEXTURE_2D, texture);
	if(OpenGL::HasVaoSupport())
		glBindVertexArray(vao);
	else
	{
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
		EnableAttribArrays();
	}

	glUniform4fv(colorI, 1, color.Get());

	// Update the scale, only if the screen size has changed.
	if(Screen::Width() != screenWidth || Screen::Height() != screenHeight)
	{
		screenWidth = Screen::Width();
		screenHeight = Screen::Height();
		scale[0] = 2.f / screenWidth;
		scale[1] = -2.f / screenHeight;
	}
	glUniform2fv(scaleI, 1, scale);
	glUniform2f(glyphSizeI, glyphWidth, glyphHeight);

	GLfloat textPos[2] = {
		static_cast<float>(x - 1.),
		static_cast<float>(y)};
	int previous = 0;
	bool isAfterSpace = true;
	bool underlineChar = false;
	bool doubleUnderlineChar = false;
	const int underscoreGlyph = max(0, min(GLYPHS - 1, '_' - 32));

	for(char c : str)
	{
		if(c == '_')
		{
			if(underlineChar)
				doubleUnderlineChar = true;
			underlineChar = showUnderlines;
			continue;
		}

		int glyph = Glyph(c, isAfterSpace);
		if(c != '"' && c != '\'')
			isAfterSpace = !glyph;
		if(!glyph)
		{
			textPos[0] += space;
			continue;
		}

		glUniform1i(glyphI, glyph);
		glUniform1f(aspectI, 1.f);

		textPos[0] += advance[previous * GLYPHS + glyph] + KERN;
		glUniform2fv(positionI, 1, textPos);

		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

		if(underlineChar)
		{
			glUniform1i(glyphI, underscoreGlyph);
			glUniform1f(aspectI, static_cast<float>(advance[glyph * GLYPHS] + KERN)
				/ (advance[underscoreGlyph * GLYPHS] + KERN));

			glUniform2fv(positionI, 1, textPos);
			glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

			if(doubleUnderlineChar)
			{
				GLfloat underlinePos[2] = {textPos[0], textPos[1] + UNDERLINE_OFFSET};
				glUniform2fv(positionI, 1, underlinePos);
				glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
				doubleUnderlineChar = false;
			}

			underlineChar = false;
		}

		previous = glyph;
	}

	if(OpenGL::HasVaoSupport())
		glBindVertexArray(0);
	else
	{
		glDisableVertexAttribArray(vertI);
		glDisableVertexAttribArray(cornerI);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);
	}
	glUseProgram(0);
#endif
}



int Font::Width(const string &str, char after) const
{
	return WidthRawString(str.c_str(), after);
}



int Font::FormattedWidth(const DisplayText &text, char after) const
{
	int width = -1;
	const string truncText = TruncateText(text, width);
	return width < 0 ? WidthRawString(truncText.c_str(), after) : width;
}



int Font::Height() const noexcept
{
	return height;
}



int Font::Space() const noexcept
{
	return space;
}



void Font::ShowUnderlines(bool show) noexcept
{
	showUnderlines = show || Preferences::Has(Preferences::UNDERLINE_SHORTCUTS);
}



int Font::Glyph(char c, bool isAfterSpace) noexcept
{
	// Curly quotes.
	if(c == '\'' && isAfterSpace)
		return 96;
	if(c == '"' && isAfterSpace)
		return 97;

	return max(0, min(GLYPHS - 3, c - 32));
}



void Font::LoadTexture(ImageBuffer &image)
{
#ifdef ES_SOFT
	// The atlas holds glyphs at the original's 1x size; the 800x600 screen draws them at 0.8,
	// so the full atlas sampled bilinearly stays sharp. A gamma below 1 keeps the strokes of
	// small text from thinning out when they fall between pixels.
	atlasWidth = image.Width();
	atlasHeight = image.Height();
	atlas.assign(static_cast<size_t>(atlasWidth) * atlasHeight, 0);
	const uint32_t *pixels = image.Pixels();
	for(size_t i = 0; i < atlas.size(); ++i)
		atlas[i] = static_cast<uint8_t>(255. * pow((pixels[i] >> 24) / 255., TEXT_GAMMA) + .5);
#else
	glGenTextures(1, &texture);
	glBindTexture(GL_TEXTURE_2D, texture);

	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, image.Width(), image.Height(), 0,
		GL_RGBA, GL_UNSIGNED_BYTE, image.Pixels());
#endif
}



void Font::CalculateAdvances(ImageBuffer &image)
{
	// Get the format and size of the surface.
	int width = image.Width() / GLYPHS;
	height = image.Height();
	unsigned mask = 0xFF000000;
	unsigned half = 0xC0000000;
	int pitch = image.Width();

	// advance[previous * GLYPHS + next] is the x advance for each glyph pair.
	// There is no advance if the previous value is 0, i.e. we are at the very
	// start of a string.
	memset(advance, 0, GLYPHS * sizeof(advance[0]));
	for(int previous = 1; previous < GLYPHS; ++previous)
		for(int next = 0; next < GLYPHS; ++next)
		{
			int maxD = 0;
			int glyphWidth = 0;
			uint32_t *begin = image.Pixels();
			for(int y = 0; y < height; ++y)
			{
				// Find the last non-empty pixel in the previous glyph.
				uint32_t *pend = begin + previous * width;
				uint32_t *pit = pend + width;
				while(pit != pend && (*--pit & mask) < half) {}
				int distance = (pit - pend) + 1;
				glyphWidth = max(distance, glyphWidth);

				// Special case: if "next" is zero (i.e. end of line of text),
				// calculate the full width of this character. Otherwise:
				if(next)
				{
					// Find the first non-empty pixel in this glyph.
					uint32_t *nit = begin + next * width;
					uint32_t *nend = nit + width;
					while(nit != nend && (*nit++ & mask) < half) {}

					// How far apart do you want these glyphs drawn? If drawn at
					// an advance of "width", there would be:
					// pend + width - pit   <- pixels after the previous glyph.
					// nit - (nend - width) <- pixels before the next glyph.
					// So for zero kerning distance, you would want:
					distance += 1 - (nit - (nend - width));
				}
				maxD = max(maxD, distance);

				// Update the pointer to point to the beginning of the next row.
				begin += pitch;
			}
			// This is a fudge factor to avoid over-kerning, especially for the
			// underscore and for glyph combinations like AV.
			advance[previous * GLYPHS + next] = max(maxD, glyphWidth - 4) / 2;
		}

	// Set the space size based on the character width.
	width /= 2;
	height /= 2;
	space = (width + 3) / 6 + 1;
}



void Font::SetUpShader(float glyphW, float glyphH)
{
	glyphWidth = glyphW * .5f;
	glyphHeight = glyphH * .5f;

#ifndef ES_SOFT
	shader = GameData::Shaders().Get("font");
	// Initialize the shared parameters only once
	if(!vbo)
	{
		vertI = shader->Attrib("vert");
		cornerI = shader->Attrib("corner");

		glUseProgram(shader->Object());
		glUniform1i(shader->Uniform("tex"), 0);
		glUseProgram(0);

		// Create the VAO and VBO.
		if(OpenGL::HasVaoSupport())
		{
			glGenVertexArrays(1, &vao);
			glBindVertexArray(vao);
		}

		glGenBuffers(1, &vbo);
		glBindBuffer(GL_ARRAY_BUFFER, vbo);

		GLfloat vertices[] = {
				0.f, 0.f, 0.f, 0.f,
				0.f, 1.f, 0.f, 1.f,
				1.f, 0.f, 1.f, 0.f,
				1.f, 1.f, 1.f, 1.f
		};
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

		if(OpenGL::HasVaoSupport())
			EnableAttribArrays();

		glBindBuffer(GL_ARRAY_BUFFER, 0);
		if(OpenGL::HasVaoSupport())
			glBindVertexArray(0);

		colorI = shader->Uniform("color");
		scaleI = shader->Uniform("scale");
		glyphSizeI = shader->Uniform("glyphSize");
		glyphI = shader->Uniform("glyph");
		aspectI = shader->Uniform("aspect");
		positionI = shader->Uniform("position");
	}
#endif

	// We must update the screen size next time we draw.
	screenWidth = 0;
	screenHeight = 0;
}



int Font::WidthRawString(const char *str, char after) const noexcept
{
	int width = 0;
	int previous = 0;
	bool isAfterSpace = true;

	for( ; *str; ++str)
	{
		if(*str == '_')
			continue;

		int glyph = Glyph(*str, isAfterSpace);
		if(*str != '"' && *str != '\'')
			isAfterSpace = !glyph;
		if(!glyph)
			width += space;
		else
		{
			width += advance[previous * GLYPHS + glyph] + KERN;
			previous = glyph;
		}
	}
	width += advance[previous * GLYPHS + max(0, min(GLYPHS - 1, after - 32))];

	return width;
}



// Param width will be set to the width of the return value, unless the layout width is negative.
string Font::TruncateText(const DisplayText &text, int &width) const
{
	width = -1;
	const auto &layout = text.GetLayout();
	const string &str = text.GetText();
	if(layout.width < 0 || (layout.align == Alignment::LEFT && layout.truncate == Truncate::NONE))
		return str;
	width = layout.width;
	switch(layout.truncate)
	{
		case Truncate::NONE:
			width = WidthRawString(str.c_str());
			return str;
		case Truncate::FRONT:
			return TruncateFront(str, width);
		case Truncate::MIDDLE:
			return TruncateMiddle(str, width);
		case Truncate::BACK:
		default:
			return TruncateBack(str, width);
	}
}



string Font::TruncateBack(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, int charCount)
		{
			return str.substr(0, charCount) + "...";
		});
}



string Font::TruncateFront(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, int charCount)
		{
			return "..." + str.substr(str.size() - charCount);
		});
}



string Font::TruncateMiddle(const string &str, int &width) const
{
	return TruncateEndsOrMiddle(str, width,
		[](const string &str, int charCount)
		{
			return str.substr(0, (charCount + 1) / 2) + "..." + str.substr(str.size() - charCount / 2);
		});
}



string Font::TruncateEndsOrMiddle(const string &str, int &width,
	function<string(const string &, int)> getResultString) const
{
	int firstWidth = WidthRawString(str.c_str());
	if(firstWidth <= width)
	{
		width = firstWidth;
		return str;
	}

	int workingChars = 0;
	int workingWidth = 0;

	int low = 0, high = str.size() - 1;
	while(low <= high)
	{
		// Think "how many chars to take from both ends, omitting in the middle".
		int nextChars = (low + high) / 2;
		int nextWidth = WidthRawString(getResultString(str, nextChars).c_str());
		if(nextWidth <= width)
		{
			if(nextChars > workingChars)
			{
				workingChars = nextChars;
				workingWidth = nextWidth;
			}
			low = nextChars + (nextChars == low);
		}
		else
			high = nextChars - 1;
	}
	width = workingWidth;
	return getResultString(str, workingChars);
}
