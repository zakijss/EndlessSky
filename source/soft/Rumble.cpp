/* Rumble.cpp
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

#ifdef ES_MIYOO
#include "Rumble.h"

#include "../Logger.h"
#include "../Preferences.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>
#include <ctime>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

namespace {
	// Kept open: the value file of GPIO 48 ("0" runs the motor, "1" stops it).
	int valueFd = -1;
	bool running = false;
	double stopAt = 0.;
	// Never run longer than this in one go, whatever asks for it.
	constexpr double MAX_SECONDS = 3.;

	double Now()
	{
		timespec t;
		clock_gettime(CLOCK_MONOTONIC, &t);
		return t.tv_sec + t.tv_nsec * 1e-9;
	}

	bool WriteFile(const char *path, const char *text)
	{
		const int fd = open(path, O_WRONLY | O_CLOEXEC);
		if(fd < 0)
			return false;
		const bool ok = write(fd, text, strlen(text)) > 0;
		close(fd);
		return ok;
	}

	void Set(bool on)
	{
		if(valueFd < 0 || running == on)
			return;
		running = on;
		lseek(valueFd, 0, SEEK_SET);
		if(write(valueFd, on ? "0" : "1", 1) < 0)
			running = false;
	}
}



void Rumble::Init()
{
	if(access("/sys/class/gpio/gpio48/value", F_OK))
		WriteFile("/sys/class/gpio/export", "48");
	WriteFile("/sys/class/gpio/gpio48/direction", "out");
	valueFd = open("/sys/class/gpio/gpio48/value", O_WRONLY | O_CLOEXEC);
	running = true;
	Set(false);
}



void Rumble::Pulse(int milliseconds)
{
	// Off, light (as asked) or strong (twice as long).
	const int strength = Preferences::Index(Preferences::MIYOO_RUMBLE);
	if(valueFd < 0 || !strength || milliseconds <= 0)
		return;
	const double now = Now();
	stopAt = max(stopAt, now + min(MAX_SECONDS, milliseconds * strength / 1000.));
	Set(true);
	static const bool log = getenv("ES_PROFILE");
	if(log)
		Logger::Log("rumble " + to_string(milliseconds * strength) + " ms", Logger::Level::INFO);
}



void Rumble::Update()
{
	if(running && Now() >= stopAt)
		Set(false);
}



void Rumble::Off()
{
	if(valueFd >= 0 && write(valueFd, "1", 1) < 0)
		return;
	running = false;
}

#endif
