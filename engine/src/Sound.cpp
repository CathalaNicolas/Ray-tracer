#include "Sound.hpp"

#include "EngineSettings.hpp"

#include <cmath>
#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#endif
#include <miniaudio.h>
#if defined(_MSC_VER)
#pragma warning(pop)
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

const char *clipName(GameSound sound)
{
    if (sound == GameSound::Pickup)
        return "game://pickup";
    if (sound == GameSound::Win)
        return "game://win";
    return "game://beep";
}

double gVolume = 1;
Vec3 gListener;

struct Audio
{
    ma_engine engine{};
    bool engineOk = false;
    ma_sound music{};
    bool musicOk = false;
    ma_sound oneshot{};
    bool oneshotOk = false;
    bool clipsRegistered = false;

    ~Audio()
    {
        releaseOneshot();
        releaseMusic();
        if (engineOk)
        {
            ma_engine_uninit(&engine);
            engineOk = false;
        }
    }

    bool ensureEngine()
    {
        if (engineOk)
            return true;
        if (ma_engine_init(nullptr, &engine) != MA_SUCCESS)
            return false;
        engineOk = true;
        registerClips();
        return true;
    }

    void registerClips()
    {
        if (clipsRegistered)
            return;
        ma_resource_manager *resources = ma_engine_get_resource_manager(&engine);
        if (resources == nullptr)
            return;
        const GameSound kinds[] = {GameSound::Beep, GameSound::Pickup, GameSound::Win};
        for (GameSound kind : kinds)
        {
            const std::vector<std::uint8_t> &wav = clipFor(kind);
            ma_resource_manager_register_encoded_data(resources, clipName(kind), wav.data(), wav.size());
        }
        clipsRegistered = true;
    }

    void releaseOneshot()
    {
        if (!oneshotOk)
            return;
        ma_sound_uninit(&oneshot);
        oneshotOk = false;
    }

    void releaseMusic()
    {
        if (!musicOk)
            return;
        ma_sound_uninit(&music);
        musicOk = false;
    }

    void applyMusicVolume()
    {
        if (!musicOk)
            return;
        ma_sound_set_volume(&music, static_cast<float>(gVolume));
    }
};

Audio &audio()
{
    static Audio instance;
    return instance;
}

}

void setMasterVolume(double volume)
{
    gVolume = volume < 0 ? 0 : (volume > 1 ? 1 : volume);
    audio().applyMusicVolume();
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
    if (gain <= 0.0001)
        return;
    Audio &device = audio();
    if (!device.ensureEngine())
        return;
    device.releaseOneshot();
    const ma_uint32 flags = MA_SOUND_FLAG_DECODE | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(&device.engine, clipName(sound), flags, nullptr, nullptr, &device.oneshot) != MA_SUCCESS)
        return;
    device.oneshotOk = true;
    ma_sound_set_volume(&device.oneshot, static_cast<float>(gain));
    ma_sound_start(&device.oneshot);
}

void startMusic()
{
    stopMusic();
    Audio &device = audio();
    if (!device.ensureEngine())
        return;
    const ma_uint32 flags = MA_SOUND_FLAG_STREAM | MA_SOUND_FLAG_NO_SPATIALIZATION;
    if (ma_sound_init_from_file(&device.engine, "assets/music.wav", flags, nullptr, nullptr, &device.music) != MA_SUCCESS)
        return;
    device.musicOk = true;
    ma_sound_set_looping(&device.music, MA_TRUE);
    device.applyMusicVolume();
    ma_sound_start(&device.music);
}

void stopMusic()
{
    audio().releaseMusic();
}
