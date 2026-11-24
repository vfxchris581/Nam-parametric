#pragma once
// The dry signal the source captures are re-rendered with ("re-amped" through each NAM capture).
// Because the teachers are neural captures rather than a live amp, any signal works; what
// matters is that it covers what a guitarist plays: single notes and chords over the whole
// neck, soft to hard picking, palm-mutes, sustained notes, plus sweeps and noise for broadband
// coverage. Users can add their own DI recordings, which is recommended for final models.
#include <cstdint>
#include <filesystem>
#include <vector>

namespace pnam
{
struct TrainingSignalOptions
{
    double sampleRate = 48000.0;
    double seconds = 60.0;
    uint32_t seed = 1;
    float peakDbfs = -1.0f;   // loudest picking
    float softestDbfs = -36.0f;
    std::vector<std::filesystem::path> userDiFiles; // appended (resampled is NOT done: must match sampleRate)
};

std::vector<float> makeTrainingSignal(const TrainingSignalOptions& o);
} // namespace pnam
