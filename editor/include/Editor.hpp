#pragma once

int runEditor(int width, int height, int samples, int depth, bool gameMode = false);

// Diligent clustered-forward primary view (SDL window, no OpenGL on the main HWND).
// Legacy Win32+GL editor remains for still/self-test hosts when RAYTRACER_DILIGENT is off.
int runEditorDiligent(int width, int height);
