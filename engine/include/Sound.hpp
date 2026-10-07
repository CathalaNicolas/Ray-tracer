#pragma once

#include "Vec3.hpp"

enum class GameSound
{
    Beep,
    Pickup,
    Win,
};

void setMasterVolume(double volume);
double masterVolume();
void setSoundListener(const Vec3 &position);
// 1 at the listener, 0 at soundFar and beyond. Used by playGameSound and self-test.
double soundDistanceFade(double distance);
void playGameSound(GameSound sound, const Vec3 &source);
void startMusic();
void stopMusic();
