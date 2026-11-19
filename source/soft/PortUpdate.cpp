/* PortUpdate.cpp
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

#include "PortUpdate.h"

#include "../Files.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace std;

namespace {
	filesystem::path Dir()
	{
		return Files::Config() / "update";
	}

	int64_t FileSize(const filesystem::path &path)
	{
		struct stat st;
		return stat(path.c_str(), &st) == 0 ? st.st_size : 0;
	}

	string Megabytes(int64_t bytes)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.1f MB", bytes / 1048576.);
		return buf;
	}
}



PortUpdate::Status PortUpdate::Parse(const string &text)
{
	Status status;
	istringstream in(text);
	string line;
	while(getline(in, line))
	{
		const size_t eq = line.find('=');
		if(eq == string::npos)
			continue;
		const string key = line.substr(0, eq);
		const string value = line.substr(eq + 1);
		if(key == "state")
			status.state = value;
		else if(key == "latest")
			status.latest = value;
		else if(key == "error")
			status.error = value;
		else if(key == "size")
			status.size = strtoll(value.c_str(), nullptr, 10);
	}
	return status;
}



PortUpdate::Status PortUpdate::Read()
{
	ifstream in(Dir() / "status");
	if(!in)
		return Status();
	ostringstream text;
	text << in.rdbuf();
	Status status = Parse(text.str());
	if(status.state == "downloading")
		status.got = FileSize(Dir() / "update.part");
	return status;
}



void PortUpdate::Start(const char *what)
{
	const string script = (Files::Resources() / "update.sh").string();
	const string dir = Dir().string();
	// Twice forked, so the script is nobody's zombie and outlives the game if it has to.
	const pid_t child = fork();
	if(child == 0)
	{
		if(fork() == 0)
		{
			setsid();
			execl("/bin/sh", "sh", script.c_str(), what, dir.c_str(), static_cast<char *>(nullptr));
			_exit(127);
		}
		_exit(0);
	}
	if(child > 0)
		waitpid(child, nullptr, 0);
}



int64_t PortUpdate::SinceCheck()
{
	ifstream in(Dir() / "checked");
	int64_t last = 0;
	if(!(in >> last) || last <= 0)
		return INT64_MAX;
	return static_cast<int64_t>(time(nullptr)) - last;
}



string PortUpdate::Describe(const Status &status)
{
	if(status.state == "checking")
		return "Checking for an update...";
	if(status.state == "current")
		return "This is the latest version.";
	if(status.state == "available")
		return "Update " + status.latest + " available" + (status.size > 0 ? " (" + Megabytes(status.size) + ")." : ".");
	if(status.state == "downloading")
	{
		const int percent = status.size > 0 ? static_cast<int>(100 * status.got / status.size) : 0;
		return "Downloading update " + status.latest + ": " + to_string(percent) + "%";
	}
	if(status.state == "ready")
		return "Update " + status.latest + " is downloaded. It installs the next time you start the game.";
	if(status.state == "error")
		return status.error.empty() ? "The update check failed." : status.error;
	return "Not checked for updates yet.";
}
