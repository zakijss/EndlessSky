// Self-check for the Miyoo sound path (on-demand mono samples, WavSupplier stereo output):
// c++ -std=c++20 -DES_MIYOO -I source miyoo/tests/sound_test.cpp source/audio/Sound.cpp
//   source/audio/supplier/WavSupplier.cpp source/audio/supplier/AudioSupplier.cpp -lopenal -o /tmp/st
#include "audio/Sound.h"
#include "audio/supplier/WavSupplier.h"
#include "Files.h"
#include "Logger.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

// Stand-ins for the game's file and log helpers.
std::shared_ptr<std::iostream> Files::Open(const std::filesystem::path &path, bool)
{
	std::ifstream in(path, std::ios::binary);
	if(!in)
		return nullptr;
	auto out = std::make_shared<std::stringstream>();
	*out << in.rdbuf();
	return out;
}
void Logger::Log(const std::string &message, Level) { std::fprintf(stderr, "%s\n", message.c_str()); }

static void WriteWav(const char *path, int count, int start)
{
	std::ofstream out(path, std::ios::binary);
	auto u32 = [&](uint32_t v) { out.write(reinterpret_cast<char *>(&v), 4); };
	auto u16 = [&](uint16_t v) { out.write(reinterpret_cast<char *>(&v), 2); };
	out.write("RIFF", 4); u32(36 + 2 * count); out.write("WAVE", 4);
	out.write("fmt ", 4); u32(16); u16(1); u16(1); u32(44100); u32(88200); u16(2); u16(16);
	out.write("data", 4); u32(2 * count);
	for(int i = 0; i < count; ++i)
		u16(static_cast<uint16_t>(start + i));
}

int main()
{
	WriteWav("/tmp/a.wav", 1000, 0);
	Sound a;
	assert(a.IsEmpty() && a.Load("/tmp/a.wav", "a") && !a.IsEmpty());

	// One chunk: every mono sample on both channels, then silence once played.
	WavSupplier supplier(a, false);
	std::vector<AudioSupplier::sample_t> chunk = supplier.NextDataChunk();
	assert(chunk.size() >= 2000);
	for(int i = 0; i < 1000; ++i)
		assert(chunk[2 * i] == i && chunk[2 * i + 1] == i);
	assert(supplier.MaxChunks() == 0);

	// A looping sound wraps around.
	WriteWav("/tmp/b~.wav", 10, 100);
	Sound b;
	b.Load("/tmp/b~.wav", "b");
	assert(b.IsLooping());
	WavSupplier loop(b, false, true);
	chunk = loop.NextDataChunk();
	assert(chunk[0] == 100 && chunk[19] == 109 && chunk[20] == 100);

	// Over budget (ES_SOUND_BUDGET_MB=1 below), unplayed sounds are dropped but a held one stays.
	WriteWav("/tmp/big.wav", 400000, 0);
	Sound big;
	big.Load("/tmp/big.wav", "big");
	auto held = a.Samples(false);
	auto first = big.Samples(false);
	assert(held && held->size() == 1000 && first && first->size() == 400000);
	assert(held == a.Samples(false));
	puts("sound ok");
}
