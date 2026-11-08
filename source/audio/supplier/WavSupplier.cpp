/* WavSupplier.cpp
Copyright (c) 2025 by tibetiroka

Endless Sky is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later version.

Endless Sky is distributed in the hope that it will be useful, but WITHOUT ANY
WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE. See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License along with
this program. If not, see <https://www.gnu.org/licenses/>.
*/

#include "WavSupplier.h"

#include "../Sound.h"

#include <algorithm>
#include <cmath>

using namespace std;



WavSupplier::WavSupplier(const Sound &sound, bool is3x, bool looping)
	: AudioSupplier(is3x, looping), sound(sound), wasStarted(false)
{
}



#ifdef ES_MIYOO
const shared_ptr<const vector<AudioSupplier::sample_t>> &WavSupplier::Stored(bool fast) const
{
	auto &stored = fast ? mono3x : mono;
	if(!stored)
		stored = sound.Samples(fast);
	return stored;
}
#endif



size_t WavSupplier::MaxChunks() const
{
	if(isLooping)
		return 2;
	else if(wasStarted && !currentSample)
		return 0;
	else
	{
#ifdef ES_MIYOO
		// Positions count stereo samples; the stored sound is mono.
		const auto &input = Stored(is3x);
		const size_t total = input ? 2 * input->size() : 0;
#else
		const size_t total = (is3x ? sound.Buffer3x() : sound.Buffer()).size();
#endif
		return ceil((total - currentSample) / static_cast<float>(OUTPUT_CHUNK));
	}
}



size_t WavSupplier::AvailableChunks() const
{
	return MaxChunks();
}



vector<AudioSupplier::sample_t> WavSupplier::NextDataChunk()
{
	vector<sample_t> samples(OUTPUT_CHUNK);
	// If we are at the beginning of the buffer and it was already played, this is a loop.
	if(!currentSample && wasStarted && !isLooping)
		return samples;

	size_t currentSampleCount = 0;
	do {
		// If restarting the buffer, check 3x status.
		if(!currentSample)
		{
			is3x = nextPlaybackIs3x;
			wasStarted = true;
		}
#ifdef ES_MIYOO
		const auto &stored = Stored(is3x);
		if(!stored || stored->empty())
			break;
		const size_t total = 2 * stored->size();
		size_t readChunk = min(total - currentSample, samples.size() - currentSampleCount);
		// Both channels get the mono sample.
		for(size_t i = 0; i < readChunk; ++i)
			samples[currentSampleCount + i] = (*stored)[(currentSample + i) / 2];
		currentSampleCount += readChunk;
		currentSample = (currentSample + readChunk) % total;
#else
		const vector<sample_t> &input = is3x ? sound.Buffer3x() : sound.Buffer();
		size_t readChunk = min(input.size() - currentSample, samples.size() - currentSampleCount);
		std::copy_n(input.begin() + currentSample, readChunk, samples.begin() + currentSampleCount);
		currentSampleCount += readChunk;
		currentSample = (currentSample + readChunk) % input.size();
#endif
	} while(currentSampleCount < samples.size() && isLooping);
	return samples;
}

