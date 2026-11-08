/* GameWindowMiyoo.cpp
GameWindow for the Miyoo Mini Plus: the framebuffer and buttons, no GPU.

The game sees a 1024x768 screen at 100% zoom; the rasterizer draws it at
640x480 (scale 0.625). Each frame goes into the hidden page of the double-
buffered framebuffer, rotated 180 degrees because the panel is mounted upside
down, then the pages are flipped. Buttons come from /dev/input/event0 and are
fed through SDL's own input functions, so SDL's keyboard and mouse state (which
the flight controls poll) stays correct.

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

#include "../Color.h"
#include "../GameData.h"
#include "../Point.h"
#include "../shader/FillShader.h"
#include "../text/Font.h"
#include "../text/FontSet.h"
#include "../Preferences.h"
#include "../GameWindow.h"

#include "../image/SpriteLoadManager.h"
#include "../Logger.h"
#include "../Screen.h"
#include "Raster.h"

#include <SDL2/SDL.h>

#include <algorithm>
#include <csignal>
#include <ucontext.h>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <cstdio>
#include <cstdlib>
#include <utility>
#include <map>
#include <string>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

using namespace std;

// SDL's internal input entry points (SDL 2.30, linked statically): the same calls
// SDL's own drivers make, so keyboard and mouse state are updated with the events.
// SDL_MouseID is internal too (SDL_mouse_c.h).
typedef Uint32 SDL_MouseID;
extern "C" {
	int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);
	int SDL_SendMouseMotion(SDL_Window *window, SDL_MouseID mouseID, int relative, int x, int y);
	int SDL_SendMouseButton(SDL_Window *window, SDL_MouseID mouseID, Uint8 state, Uint8 button);
	void SDL_SetKeyboardFocus(SDL_Window *window);
	void SDL_SetMouseFocus(SDL_Window *window);
	int SDL_SendKeyboardText(const char *text);
	int SDL_SendMouseWheel(SDL_Window *window, SDL_MouseID mouseID, float x, float y, SDL_MouseWheelDirection direction);
}

namespace {
	// The screen the game lays itself out for: 800x600 draws everything 0.8 the size of
	// the desktop's 1024x768 minimum instead of 0.625, with layouts from miyoo interfaces.txt.
	// ES_LOGICAL=<width>x<height> overrides it.
	int LogicalSize(bool height)
	{
		static const auto size = []
		{
			int w = 800, h = 600;
			if(const char *env = getenv("ES_LOGICAL"))
				sscanf(env, "%dx%d", &w, &h);
			return std::make_pair(std::max(w, 320), std::max(h, 240));
		}();
		return height ? size.second : size.first;
	}
	const int LOGICAL_WIDTH = LogicalSize(false);
	const int LOGICAL_HEIGHT = LogicalSize(true);
	constexpr int PIXEL_WIDTH = 640;
	constexpr int PIXEL_HEIGHT = 480;

	// Button codes from /dev/input/event0 (gpio_keys), checked on the device.
	enum Button {
		UP = 103, DOWN = 108, LEFT = 105, RIGHT = 106,
		A = 57, B = 29, X = 42, Y = 56,
		L1 = 18, L2 = 15, R1 = 20, R2 = 14,
		SELECT = 97, START = 28, MENU = 1
	};

	// Flight controls. Select and Start also work as shift keys: held, they give buttons a
	// second and a third command; tapped alone, Select switches between flight controls and
	// the pointer and Start opens the main menu. Menu is left to Onion (its game switcher).
	// Each entry sends one of the game's default keys (keys.txt); the labels name the buttons
	// wherever the game names keys.
	enum Layer { PLAIN, SELECT_HELD, START_HELD };
	struct Binding {
		int button;
		Layer layer;
		SDL_Scancode key;
		const char *label;
	};
	const Binding BINDINGS[] = {
		{UP, PLAIN, SDL_SCANCODE_UP, "Up"},
		{DOWN, PLAIN, SDL_SCANCODE_DOWN, "Down"},
		{LEFT, PLAIN, SDL_SCANCODE_LEFT, "Left"},
		{RIGHT, PLAIN, SDL_SCANCODE_RIGHT, "Right"},
		{A, PLAIN, SDL_SCANCODE_TAB, "A"},                       // fire primary weapon
		{B, PLAIN, SDL_SCANCODE_Q, "B"},                         // fire secondary weapon
		{X, PLAIN, SDL_SCANCODE_A, "X"},                         // afterburner
		{Y, PLAIN, SDL_SCANCODE_R, "Y"},                         // nearest hostile ship
		{L1, PLAIN, SDL_SCANCODE_L, "L1"},                       // land
		{R1, PLAIN, SDL_SCANCODE_J, "R1"},                       // hyperspace jump
		{L2, PLAIN, SDL_SCANCODE_M, "L2"},                       // star map
		{R2, PLAIN, SDL_SCANCODE_B, "R2"},                       // board
		{START, PLAIN, SDL_SCANCODE_ESCAPE, "Start"},            // main menu (on release)
		{UP, SELECT_HELD, SDL_SCANCODE_F, "Select+Up"},          // fleet: fight my target
		{DOWN, SELECT_HELD, SDL_SCANCODE_G, "Select+Down"},      // fleet: gather around me
		{LEFT, SELECT_HELD, SDL_SCANCODE_H, "Select+Left"},      // fleet: hold position
		{RIGHT, SELECT_HELD, SDL_SCANCODE_Y, "Select+Right"},    // fleet: toggle hold fire
		{A, SELECT_HELD, SDL_SCANCODE_T, "Select+A"},            // talk to (hail) the target
		{B, SELECT_HELD, SDL_SCANCODE_W, "Select+B"},            // select secondary weapon
		{X, SELECT_HELD, SDL_SCANCODE_D, "Select+X"},            // deploy / recall fighters
		{Y, SELECT_HELD, SDL_SCANCODE_N, "Select+Y"},            // next ship
		{L1, SELECT_HELD, SDL_SCANCODE_S, "Select+L1"},          // scan
		{R1, SELECT_HELD, SDL_SCANCODE_E, "Select+R1"},          // auto steer
		{L2, SELECT_HELD, SDL_SCANCODE_SLASH, "Select+L2"},      // message log
		{R2, SELECT_HELD, SDL_SCANCODE_V, "Select+R2"},          // nearest asteroid
		{START, SELECT_HELD, SDL_SCANCODE_CAPSLOCK, "Select+Start"}, // fast-forward
		{Y, START_HELD, SDL_SCANCODE_I, "Start+Y"},              // player info
		{A, START_HELD, SDL_SCANCODE_C, "Start+A"},              // cloak
		{B, START_HELD, SDL_SCANCODE_Z, "Start+B"},              // fleet: harvest flotsam
		{X, START_HELD, SDL_SCANCODE_U, "Start+X"},              // fleet: ammo usage
		{L1, START_HELD, SDL_SCANCODE_P, "Start+L1"},            // pause
		{R1, START_HELD, SDL_SCANCODE_F1, "Start+R1"},           // help
		// Pointer mode (see CursorButton): named so menus and dialogs show buttons too.
		{START, PLAIN, SDL_SCANCODE_RETURN, "Start"},
		{L2, PLAIN, SDL_SCANCODE_PAGEUP, "L2"},
		{R2, PLAIN, SDL_SCANCODE_PAGEDOWN, "R2"},
	};

	SDL_Scancode FlightKey(int button, Layer layer)
	{
		for(const Binding &binding : BINDINGS)
			if(binding.button == button && binding.layer == layer)
				return binding.key;
		return SDL_SCANCODE_UNKNOWN;
	}

	SDL_Window *window = nullptr;
	int fbFd = -1;
	uint8_t *fbMap = nullptr;
	size_t fbSize = 0;
	fb_var_screeninfo fbVar = {};
	int fbStride = 0;
	int page = 0;
	int inputFd = -1;

	// The game starts in the main menu, so the pointer is on until the main loop says we fly.
	bool cursorMode = true;
	bool inFlightNow = false;
	// Frame skipping in flight: seconds the drawn frames are behind game time, and
	// how many coming steps to run undrawn.
	double drawDebt = 0.;
	double lastDrawn = 0.;
	int skipPending = 0;
	int skippedSteps = 0;
	constexpr double STEP_SECONDS = 1. / 60.;
	// Off flight, an unchanged screen is shown again instead of redrawn: frames since the
	// last button event, whether the last two drawn frames matched, and reuses in a row.
	int inputQuiet = 0;
	bool sameAsLast = false;
	uint32_t lastHash = 0;
	int reusedRun = 0;
	bool reusing = false;
	constexpr int QUIET_FRAMES = 10;
	// Redraw at least this often, to catch changes that come without input.
	constexpr int MAX_REUSE = 7;

	uint32_t FrameHash(const uint32_t *pixels, int count)
	{
		uint32_t hash = 2166136261u;
		for(int i = 0; i < count; ++i)
			hash = (hash ^ pixels[i]) * 16777619u;
		return hash;
	}
	// Up to 3 undrawn steps per drawn frame keeps game time real down to 15 fps (hyperspace).
	constexpr int MAX_SKIP = 3;
	bool leftDown = false;
	bool rightDown = false;
	double cursorX = LOGICAL_WIDTH / 2.;
	double cursorY = LOGICAL_HEIGHT / 2.;
	double cursorHeldTime = 0.;
	bool dpad[4] = {}; // up, down, left, right
	// Keys currently held down through SDL, so a mode switch can release them.
	bool keyDown[SDL_NUM_SCANCODES] = {};
	// Select and Start: held now, and whether another button was pressed while held (then
	// their own tap action is skipped).
	bool selectHeld = false, selectUsed = false, startHeld = false, startUsed = false;
	// The key each held button sent, so its release lifts the same key whatever changed since.
	map<int, SDL_Scancode> pressedKey;

	// On-screen keyboard, shown while the game wants text (an edit field, the pilot name, a
	// dialog's input) or while opened with Y in pointer mode.
	const char *const KEY_ROWS[] = {"1234567890", "qwertyuiop", "asdfghjkl'", "zxcvbnm,.-"};
	constexpr int KEY_COLUMNS = 10, KEY_ROW_COUNT = 5;
	// The bottom row: Shift, Space, Del, Done, as column ranges.
	const int SPECIAL_START[] = {0, 2, 6, 8, 10};
	bool keyboardVisible = false, keyboardOpened = false, keyboardDismissed = false, shiftOn = false;
	int keyRow = 1, keyColumn = 0;
	int frameNumber = 0, textWantedFrame = -10;

	void SendKey(SDL_Scancode key, bool down)
	{
		if(key == SDL_SCANCODE_UNKNOWN || keyDown[key] == down)
			return;
		keyDown[key] = down;
		SDL_SendKeyboardKey(down ? SDL_PRESSED : SDL_RELEASED, key);
	}

	void SendMouse(Uint8 button, bool &held, bool down)
	{
		if(held == down)
			return;
		held = down;
		SDL_SendMouseButton(window, 0, down ? SDL_PRESSED : SDL_RELEASED, button);
	}

	void ReleaseAll()
	{
		for(int key = 0; key < SDL_NUM_SCANCODES; ++key)
			SendKey(static_cast<SDL_Scancode>(key), false);
		SendMouse(SDL_BUTTON_LEFT, leftDown, false);
		SendMouse(SDL_BUTTON_RIGHT, rightDown, false);
		fill(begin(dpad), end(dpad), false);
		pressedKey.clear();
	}

	void Tap(SDL_Scancode key)
	{
		SendKey(key, true);
		SendKey(key, false);
	}

	void Press(int button, SDL_Scancode key)
	{
		pressedKey[button] = key;
		SendKey(key, true);
	}

	void Lift(int button)
	{
		auto it = pressedKey.find(button);
		if(it == pressedKey.end())
			return;
		SendKey(it->second, false);
		pressedKey.erase(it);
	}

	SDL_Scancode CharacterKey(char c)
	{
		if(c >= 'a' && c <= 'z')
			return static_cast<SDL_Scancode>(SDL_SCANCODE_A + (c - 'a'));
		if(c >= '1' && c <= '9')
			return static_cast<SDL_Scancode>(SDL_SCANCODE_1 + (c - '1'));
		switch(c)
		{
			case '0': return SDL_SCANCODE_0;
			case ' ': return SDL_SCANCODE_SPACE;
			case '\'': return SDL_SCANCODE_APOSTROPHE;
			case ',': return SDL_SCANCODE_COMMA;
			case '.': return SDL_SCANCODE_PERIOD;
			default: return SDL_SCANCODE_MINUS;
		}
	}

	// Type one character the way a keyboard does: the key (some panels read keys, with Shift
	// for capitals) and, while an edit field takes text, the text event.
	void TypeCharacter(char c)
	{
		const bool upper = shiftOn && c >= 'a' && c <= 'z';
		if(upper)
			SendKey(SDL_SCANCODE_LSHIFT, true);
		Tap(CharacterKey(c));
		if(upper)
			SendKey(SDL_SCANCODE_LSHIFT, false);
		if(SDL_IsTextInputActive())
		{
			const char text[2] = {upper ? static_cast<char>(c - 'a' + 'A') : c, 0};
			SDL_SendKeyboardText(text);
		}
		shiftOn = false;
	}

	void KeyboardButton(int code)
	{
		switch(code)
		{
			case UP: keyRow = (keyRow + KEY_ROW_COUNT - 1) % KEY_ROW_COUNT; break;
			case DOWN: keyRow = (keyRow + 1) % KEY_ROW_COUNT; break;
			case LEFT: keyColumn = (keyColumn + KEY_COLUMNS - 1) % KEY_COLUMNS; break;
			case RIGHT: keyColumn = (keyColumn + 1) % KEY_COLUMNS; break;
			case B: Tap(SDL_SCANCODE_BACKSPACE); break;
			case X: shiftOn = !shiftOn; break;
			case L1: Tap(SDL_SCANCODE_LEFT); break;
			case R1: Tap(SDL_SCANCODE_RIGHT); break;
			// Scroll what is behind the keyboard (the conversation text).
			case L2: SDL_SendMouseWheel(window, 0, 0.f, 1.f, SDL_MOUSEWHEEL_NORMAL); break;
			case R2: SDL_SendMouseWheel(window, 0, 0.f, -1.f, SDL_MOUSEWHEEL_NORMAL); break;
			case Y:
				keyboardOpened = false;
				keyboardDismissed = true;
				break;
			case A:
				if(keyRow < KEY_ROW_COUNT - 1)
					TypeCharacter(KEY_ROWS[keyRow][keyColumn]);
				else if(keyColumn < SPECIAL_START[1])
					shiftOn = !shiftOn;
				else if(keyColumn < SPECIAL_START[2])
					TypeCharacter(' ');
				else if(keyColumn < SPECIAL_START[3])
					Tap(SDL_SCANCODE_BACKSPACE);
				else
					Tap(SDL_SCANCODE_RETURN);
				break;
			default:
				break;
		}
	}

	// Pointer mode: the D-pad moves the pointer (with Select held it sends arrow keys), A and X
	// click, B goes back, L1 and R1 scroll, L2 and R2 page, Y opens the keyboard. (Start, which
	// confirms, is handled with the shift buttons.)
	void CursorButton(int code, bool down)
	{
		const int direction = code == UP ? 0 : code == DOWN ? 1 : code == LEFT ? 2 : code == RIGHT ? 3 : -1;
		if(direction >= 0)
		{
			if(down && selectHeld)
			{
				static const SDL_Scancode ARROWS[] = {SDL_SCANCODE_UP, SDL_SCANCODE_DOWN, SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
				Press(code, ARROWS[direction]);
			}
			else if(down)
				dpad[direction] = true;
			else
			{
				dpad[direction] = false;
				Lift(code);
			}
			return;
		}
		switch(code)
		{
			case A: SendMouse(SDL_BUTTON_LEFT, leftDown, down); break;
			case X: SendMouse(SDL_BUTTON_RIGHT, rightDown, down); break;
			// B goes back like Escape; it never sends the flight keys (B's "q" is Quit on the main menu).
			case B: SendKey(SDL_SCANCODE_ESCAPE, down); break;
			case L2: SendKey(SDL_SCANCODE_PAGEUP, down); break;
			case R2: SendKey(SDL_SCANCODE_PAGEDOWN, down); break;
			case L1:
			case R1:
				if(down)
					SDL_SendMouseWheel(window, 0, 0.f, code == L1 ? 1.f : -1.f, SDL_MOUSEWHEEL_NORMAL);
				break;
			case Y:
				if(down)
				{
					keyboardOpened = true;
					keyboardDismissed = false;
				}
				break;
			default:
				break;
		}
	}

	// A release undoes whatever the press did, in any mode: the mode or the keyboard may have
	// changed since (a click that opened a text dialog), and nothing may stay held.
	void ReleaseButton(int code)
	{
		Lift(code);
		switch(code)
		{
			case UP: dpad[0] = false; break;
			case DOWN: dpad[1] = false; break;
			case LEFT: dpad[2] = false; break;
			case RIGHT: dpad[3] = false; break;
			case A: SendMouse(SDL_BUTTON_LEFT, leftDown, false); break;
			case X: SendMouse(SDL_BUTTON_RIGHT, rightDown, false); break;
			case B: SendKey(SDL_SCANCODE_ESCAPE, false); break;
			case L2: SendKey(SDL_SCANCODE_PAGEUP, false); break;
			case R2: SendKey(SDL_SCANCODE_PAGEDOWN, false); break;
			default: break;
		}
	}

	void HandleButton(int code, bool down)
	{
		// Menu belongs to Onion: its key monitor opens the game switcher.
		if(code == MENU)
			return;
		// In pointer mode, Select held works as Shift and Start held as Control, for clicks
		// that need them (5 or 20 at once, adding ships to a selection, map routes).
		const bool modifiersAsKeys = cursorMode && !keyboardVisible;
		if(code == SELECT || code == START)
		{
			bool &held = code == SELECT ? selectHeld : startHeld;
			bool &used = code == SELECT ? selectUsed : startUsed;
			const bool otherHeld = code == SELECT ? startHeld : selectHeld;
			held = down;
			SendKey(code == SELECT ? SDL_SCANCODE_LSHIFT : SDL_SCANCODE_LCTRL, down && modifiersAsKeys);
			if(down)
			{
				used = false;
				// Select and Start together, in either order: fast-forward.
				if(otherHeld)
				{
					selectUsed = startUsed = true;
					Tap(FlightKey(START, SELECT_HELD));
				}
			}
			else if(!used)
			{
				if(code == SELECT)
				{
					ReleaseAll();
					cursorMode = !cursorMode;
				}
				// Start alone: confirm in menus and on the keyboard, the main menu in flight.
				else
					Tap(cursorMode || keyboardVisible ? SDL_SCANCODE_RETURN : SDL_SCANCODE_ESCAPE);
			}
			return;
		}
		if(!down)
		{
			ReleaseButton(code);
			return;
		}
		if(selectHeld)
			selectUsed = true;
		if(startHeld)
			startUsed = true;
		if(keyboardVisible)
		{
			KeyboardButton(code);
			return;
		}
		if(cursorMode)
		{
			// Start held: help, player info and pause work anywhere.
			if(startHeld && (code == R1 || code == Y || code == L1))
				Tap(FlightKey(code, START_HELD));
			else
				CursorButton(code, down);
			return;
		}
		Press(code, FlightKey(code, selectHeld ? SELECT_HELD : startHeld ? START_HELD : PLAIN));
	}

	void ReadButtons()
	{
		if(inputFd < 0)
			return;
		input_event e;
		while(read(inputFd, &e, sizeof(e)) == sizeof(e))
			if(e.type == EV_KEY && e.value != 2) // ignore autorepeat
			{
				static const bool logButtons = getenv("ES_PROFILE") != nullptr;
				if(logButtons)
					Logger::Log("button " + to_string(e.code) + (e.value ? " down" : " up") + " at "
						+ to_string(e.time.tv_sec % 100) + "." + to_string(e.time.tv_usec / 1000)
						+ ", frame " + to_string(frameNumber), Logger::Level::INFO);
				HandleButton(e.code, e.value == 1);
				inputQuiet = 0;
			}
	}

	void MoveCursor()
	{
		timespec now;
		clock_gettime(CLOCK_MONOTONIC, &now);
		const double seconds = now.tv_sec + now.tv_nsec * 1e-9;
		// Time since the last step, so the pointer moves the same at 20 or 40 frames a second.
		static double lastMove = seconds;
		const double elapsed = clamp(seconds - lastMove, 0., .1);
		lastMove = seconds;
		if(!cursorMode)
			return;
		const int dx = dpad[3] - dpad[2], dy = dpad[1] - dpad[0];
		if(!dx && !dy)
		{
			cursorHeldTime = 0.;
			return;
		}
		inputQuiet = 0;
		// Starts slow for precise clicks (a tap moves a few units), then speeds up to
		// crossing the screen in about half a second; units are logical, per second.
		cursorHeldTime += elapsed;
		const double speed = min(1600., 120. + cursorHeldTime * 2400.);
		cursorX = clamp(cursorX + dx * speed * elapsed, 0., LOGICAL_WIDTH - 1.);
		cursorY = clamp(cursorY + dy * speed * elapsed, 0., LOGICAL_HEIGHT - 1.);
		SDL_SendMouseMotion(window, 0, 0, static_cast<int>(cursorX), static_cast<int>(cursorY));
	}

	// A small arrow, white with a black edge, drawn onto the shown copy of the frame (so a
	// frame can be shown again without redrawing it). The panel is upside down.
	void DrawCursor(uint8_t *pageStart)
	{
		if(!cursorMode)
			return;
		const int ox = static_cast<int>(cursorX * PIXEL_WIDTH / LOGICAL_WIDTH);
		const int oy = static_cast<int>(cursorY * PIXEL_HEIGHT / LOGICAL_HEIGHT);
		for(int y = 0; y < 14; ++y)
			for(int x = 0; x <= y / 2 + 1 && x < 8; ++x)
			{
				const int px = ox + x, py = oy + y;
				if(px < 0 || py < 0 || px >= PIXEL_WIDTH || py >= PIXEL_HEIGHT)
					continue;
				const bool edge = x == 0 || x == y / 2 + 1 || y == 13;
				uint32_t *row = reinterpret_cast<uint32_t *>(pageStart + static_cast<size_t>(PIXEL_HEIGHT - 1 - py) * fbStride);
				row[PIXEL_WIDTH - 1 - px] = edge ? 0xFF000000 : 0xFFFFFFFF;
			}
	}

	// A crash writes where it happened to stderr (the launcher's last-run.log): the faulting
	// and return addresses and the code mappings. The binary is not position independent, so
	// `addr2line -fCe build-arm/endless-sky <address>` decodes addresses inside it.
	void OnCrash(int signal, siginfo_t *info, void *context)
	{
		// Only async-signal-safe calls here: the crash may be inside malloc.
		const auto &machine = static_cast<ucontext_t *>(context)->uc_mcontext;
		char line[160];
		const int length = snprintf(line, sizeof(line), "Crashed (signal %d) at pc %#lx lr %#lx, address %p\n",
			signal, static_cast<unsigned long>(machine.arm_pc), static_cast<unsigned long>(machine.arm_lr), info->si_addr);
		if(write(STDERR_FILENO, line, length) < 0)
			_exit(128 + signal);
		// The code mappings, to tell which library an address is in.
		const int maps = open("/proc/self/maps", O_RDONLY);
		char buffer[4096];
		ssize_t got;
		while(maps >= 0 && (got = read(maps, buffer, sizeof(buffer))) > 0)
			if(write(STDERR_FILENO, buffer, got) < 0)
				break;
		_exit(128 + signal);
	}

	// The status bar, as on the handheld's own screens: clock, Wi-Fi and battery, read at
	// most once a second from what Onion keeps (its battery monitor writes /tmp/percBat).
	struct Status {
		string time;
		int wifiBars = 0;
		int battery = -1;
		time_t nextRead = 0;
	} status;

	void ReadStatus()
	{
		const time_t now = time(nullptr);
		if(now < status.nextRead)
			return;
		status.nextRead = now + 1;
		// Onion passes TZ to games it launches; started any other way, read its setting.
		static const bool zoneSet = []
		{
			if(!getenv("TZ"))
				if(FILE *zone = fopen("/mnt/SDCARD/.tmp_update/config/.tz", "r"))
				{
					char line[64] = {};
					if(fgets(line, sizeof(line), zone))
					{
						line[strcspn(line, "\r\n")] = 0;
						setenv("TZ", line, 1);
					}
					fclose(zone);
				}
			tzset();
			return true;
		}();
		(void)zoneSet;
		tm local;
		localtime_r(&now, &local);
		char clock[8];
		strftime(clock, sizeof(clock), "%H:%M", &local);
		status.time = clock;

		status.battery = -1;
		if(FILE *battery = fopen("/tmp/percBat", "r"))
		{
			if(fscanf(battery, "%d", &status.battery) != 1)
				status.battery = -1;
			fclose(battery);
		}
		// Link quality (0..100) from /proc/net/wireless, as 0 to 3 bars.
		status.wifiBars = 0;
		if(FILE *wireless = fopen("/proc/net/wireless", "r"))
		{
			char line[256];
			while(fgets(line, sizeof(line), wireless))
			{
				float quality = 0.f;
				if(strstr(line, "wlan0:") && sscanf(strstr(line, ":") + 1, "%*s %f", &quality) == 1)
					status.wifiBars = quality >= 70.f ? 3 : quality >= 40.f ? 2 : quality > 0.f ? 1 : 0;
			}
			fclose(wireless);
		}
	}

	void DrawStatus()
	{
		ReadStatus();
		const Font &font = FontSet::Get(14);
		const Color &bright = *GameData::Colors().Get("bright");
		const Color &dim = *GameData::Colors().Get("dim");
		const string percent = status.battery >= 0 ? to_string(status.battery) + "%" : "";
		// Right edge at x = 200: in flight that is the gap between the HUD's top boxes.
		const double height = 18., right = 200., top = -LOGICAL_HEIGHT / 2.;
		const double width = 10. + font.Width(status.time) + 10. + 14. + 10. + 26. + 4. + font.Width(percent) + 8.;
		double x = right - width;
		FillShader::Fill(Point(x + width / 2., top + height / 2.), Point(width, height), Color(0.f, 0.f, 0.f, .55f));
		const double textY = top + (height - font.Height()) / 2.;
		x += 10.;
		font.Draw(status.time, Point(x, textY), bright);
		x += font.Width(status.time) + 10.;
		// Wi-Fi: three rising bars, lit by link quality.
		for(int i = 0; i < 3; ++i)
		{
			const double barHeight = 4. + 3. * i;
			FillShader::Fill(Point(x + 2. + 5. * i, top + height - 4. - barHeight / 2.), Point(3., barHeight),
				i < status.wifiBars ? bright : dim);
		}
		x += 14. + 10.;
		// Battery: an outline with a nub and a fill as long as the charge, colour by level.
		if(status.battery >= 0)
		{
			const double bodyWidth = 22., bodyHeight = 10., centerY = top + height / 2.;
			FillShader::Fill(Point(x + bodyWidth / 2., centerY), Point(bodyWidth, bodyHeight), bright);
			FillShader::Fill(Point(x + bodyWidth / 2., centerY), Point(bodyWidth - 2., bodyHeight - 2.), Color(0.f, 0.f, 0.f, 1.f));
			FillShader::Fill(Point(x + bodyWidth + 1.5, centerY), Point(3., 4.), bright);
			const double charge = min(100, max(0, status.battery)) / 100. * (bodyWidth - 4.);
			const Color level = status.battery < 15 ? Color(.9f, .2f, .2f, 1.f)
				: status.battery < 30 ? Color(.9f, .75f, .2f, 1.f) : Color(.35f, .85f, .4f, 1.f);
			FillShader::Fill(Point(x + 2. + charge / 2., centerY), Point(charge, bodyHeight - 4.), level);
			x += bodyWidth + 4. + 4.;
			font.Draw(percent, Point(x, textY), bright);
		}
	}

	// The on-screen keyboard, drawn into the frame (logical units) across the bottom.
	void DrawKeyboard()
	{
		constexpr double KEY_WIDTH = 80., KEY_HEIGHT = 36.;
		// At the top: the pilot name fields sit at the bottom of the conversation text.
		const double top = -LOGICAL_HEIGHT / 2.;
		const Color back(.05f, .05f, .07f, .92f), key(.16f, .16f, .2f, 1.f), selected(.55f, .45f, .15f, 1.f);
		const Color &bright = *GameData::Colors().Get("bright");
		const Font &font = FontSet::Get(14);
		FillShader::Fill(Point(0., top + KEY_ROW_COUNT * KEY_HEIGHT / 2. + 3.),
			Point(LOGICAL_WIDTH, KEY_ROW_COUNT * KEY_HEIGHT + 6.), back);
		auto drawKey = [&](int row, int first, int last, const string &label)
		{
			const bool isSelected = keyRow == row && keyColumn >= first && keyColumn < last;
			const Point center(-LOGICAL_WIDTH / 2. + (first + last) * KEY_WIDTH / 2., top + (row + .5) * KEY_HEIGHT + 3.);
			FillShader::Fill(center, Point((last - first) * KEY_WIDTH - 4., KEY_HEIGHT - 4.), isSelected ? selected : key);
			font.Draw(label, center - Point(font.Width(label) / 2., font.Height() / 2.), bright);
		};
		for(int row = 0; row < KEY_ROW_COUNT - 1; ++row)
			for(int column = 0; column < KEY_COLUMNS; ++column)
			{
				char c = KEY_ROWS[row][column];
				if(shiftOn && c >= 'a' && c <= 'z')
					c += 'A' - 'a';
				drawKey(row, column, column + 1, string(1, c));
			}
		const char *const SPECIAL[] = {shiftOn ? "SHIFT" : "Shift", "Space", "Del", "Done"};
		for(int i = 0; i < 4; ++i)
			drawKey(KEY_ROW_COUNT - 1, SPECIAL_START[i], SPECIAL_START[i + 1], SPECIAL[i]);
		const string help = "A type  B delete  X shift  Start done  L2/R2 scroll  Y hide";
		FillShader::Fill(Point(0., top + KEY_ROW_COUNT * KEY_HEIGHT + 6. + 10.), Point(LOGICAL_WIDTH, 20.), back);
		font.Draw(help, Point(-font.Width(help) / 2., top + KEY_ROW_COUNT * KEY_HEIGHT + 6. + 10. - font.Height() / 2.), bright);
	}

	double Now()
	{
		timespec t;
		clock_gettime(CLOCK_MONOTONIC, &t);
		return t.tv_sec + t.tv_nsec * 1e-9;
	}

	// ES_PROFILE=1: every 120 frames, log frame rate, game time (update and drawing) and present time.
	struct Profile {
		bool enabled = getenv("ES_PROFILE") != nullptr;
		int frames = 0;
		double windowStart = 0., lastEnd = 0., game = 0., present = 0.;
		int skipped = 0;
	} profile;

	void Present()
	{
		if(!fbMap)
			return;
		const uint32_t *src = Raster::Pixels();
		page = 1 - page;
		// Rotate 180 degrees: the last source pixel goes first. Rows are tightly packed
		// (stride 2560 = 640 * 4 on this device); otherwise copy row by row.
		uint8_t *pageStart = fbMap + static_cast<size_t>(page) * PIXEL_HEIGHT * fbStride;
		const int count = PIXEL_WIDTH * PIXEL_HEIGHT;
		if(fbStride == PIXEL_WIDTH * 4)
		{
			uint32_t *dst = reinterpret_cast<uint32_t *>(pageStart);
			for(int i = 0; i < count; ++i)
				dst[i] = src[count - 1 - i] | 0xFF000000;
		}
		else
			for(int y = 0; y < PIXEL_HEIGHT; ++y)
			{
				uint32_t *dst = reinterpret_cast<uint32_t *>(pageStart + static_cast<size_t>(y) * fbStride);
				const uint32_t *row = src + (PIXEL_HEIGHT - 1 - y) * PIXEL_WIDTH;
				for(int x = 0; x < PIXEL_WIDTH; ++x)
					dst[x] = row[PIXEL_WIDTH - 1 - x] | 0xFF000000;
			}
		DrawCursor(pageStart);
		fbVar.yoffset = page * PIXEL_HEIGHT;
		Raster::ProfScope prof(Raster::PROF_FLIP);
		ioctl(fbFd, FBIOPAN_DISPLAY, &fbVar);
	}
}



string GameWindow::SDLVersions()
{
	SDL_version linked;
	SDL_GetVersion(&linked);
	return "Using SDL v" + to_string(linked.major) + "." + to_string(linked.minor) + "."
		+ to_string(linked.patch) + " (events only), Miyoo framebuffer.";
}



bool GameWindow::Init(bool headless)
{
	struct sigaction crash = {};
	crash.sa_sigaction = OnCrash;
	crash.sa_flags = SA_SIGINFO;
	for(int signal : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT})
		sigaction(signal, &crash, nullptr);
	Raster::Init(PIXEL_WIDTH, PIXEL_HEIGHT);
	Raster::SetView(LOGICAL_WIDTH, LOGICAL_HEIGHT);

	// SDL provides the event queue, timers and keyboard / mouse state; no video output.
	SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
	if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
	{
		ExitWithError("Unable to initialize SDL!");
		return false;
	}
	window = SDL_CreateWindow("Endless Sky", 0, 0, LOGICAL_WIDTH, LOGICAL_HEIGHT, 0);
	if(!window)
	{
		ExitWithError("Unable to create window!");
		return false;
	}
	SDL_SetKeyboardFocus(window);
	SDL_SetMouseFocus(window);
	// SDL starts with text input on; edit fields turn it on while they have focus.
	SDL_StopTextInput();
	Screen::SetRaw(LOGICAL_WIDTH, LOGICAL_HEIGHT, true);
	if(headless)
		return true;

	fbFd = open("/dev/fb0", O_RDWR | O_CLOEXEC);
	fb_fix_screeninfo fix = {};
	if(fbFd < 0 || ioctl(fbFd, FBIOGET_VSCREENINFO, &fbVar) || ioctl(fbFd, FBIOGET_FSCREENINFO, &fix))
	{
		ExitWithError("Unable to open the framebuffer!");
		return false;
	}
	if(static_cast<int>(fbVar.xres) != PIXEL_WIDTH || static_cast<int>(fbVar.yres) != PIXEL_HEIGHT
		|| fbVar.bits_per_pixel != 32 || fbVar.yres_virtual < 2 * fbVar.yres)
	{
		ExitWithError("Unexpected framebuffer layout (expected 640x480, 32 bits, double buffered).");
		return false;
	}
	fbStride = fix.line_length;
	fbSize = static_cast<size_t>(fbStride) * fbVar.yres_virtual;
	void *map = mmap(nullptr, fbSize, PROT_READ | PROT_WRITE, MAP_SHARED, fbFd, 0);
	if(map == MAP_FAILED)
	{
		ExitWithError("Unable to map the framebuffer!");
		return false;
	}
	fbMap = static_cast<uint8_t *>(map);

	inputFd = open("/dev/input/event0", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if(inputFd < 0)
		Logger::Log("No /dev/input/event0: buttons will not work.", Logger::Level::WARNING);
	// ES_GRAB_INPUT=1 (test runs with injected presses): only the game gets the buttons, so
	// a paused MainUI doesn't act on them later. Normal play leaves them shared with keymon.
	else if(getenv("ES_GRAB_INPUT"))
		ioctl(inputFd, EVIOCGRAB, 1);
	return true;
}



void GameWindow::Quit()
{
	ReleaseAll();
	if(fbMap)
	{
		// Leave a black screen for the menu to redraw over.
		memset(fbMap, 0, fbSize);
		munmap(fbMap, fbSize);
	}
	if(fbFd >= 0)
		close(fbFd);
	if(inputFd >= 0)
		close(inputFd);
	fbMap = nullptr;
	fbFd = inputFd = -1;
	if(window)
		SDL_DestroyWindow(window);
	window = nullptr;
	SDL_Quit();
}



void GameWindow::StepWithoutDraw()
{
	++skippedSteps;
}



bool GameWindow::SkipDraw()
{
	if(!skipPending)
		return false;
	--skipPending;
	++skippedSteps;
	++profile.skipped;
	return true;
}



void GameWindow::WantText()
{
	textWantedFrame = frameNumber;
}



string GameWindow::ButtonName(int keycode)
{
	for(const Binding &binding : BINDINGS)
		if(SDL_GetKeyFromScancode(binding.key) == keycode)
			return binding.label;
	return "(no button)";
}



bool GameWindow::ReuseFrame()
{
	// The keyboard depends on WantText() calls made while drawing.
	if(keyboardVisible || inputQuiet < QUIET_FRAMES || !sameAsLast || reusedRun >= MAX_REUSE)
	{
		reusedRun = 0;
		return false;
	}
	++reusedRun;
	reusing = true;
	return true;
}



void GameWindow::Step()
{
	const double start = profile.enabled ? Now() : 0.;
	Raster::SetLinearFiltering(Preferences::Has(Preferences::TEXTURE_FILTERING));
	// The keyboard shows while the game wants text (re-armed once it stops) or when opened by hand.
	const bool textWanted = SDL_IsTextInputActive() || frameNumber - textWantedFrame <= 1;
	if(!textWanted)
		keyboardDismissed = false;
	keyboardVisible = keyboardOpened || (textWanted && !keyboardDismissed);
	++frameNumber;
	if(!reusing)
		DrawStatus();
	if(keyboardVisible && !reusing)
		DrawKeyboard();
	SpriteLoadManager::PumpSprites();
	if(reusing)
		reusing = false;
	else
	{
		const uint32_t hash = FrameHash(Raster::Pixels(), Raster::Width() * Raster::Height());
		sameAsLast = (hash == lastHash);
		lastHash = hash;
		Present();
	}
	++inputQuiet;
	// Charge this frame's time against the game time it showed, then skip enough
	// steps to pay the debt back. Outside flight every step is drawn.
	const double now = Now();
	if(inFlightNow && lastDrawn)
	{
		drawDebt = clamp(drawDebt + (now - lastDrawn) - (1 + skippedSteps) * STEP_SECONDS,
			-STEP_SECONDS, (MAX_SKIP + 1) * STEP_SECONDS);
		// The skipped steps are paid for when the next drawn frame counts them above.
		skipPending = clamp(static_cast<int>(drawDebt / STEP_SECONDS), 0, MAX_SKIP);
	}
	else
		drawDebt = skipPending = 0;
	skippedSteps = 0;
	lastDrawn = now;
	ReadButtons();
	MoveCursor();
	if(!profile.enabled)
		return;
	const double end = Now();
	if(profile.lastEnd)
		profile.game += start - profile.lastEnd;
	profile.present += end - start;
	profile.lastEnd = end;
	if(!profile.windowStart)
		profile.windowStart = start;
	if(++profile.frames == 120)
	{
		const double seconds = end - profile.windowStart;
		Logger::Log("profile: " + to_string(static_cast<int>(120 / seconds + .5)) + " fps, game speed "
			+ to_string(static_cast<int>((120 + profile.skipped) / seconds / 60. * 100. + .5)) + "%, game "
			+ to_string(static_cast<int>(profile.game / 120 * 1000 + .5)) + " ms, present "
			+ to_string(static_cast<int>(profile.present / 120 * 1000 + .5)) + " ms, textures "
			+ to_string(Raster::TextureBytes() >> 20) + " MB, pointer " + to_string(static_cast<int>(cursorX)) + ","
			+ to_string(static_cast<int>(cursorY)) + (cursorMode ? " on" : " off"), Logger::Level::INFO);
		string parts = "profile ms/frame (calls):";
		for(int i = 0; i < Raster::PROF_COUNT; ++i)
		{
			if(Raster::profCalls[i])
				parts += " " + string(Raster::profNames[i]) + " " + to_string(Raster::profTime[i] / 120 * 1000).substr(0, 5)
					+ " (" + to_string(Raster::profCalls[i] / 120) + ")";
			Raster::profTime[i] = 0.;
			Raster::profCalls[i] = 0;
		}
		Logger::Log(parts, Logger::Level::INFO);
		profile.frames = 0;
		profile.game = profile.present = 0.;
		profile.skipped = 0;
		profile.windowStart = end;
	}
}



void GameWindow::SetInFlight(bool inFlight)
{
	if(inFlight == inFlightNow)
		return;
	// Switch to the controls that suit the new screen; Select still flips them by hand.
	inFlightNow = inFlight;
	ReleaseAll();
	cursorMode = !inFlight;
}



void GameWindow::AdjustViewport(bool noResizeEvent)
{
	Screen::SetRaw(LOGICAL_WIDTH, LOGICAL_HEIGHT, noResizeEvent);
}



bool GameWindow::SetVSync(Preferences::VSync state)
{
	// Flipping pages waits for the display; there is nothing else to set.
	return state != Preferences::VSync::ADAPTIVE;
}



int GameWindow::Width()
{
	return LOGICAL_WIDTH;
}



int GameWindow::Height()
{
	return LOGICAL_HEIGHT;
}



int GameWindow::DrawWidth()
{
	return LOGICAL_WIDTH;
}



int GameWindow::DrawHeight()
{
	return LOGICAL_HEIGHT;
}



bool GameWindow::IsInFocus()
{
	return true;
}



bool GameWindow::IsMaximized()
{
	return false;
}



bool GameWindow::IsFullscreen()
{
	return true;
}



void GameWindow::ToggleFullscreen()
{
}



void GameWindow::ToggleBlockScreenSaver()
{
}



void GameWindow::ExitWithError(const string &message, bool)
{
	Logger::Log(message, Logger::Level::ERROR);
	if(const char *error = SDL_GetError(); error && *error)
		Logger::Log(string("(SDL message: \"") + error + "\")", Logger::Level::ERROR);
	Quit();
}

#endif
