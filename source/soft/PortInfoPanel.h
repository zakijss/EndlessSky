/* PortInfoPanel.h
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

#include "../Panel.h"

#include "../text/WrappedText.h"



// About the Miyoo Mini Plus port: its version, the controls and tips, scrollable.
// The text is the help message "miyoo port info", so <command> names become buttons.
class PortInfoPanel : public Panel {
public:
	PortInfoPanel();

	void Draw() override;


protected:
	bool KeyDown(SDL_Keycode key, Uint16 mod, const Command &command, bool isNewPress) override;
	bool Click(int x, int y, MouseButton button, int clicks) override;
	bool Drag(double dx, double dy) override;
	bool Scroll(double dx, double dy) override;


private:
	void ScrollBy(double dy);


private:
	WrappedText text;
	double scroll = 0.;
};
