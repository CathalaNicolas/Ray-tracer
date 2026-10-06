# Build, platform, and content

The Windows build uses MSVC from Visual Studio Build Tools 2022 and the Autodesk FBX SDK 2020.3.11. The Makefile calls `vcvars64.bat`, then `cl` and `link`. Flags are `/std:c++17 /EHsc /O2 /MD /bigobj`. Engine and editor translation units use `/W4`. `third_party` and the stb image wrappers stay at `/W0`.

Object files go in `dist/`. `raytracer.exe` stays in the project root so it still finds `assets/`. `build.bat` in the project root runs that build. Double-click it, or run it from a terminal. It pauses at the end so the result stays on screen.

Runtime code lives in `engine/include` and `engine/src`. The window, ImGui panels, and collider overlay live in `editor/include` and `editor/src`. `editor/src/main.cpp` is the process entry. Both trees are on the compiler include path, and sources still include headers by file name.

The link line includes `user32`, `gdi32`, `opengl32`, `dwmapi`, `comdlg32`, `advapi32`, `bcrypt`, `winmm`, and the FBX SDK static libraries `libfbxsdk-md`, `libxml2-md`, and `zlib-md`. OpenGL is 3.3. The shader is GLSL 330.

The non-Windows Makefile branch is still g++. Its `OBJS` list is stale. `Mesh.cpp` is not compiled there because it includes the FBX SDK. The `GpuRayTracer` stub matches the current `render` signature so that file alone compiles; a full non-Windows link is not maintained.

Assets the demo needs sit in `assets/`: `tree-trunk.obj` and `tree-crown.obj`. Paths in a scene file are stored as written. Moving the exe without those files, or without the FBX runtime pieces the SDK requires, is not a supported package.

## Not built

- A folder a friend can run without this repo.
- Asset paths rewritten for that folder.
- A maintained build for another OS.
- An installer or a zip of the exe, assets, and runtime libraries.
- A content-license note for third-party meshes and the FBX SDK.
