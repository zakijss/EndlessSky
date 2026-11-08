/* GameWindow.cpp
Software-rendered GameWindow for builds without a GPU.

The game always sees a 1024x768 screen at 100% zoom, as on desktop; the
rasterizer draws it into a 640x480 framebuffer (scale 0.625), the Miyoo Mini
Plus screen. On desktop the framebuffer is shown in an SDL window, and SDL's
logical size maps the mouse back to 1024x768.

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

// The desktop preview; the Miyoo build uses GameWindowMiyoo.cpp.
#ifndef ES_MIYOO

#include "../Preferences.h"
#include "../GameWindow.h"

#include "../image/SpriteLoadManager.h"
#include "../Logger.h"
#include "../Screen.h"
#include "Raster.h"

#include <SDL2/SDL.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>
#include <algorithm>
#include <string>

using namespace std;

namespace {
	// What the game lays out for, and what is actually drawn.
	// The screen the game lays itself out for. 1024x768 is the desktop minimum; smaller sizes
	// draw everything larger on the 640x480 panel. ES_LOGICAL=<width>x<height> overrides it.
	int LogicalSize(bool height)
	{
		static const auto size = []
		{
			int w = 1024, h = 768;
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
	// Desktop preview window: the 640x480 picture shown at 2x.
	constexpr int WINDOW_SCALE = 2;

	SDL_Window *window = nullptr;
	SDL_Renderer *renderer = nullptr;
	SDL_Texture *texture = nullptr;
	bool fullscreen = false;
}



string GameWindow::SDLVersions()
{
	SDL_version built;
	SDL_version linked;
	SDL_VERSION(&built);
	SDL_GetVersion(&linked);
	return "Compiled against SDL v" + to_string(built.major) + "." + to_string(built.minor) + "."
		+ to_string(built.patch) + ", using SDL v" + to_string(linked.major) + "." + to_string(linked.minor)
		+ "." + to_string(linked.patch) + ".";
}



bool GameWindow::Init(bool headless)
{
	Raster::Init(PIXEL_WIDTH, PIXEL_HEIGHT);
	Raster::SetView(LOGICAL_WIDTH, LOGICAL_HEIGHT);

	if(headless)
		SDL_SetHint(SDL_HINT_VIDEODRIVER, "dummy");
	if(SDL_Init(SDL_INIT_VIDEO))
	{
		ExitWithError("Unable to initialize SDL!");
		return false;
	}
	window = SDL_CreateWindow("Endless Sky", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		PIXEL_WIDTH * WINDOW_SCALE, PIXEL_HEIGHT * WINDOW_SCALE, headless ? 0 : SDL_WINDOW_SHOWN);
	if(!window)
	{
		ExitWithError("Unable to create window!");
		return false;
	}
	Screen::SetRaw(LOGICAL_WIDTH, LOGICAL_HEIGHT, true);
	if(headless)
		return true;

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if(!renderer)
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	if(!renderer)
	{
		ExitWithError("Unable to create a renderer!");
		return false;
	}
	// Mouse events arrive in the game's 1024x768 coordinates.
	SDL_RenderSetLogicalSize(renderer, LOGICAL_WIDTH, LOGICAL_HEIGHT);
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
	texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
		PIXEL_WIDTH, PIXEL_HEIGHT);
	if(!texture)
	{
		ExitWithError("Unable to create the screen texture!");
		return false;
	}
	return true;
}



void GameWindow::Quit()
{
	if(texture)
		SDL_DestroyTexture(texture);
	if(renderer)
		SDL_DestroyRenderer(renderer);
	if(window)
		SDL_DestroyWindow(window);
	texture = nullptr;
	renderer = nullptr;
	window = nullptr;
	SDL_Quit();
}



void GameWindow::Step()
{
	Raster::SetLinearFiltering(Preferences::Has(Preferences::TEXTURE_FILTERING));
	SpriteLoadManager::PumpSprites();
	// Test hook: ES_SHOT=<file.ppm> saves frame ES_SHOT_FRAME (default 60) and exits. If the
	// name holds %d, it also saves every ES_SHOT_EVERY frames (file name numbered by frame).
	if(const char *shot = getenv("ES_SHOT"))
	{
		static int frameCount = 0;
		const char *at = getenv("ES_SHOT_FRAME");
		const char *every = getenv("ES_SHOT_EVERY");
		++frameCount;
		const bool last = frameCount == (at ? atoi(at) : 60);
		const bool numbered = strstr(shot, "%d") != nullptr;
		if(last || (numbered && every && atoi(every) > 0 && frameCount % atoi(every) == 0))
		{
			char name[512];
			snprintf(name, sizeof(name), shot, frameCount);
			if(FILE *out = fopen(numbered ? name : shot, "wb"))
			{
				fprintf(out, "P6\n%d %d\n255\n", PIXEL_WIDTH, PIXEL_HEIGHT);
				const uint32_t *pixels = Raster::Pixels();
				for(int i = 0; i < PIXEL_WIDTH * PIXEL_HEIGHT; ++i)
				{
					const unsigned char rgb[3] = {static_cast<unsigned char>(pixels[i] >> 16),
						static_cast<unsigned char>(pixels[i] >> 8), static_cast<unsigned char>(pixels[i])};
					fwrite(rgb, 1, 3, out);
				}
				fclose(out);
			}
			if(last)
				exit(0);
		}
	}
	if(!texture)
		return;
	SDL_UpdateTexture(texture, nullptr, Raster::Pixels(), PIXEL_WIDTH * 4);
	SDL_RenderClear(renderer);
	SDL_RenderCopy(renderer, texture, nullptr, nullptr);
	SDL_RenderPresent(renderer);
}



void GameWindow::AdjustViewport(bool noResizeEvent)
{
	Screen::SetRaw(LOGICAL_WIDTH, LOGICAL_HEIGHT, noResizeEvent);
}



bool GameWindow::SetVSync(Preferences::VSync state)
{
	// The renderer presents with vsync; the game's own frame timer paces the rest.
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
	return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS);
}



bool GameWindow::IsMaximized()
{
	return false;
}



bool GameWindow::IsFullscreen()
{
	return fullscreen;
}



void GameWindow::ToggleFullscreen()
{
	if(!window)
		return;
	fullscreen = !fullscreen;
	SDL_SetWindowFullscreen(window, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
}



void GameWindow::ToggleBlockScreenSaver()
{
	if(SDL_IsScreenSaverEnabled())
		SDL_DisableScreenSaver();
	else
		SDL_EnableScreenSaver();
}



void GameWindow::ExitWithError(const string &message, bool doPopUp)
{
	Logger::Log(message, Logger::Level::ERROR);
	if(const char *error = SDL_GetError(); error && *error)
		Logger::Log(string("(SDL message: \"") + error + "\")", Logger::Level::ERROR);
	if(doPopUp)
		SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Endless Sky: Error", message.c_str(), nullptr);
	Quit();
}



#ifdef _WIN32
void GameWindow::UpdateTitleBarTheme()
{
}



void GameWindow::UpdateWindowRounding()
{
}
#endif

#endif
