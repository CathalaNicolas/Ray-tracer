#pragma once

#include "Camera.hpp"
#include "Scene.hpp"

// Screen-space collider overlay for the view rectangle that just showed the frame.
// x0,y0 and x1,y1 are the image widget corners in ImGui screen pixels.
void drawColliders(const Scene &scene, const Camera &camera, int playerId, float x0, float y0, float x1, float y1);

// The Use key, centered on a world point in the view image.
void drawWorldPrompt(const Camera &camera, const Vec3 &point, const char *text, float x0, float y0, float x1, float y1);

// Light-to-mirror and mirror-to-receiver legs. Green is kept, red is rejected.
void drawBounceRays(const Scene &scene, const Camera &camera, float x0, float y0, float x1, float y1);

// mode: 0 move, 1 rotate, 2 scale. Returns 0, or 1/2/3 for the X/Y/Z handle under the mouse.
void drawGizmo(const Scene &scene, int objectId, int mode, const Camera &camera, float x0, float y0, float x1, float y1);
int pickGizmo(const Scene &scene, int objectId, int mode, const Camera &camera, float x0, float y0, float x1, float y1, float mouseX, float mouseY);
