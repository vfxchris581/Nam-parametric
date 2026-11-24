#include "WavIO.h"

#include <cstdint>
#include <cstring>
#include <fstream>
#include <stdexcept>

namespace pnam
{
namespace
{
uint32_t u32(const unsigned char* p) { return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24); }
uint16_t u16(const unsigned char* p) { return (uint16_t) (p[0] | (p[1] << 8)); }

void put32(std::ofstream& f, uint32_t v)
{
    unsigned char b[4] = { (unsigned char) v, (unsigned char) (v >> 8), (unsigned char) (v >> 16), (unsigned char) (v >> 24) };
    f.write((const char*) b, 4);
}
void put16(std::ofstream& f, uint16_t v)
{
    unsigned char b[2] = { (unsigned char) v, (unsigned char) (v >> 8) };
    f.write((const char*) b, 2);
}
} // namespace

WavData readWav(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot open " + path.string());
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (bytes.size() < 12 || std::memcmp(bytes.data(), "RIFF", 4) != 0 || std::memcmp(bytes.data() + 8, "WAVE", 4) != 0)
        throw std::runtime_error(path.string() + " is not a RIFF/WAVE file");

    uint16_t format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const unsigned char* pcm = nullptr;
    size_t pcmSize = 0;
    size_t p = 12;
    while (p + 8 <= bytes.size())
    {
        const unsigned char* c = bytes.data() + p;
        const uint32_t size = u32(c + 4);
        const size_t body = p + 8;
        if (body + size > bytes.size() && std::memcmp(c, "data", 4) != 0)
            break;
        if (std::memcmp(c, "fmt ", 4) == 0 && size >= 16)
        {
            format = u16(c + 8);
            channels = u16(c + 10);
            rate = u32(c + 12);
            bits = u16(c + 22);
            if (format == 0xFFFE && size >= 40)
                format = u16(c + 8 + 24); // WAVE_FORMAT_EXTENSIBLE: sub-format GUID starts with the format tag
        }
        else if (std::memcmp(c, "data", 4) == 0)
        {
            pcm = bytes.data() + body;
            pcmSize = std::min<size_t>(size, bytes.size() - body);
        }
        p = body + size + (size & 1);
    }
    if (pcm == nullptr || channels == 0 || rate == 0)
        throw std::runtime_error(path.string() + ": missing fmt or data chunk");
    if (!((format == 1 && (bits == 16 || bits == 24 || bits == 32)) || (format == 3 && (bits == 32 || bits == 64))))
        throw std::runtime_error(path.string() + ": unsupported WAV encoding (format " + std::to_string(format) + ", "
                                 + std::to_string(bits) + " bit)");

    const size_t bps = bits / 8;
    const size_t frames = pcmSize / (bps * channels);
    WavData w;
    w.sampleRate = rate;
    w.channels = channels;
    w.data.assign(channels, std::vector<float>(frames));
    for (size_t i = 0; i < frames; ++i)
        for (size_t ch = 0; ch < channels; ++ch)
        {
            const unsigned char* s = pcm + (i * channels + ch) * bps;
            float v = 0.0f;
            if (format == 1)
            {
                if (bits == 16)
                    v = (float) (int16_t) u16(s) / 32768.0f;
                else if (bits == 24)
                {
                    int32_t x = (int32_t) ((uint32_t) s[0] << 8 | (uint32_t) s[1] << 16 | (uint32_t) s[2] << 24) >> 8;
                    v = (float) x / 8388608.0f;
                }
                else
                    v = (float) ((double) (int32_t) u32(s) / 2147483648.0);
            }
            else if (bits == 32)
            {
                uint32_t x = u32(s);
                std::memcpy(&v, &x, 4);
            }
            else
            {
                uint64_t x = (uint64_t) u32(s) | ((uint64_t) u32(s + 4) << 32);
                double d;
                std::memcpy(&d, &x, 8);
                v = (float) d;
            }
            w.data[ch][i] = v;
        }
    return w;
}

std::vector<float> readWavMono(const std::filesystem::path& path, double* sampleRate)
{
    auto w = readWav(path);
    if (sampleRate)
        *sampleRate = w.sampleRate;
    if (w.channels == 1)
        return std::move(w.data[0]);
    std::vector<float> m(w.frames(), 0.0f);
    for (const auto& ch : w.data)
        for (size_t i = 0; i < m.size(); ++i)
            m[i] += ch[i] / (float) w.channels;
    return m;
}

void writeWav(const std::filesystem::path& path, const std::vector<std::vector<float>>& channels, double sampleRate)
{
    if (channels.empty())
        throw std::runtime_error("writeWav: no channels");
    const size_t frames = channels[0].size();
    const uint16_t nch = (uint16_t) channels.size();
    std::ofstream f(path, std::ios::binary);
    if (!f)
        throw std::runtime_error("cannot write " + path.string());
    const uint32_t dataBytes = (uint32_t) (frames * nch * 4);
    f.write("RIFF", 4);
    put32(f, 36 + dataBytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    put32(f, 16);
    put16(f, 3);
    put16(f, nch);
    put32(f, (uint32_t) sampleRate);
    put32(f, (uint32_t) sampleRate * nch * 4);
    put16(f, (uint16_t) (nch * 4));
    put16(f, 32);
    f.write("data", 4);
    put32(f, dataBytes);
    for (size_t i = 0; i < frames; ++i)
        for (const auto& ch : channels)
        {
            float v = ch[i];
            uint32_t x;
            std::memcpy(&x, &v, 4);
            put32(f, x);
        }
}

void writeWavMono(const std::filesystem::path& path, const std::vector<float>& mono, double sampleRate)
{
    writeWav(path, { mono }, sampleRate);
}
} // namespace pnam
