/* Rumble.h
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



// The Miyoo Mini's vibration motor (GPIO 48, active low, as Onion's rumble.h drives it).
namespace Rumble {
	void Init();
	// Run the motor for this long (milliseconds, scaled by the Rumble setting; 0 when it is off).
	void Pulse(int milliseconds);
	// Stop the motor once its pulse is over; call every frame.
	void Update();
	// Stop now (quit). Async-signal-safe, for the crash handler.
	void Off();
}
