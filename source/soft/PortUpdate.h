/* PortUpdate.h
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

#include <cstdint>
#include <string>



// Updates of the Miyoo port from its GitHub releases. The work is done by update.sh in the game
// folder (check, download, and apply, which the launcher runs before the game starts); this reads
// the status file it writes into the config folder and starts it in the background.
namespace PortUpdate {
	struct Status {
		// idle, checking, current, available, downloading, ready or error.
		std::string state = "idle";
		std::string latest;
		std::string error;
		int64_t size = 0;
		// Bytes downloaded so far, while downloading.
		int64_t got = 0;
	};

	// The status as update.sh last wrote it.
	Status Read();
	// Parse a status file's text (for the tests).
	Status Parse(const std::string &text);
	// Start "check" or "download" in the background; it returns at once.
	void Start(const char *what);
	// Seconds since the last finished check, or a large number if there was none.
	int64_t SinceCheck();
	// One line for the screen, such as "Update 1.4 available (6.8 MB)".
	std::string Describe(const Status &status);
}
