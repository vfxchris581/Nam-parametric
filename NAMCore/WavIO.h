#pragma once
// Minimal, dependency-free WAV reader/writer used by the trainer, the CLI and the tests.
// Reads PCM 16/24/32-bit and IEEE float 32/64-bit, any channel count (mixed to mono on request).
#include <filesystem>
#include <string>
#include <vector>

namespace pnam
{
struct WavData
{
    double sampleRate = 0.0;
    int channels = 0;
    std::vector<std::vector<float>> data; // [channel][frame]
    size_t frames() const { return data.empty() ? 0 : data[0].size(); }
};

// Throws std::runtime_error with a readable message on failure.
WavData readWav(const std::filesystem::path& path);
std::vector<float> readWavMono(const std::filesystem::path& path, double* sampleRate = nullptr);

// Writes 32-bit float WAV.
void writeWav(const std::filesystem::path& path, const std::vector<std::vector<float>>& channels, double sampleRate);
void writeWavMono(const std::filesystem::path& path, const std::vector<float>& mono, double sampleRate);
} // namespace pnam
