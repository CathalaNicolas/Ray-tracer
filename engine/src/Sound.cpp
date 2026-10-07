#include "Sound.hpp"

#include "EngineSettings.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>
#endif

namespace
{

constexpr int kRate = 22050;
constexpr int kHeader = 44;
constexpr int kPeak = 28000;

std::vector<std::uint8_t> makeTone(double hz, int count)
{
    std::vector<std::uint8_t> wav(static_cast<size_t>(kHeader + count * 2));
    auto storeU16 = [&](size_t offset, unsigned value) {
        wav[offset] = static_cast<std::uint8_t>(value & 0xff);
        wav[offset + 1] = static_cast<std::uint8_t>((value >> 8) & 0xff);
    };
    auto store32 = [&](size_t offset, std::uint32_t value) {
        storeU16(offset, value & 0xffff);
        storeU16(offset + 2, (value >> 16) & 0xffff);
    };
    auto store16 = [&](size_t offset, int value) {
        if (value > 32767)
            value = 32767;
        if (value < -32767)
            value = -32767;
        storeU16(offset, static_cast<unsigned>(static_cast<std::uint16_t>(static_cast<std::int16_t>(value))));
    };
    wav[0] = 'R';
    wav[1] = 'I';
    wav[2] = 'F';
    wav[3] = 'F';
    store32(4, static_cast<std::uint32_t>(wav.size() - 8));
    wav[8] = 'W';
    wav[9] = 'A';
    wav[10] = 'V';
    wav[11] = 'E';
    wav[12] = 'f';
    wav[13] = 'm';
    wav[14] = 't';
    wav[15] = ' ';
    store32(16, 16);
    store16(20, 1);
    store16(22, 1);
    store32(24, kRate);
    store32(28, kRate * 2);
    store16(32, 2);
    store16(34, 16);
    wav[36] = 'd';
    wav[37] = 'a';
    wav[38] = 't';
    wav[39] = 'a';
    store32(40, static_cast<std::uint32_t>(count * 2));
    for (int i = 0; i < count; ++i)
    {
        const double time = static_cast<double>(i) / kRate;
        const double envelope = 1.0 - static_cast<double>(i) / count;
        const double sample = std::sin(2.0 * 3.141592653589793 * hz * time) * envelope;
        store16(static_cast<size_t>(kHeader + i * 2), static_cast<int>(sample * kPeak));
    }
    return wav;
}

std::vector<std::uint8_t> loadWav(const char *path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() < 44)
        return {};
    if (bytes[0] != 'R' || bytes[1] != 'I' || bytes[2] != 'F' || bytes[3] != 'F')
        return {};
    return bytes;
}

const std::vector<std::uint8_t> &clipFor(GameSound sound)
{
    static const std::vector<std::uint8_t> beep = [] {
        auto loaded = loadWav("assets/beep.wav");
        return loaded.empty() ? makeTone(880.0, 1540) : loaded;
    }();
    static const std::vector<std::uint8_t> pickup = [] {
        auto loaded = loadWav("assets/pickup.wav");
        return loaded.empty() ? makeTone(1760.0, 882) : loaded;
    }();
    static const std::vector<std::uint8_t> win = [] {
        auto loaded = loadWav("assets/win.wav");
        return loaded.empty() ? makeTone(660.0, 2205) : loaded;
    }();
    if (sound == GameSound::Pickup)
        return pickup;
    if (sound == GameSound::Win)
        return win;
    return beep;
}

double gVolume = 1;
Vec3 gListener;
std::vector<std::uint8_t> gPlayed;

std::vector<std::uint8_t> scaled(const std::vector<std::uint8_t> &wav, double volume)
{
    if (wav.size() < 44 || volume >= 0.999)
        return wav;
    std::vector<std::uint8_t> out = wav;
    const double gain = volume < 0 ? 0 : volume;
    for (size_t offset = 44; offset + 1 < out.size(); offset += 2)
    {
        int sample = static_cast<int>(static_cast<std::int16_t>(out[offset] | (out[offset + 1] << 8)));
        sample = static_cast<int>(sample * gain);
        if (sample > 32767)
            sample = 32767;
        if (sample < -32767)
            sample = -32767;
        const auto bits = static_cast<std::uint16_t>(static_cast<std::int16_t>(sample));
        out[offset] = static_cast<std::uint8_t>(bits & 0xff);
        out[offset + 1] = static_cast<std::uint8_t>((bits >> 8) & 0xff);
    }
    return out;
}

}

bool gMusic = false;

void applyMusicVolume()
{
#if defined(_WIN32)
    if (!gMusic)
        return;
    const int level = static_cast<int>(gVolume * 1000.0);
    const std::string command = "setaudio raymusic volume to " + std::to_string(level);
    ::mciSendStringA(command.c_str(), nullptr, 0, nullptr);
#else
    (void)0;
#endif
}

void setMasterVolume(double volume)
{
    gVolume = volume < 0 ? 0 : (volume > 1 ? 1 : volume);
    applyMusicVolume();
}

double masterVolume()
{
    return gVolume;
}

void setSoundListener(const Vec3 &position)
{
    gListener = position;
}

double soundDistanceFade(double distance)
{
    if (distance <= 0)
        return 1;
    const double range = engineSettings().soundFar;
    if (range <= 0)
        return 0;
    double fade = 1.0 - distance / range;
    if (fade < 0)
        fade = 0;
    return fade;
}

void playGameSound(GameSound sound, const Vec3 &source)
{
    const double distance = length(source - gListener);
    const double gain = gVolume * soundDistanceFade(distance);
#if defined(_WIN32)
    if (gain <= 0.0001)
        return;
    // Stop first so the previous buffer can be freed before gPlayed reallocates.
    ::PlaySoundA(nullptr, nullptr, 0);
    gPlayed = scaled(clipFor(sound), gain);
    ::PlaySoundA(reinterpret_cast<LPCSTR>(gPlayed.data()), nullptr, SND_ASYNC | SND_MEMORY | SND_NODEFAULT);
#else
    (void)sound;
    (void)gain;
#endif
}

void startMusic()
{
#if defined(_WIN32)
    stopMusic();
    if (::mciSendStringA("open \"assets/music.wav\" type waveaudio alias raymusic", nullptr, 0, nullptr) != 0)
        return;
    gMusic = true;
    applyMusicVolume();
    ::mciSendStringA("play raymusic repeat", nullptr, 0, nullptr);
#else
    (void)0;
#endif
}

void stopMusic()
{
#if defined(_WIN32)
    if (!gMusic)
        return;
    ::mciSendStringA("stop raymusic", nullptr, 0, nullptr);
    ::mciSendStringA("close raymusic", nullptr, 0, nullptr);
    gMusic = false;
#else
    (void)0;
#endif
}
