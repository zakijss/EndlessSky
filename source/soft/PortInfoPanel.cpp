/* PortInfoPanel.cpp
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

#include "PortInfoPanel.h"

#include "../Color.h"
#include "../shader/FillShader.h"
#include "../GameData.h"
#include "../GameVersion.h"
#include "../Point.h"
#include "../Screen.h"
#include "../text/Font.h"
#include "../text/FontSet.h"
#include "../UI.h"
#include "Raster.h"

#include <algorithm>
#include <cmath>

using namespace std;

namespace {
	// The text box, in logical units around the screen centre.
	constexpr double BOX_WIDTH = 720.;
	constexpr double BOX_HEIGHT = 540.;
	constexpr double PAD = 20.;
	// Title area above the text, and the hint line below it.
	constexpr double HEADER = 56.;
	constexpr double FOOTER = 30.;

	double VisibleHeight()
	{
		return BOX_HEIGHT - HEADER - FOOTER;
	}
}



PortInfoPanel::PortInfoPanel()
{
	SetInterruptible(false);
	text.SetFont(FontSet::Get(14));
	text.SetWrapWidth(static_cast<int>(BOX_WIDTH - 2. * PAD));
	text.SetAlignment(Alignment::JUSTIFIED);
	text.Wrap(GameData::HelpMessage("miyoo port info"));
}



void PortInfoPanel::Draw()
{
	DrawBackdrop();
	const Color back(.09f, .09f, .11f, .97f), edge(.35f, .35f, .4f, 1.f);
	const Color &bright = *GameData::Colors().Get("bright");
	const Color &medium = *GameData::Colors().Get("medium");
	const double left = -BOX_WIDTH / 2., top = -BOX_HEIGHT / 2.;
	FillShader::Fill(Point(0., 0.), Point(BOX_WIDTH + 2., BOX_HEIGHT + 2.), edge);
	FillShader::Fill(Point(0., 0.), Point(BOX_WIDTH, BOX_HEIGHT), back);

	const Font &title = FontSet::Get(18);
	const Font &font = FontSet::Get(14);
	title.Draw("Endless Sky for Miyoo Mini Plus", Point(left + PAD, top + 12.), bright);
	string version = "Endless Sky " + GameVersion::Running().ToString();
#ifdef ES_PORT_VERSION
	// (The game's font has no middle dot.)
	version += "  -  Mini Plus port " ES_PORT_VERSION "  -  by ZJSS";
#endif
	font.Draw(version, Point(left + PAD, top + 36.), medium);
	FillShader::Fill(Point(0., top + HEADER - 4.), Point(BOX_WIDTH - 2. * PAD, 1.), edge);

	// The text, clipped to its area and scrolled.
	const double textTop = top + HEADER;
	const double scale = Raster::Scale();
	const int clipTop = lround((textTop - Screen::Top()) * scale);
	const int clipBottom = lround((textTop + VisibleHeight() - Screen::Top()) * scale);
	Raster::SetClip(0, clipTop, Raster::Width(), clipBottom - clipTop);
	text.Draw(Point(left + PAD, textTop + 6. - scroll), bright);
	Raster::ResetClip();

	const string hint = text.Height() > VisibleHeight() ? "Up/Down or L1/R1: scroll    B or Start: close" : "B or Start: close";
	font.Draw(hint, Point(-font.Width(hint) / 2., top + BOX_HEIGHT - FOOTER + 8.), medium);
}



bool PortInfoPanel::KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress)
{
	if(key == SDLK_ESCAPE || key == SDLK_RETURN || key == SDLK_KP_ENTER || key == 'd' || key == 'i')
		GetUI().Pop(this);
	else if(key == SDLK_UP || key == SDLK_DOWN)
		ScrollBy(key == SDLK_DOWN ? 40. : -40.);
	else if(key == SDLK_PAGEUP || key == SDLK_PAGEDOWN)
		ScrollBy((key == SDLK_PAGEDOWN ? 1. : -1.) * (VisibleHeight() - 40.));
	return true;
}



bool PortInfoPanel::Click(int x, int y, MouseButton button, int clicks)
{
	return true;
}



bool PortInfoPanel::Drag(double dx, double dy)
{
	ScrollBy(-dy);
	return true;
}



bool PortInfoPanel::Scroll(double dx, double dy)
{
	ScrollBy(-dy * 40.);
	return true;
}



void PortInfoPanel::ScrollBy(double dy)
{
	const double maxScroll = max(0., text.Height() - VisibleHeight() + 12.);
	scroll = clamp(scroll + dy, 0., maxScroll);
}
