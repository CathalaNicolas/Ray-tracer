# Windows builds with the MSVC compiler from Visual Studio Build Tools.
# FBX SDK 2020.3.11 is the VS2022 install under Program Files.
# Short paths keep the recipes free of spaces:
#   C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools
#   C:/Program Files/Autodesk/FBX/FBX SDK/2020.3.11

ifeq ($(OS),Windows_NT)
  VCVARS = C:/PROGRA~2/MICROS~2/2022/BUILDT~1/VC/AUXILI~1/Build/vcvars64.bat
  FBX = C:/PROGRA~1/Autodesk/FBX/FBXSDK~1/20203~1.11
  CL = cmd /C "call $(VCVARS) >nul && cl /nologo /std:c++17 /EHsc /O2 /MD /bigobj /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS /Iengine/include /Ieditor/include /Ithird_party /Ithird_party/imgui /Ithird_party/imgui/backends /I$(FBX)/include"
  EXE = raytracer.exe
  RM = del /Q
  OBJS = dist/main.obj dist/RayTracer.obj dist/self_test.obj dist/image_write.obj dist/image_read.obj dist/editor.obj dist/EditorPlay.obj dist/EditorScene.obj dist/EditorHistory.obj dist/EditorUi.obj dist/EditorWidgets.obj dist/GpuGl.obj dist/GpuShaders.obj dist/GpuShaderTrace.obj dist/GpuRaster.obj dist/GpuRayTracer.obj dist/GpuRender.obj dist/SceneFile.obj dist/SceneLoad.obj dist/SceneWrite.obj dist/Prefab.obj dist/Mesh.obj dist/Sphere.obj dist/Plane.obj dist/Play.obj dist/PlayTests.obj dist/Collision.obj dist/Sound.obj dist/EngineSettings.obj dist/Role.obj dist/DebugDraw.obj dist/SceneDebug.obj dist/imgui.obj dist/imgui_draw.obj dist/imgui_tables.obj dist/imgui_widgets.obj dist/imgui_impl_win32.obj dist/imgui_impl_opengl3.obj
  LIBS = user32.lib gdi32.lib opengl32.lib dwmapi.lib comdlg32.lib advapi32.lib bcrypt.lib winmm.lib $(FBX)/lib/x64/release/libfbxsdk-md.lib $(FBX)/lib/x64/release/libxml2-md.lib $(FBX)/lib/x64/release/zlib-md.lib
else
  CXX = g++
  CXXFLAGS = -std=c++17 -Wall -Wextra -O2 -pthread -Iengine/include -Ieditor/include -Ithird_party -Ithird_party/imgui -Ithird_party/imgui/backends
  EXE = raytracer
  RM = rm -f
  OBJS = dist/main.o dist/RayTracer.o dist/self_test.o dist/image_write.o dist/image_read.o dist/editor.o dist/GpuGl.o dist/GpuShaders.o dist/GpuRaster.o dist/GpuRayTracer.o dist/SceneFile.o dist/Mesh.o dist/Play.o dist/Collision.o dist/imgui.o dist/imgui_draw.o dist/imgui_tables.o dist/imgui_widgets.o dist/imgui_impl_win32.o dist/imgui_impl_opengl3.o
  LIBS =
endif

.PHONY: all clean

all: $(EXE)

ifeq ($(OS),Windows_NT)

$(OBJS): | dist

dist:
	cmd /C "if not exist dist mkdir dist"

$(EXE): $(OBJS)
	cmd /C "call $(VCVARS) >nul && link /nologo /OUT:$(EXE) $(OBJS) $(LIBS)"

dist/main.obj: editor/src/main.cpp engine/include/Camera.hpp engine/include/Material.hpp engine/include/DemoScene.hpp
	$(CL) /W4 /c editor/src/main.cpp /Fodist/main.obj

dist/RayTracer.obj: engine/src/RayTracer.cpp engine/include/GpuRayTracer.hpp engine/include/RayTracer.hpp engine/include/Camera.hpp engine/include/Material.hpp
	$(CL) /W4 /c engine/src/RayTracer.cpp /Fodist/RayTracer.obj

dist/self_test.obj: engine/src/self_test.cpp engine/include/Material.hpp engine/include/Camera.hpp engine/include/Mesh.hpp engine/include/DemoScene.hpp engine/include/SceneParse.hpp
	$(CL) /W4 /c engine/src/self_test.cpp /Fodist/self_test.obj

dist/editor.obj: editor/src/editor.cpp editor/include/EditorInternal.hpp engine/include/GpuRayTracer.hpp editor/include/Editor.hpp engine/include/Material.hpp engine/include/Camera.hpp engine/include/Mesh.hpp engine/include/DemoScene.hpp engine/include/Sound.hpp editor/include/DebugDraw.hpp
	$(CL) /W4 /c editor/src/editor.cpp /Fodist/editor.obj

dist/EditorPlay.obj: editor/src/EditorPlay.cpp editor/include/EditorInternal.hpp
	$(CL) /W4 /c editor/src/EditorPlay.cpp /Fodist/EditorPlay.obj

dist/EditorScene.obj: editor/src/EditorScene.cpp editor/include/EditorInternal.hpp
	$(CL) /W4 /c editor/src/EditorScene.cpp /Fodist/EditorScene.obj

dist/EditorHistory.obj: editor/src/EditorHistory.cpp editor/include/EditorInternal.hpp
	$(CL) /W4 /c editor/src/EditorHistory.cpp /Fodist/EditorHistory.obj

dist/EditorUi.obj: editor/src/EditorUi.cpp editor/include/EditorInternal.hpp
	$(CL) /W4 /c editor/src/EditorUi.cpp /Fodist/EditorUi.obj

dist/EditorWidgets.obj: editor/src/EditorWidgets.cpp editor/include/EditorInternal.hpp
	$(CL) /W4 /c editor/src/EditorWidgets.cpp /Fodist/EditorWidgets.obj

dist/GpuGl.obj: engine/src/GpuGl.cpp engine/src/GpuGl.hpp
	$(CL) /W4 /c engine/src/GpuGl.cpp /Fodist/GpuGl.obj

dist/GpuShaders.obj: engine/src/GpuShaders.cpp engine/src/GpuShaders.hpp engine/include/GpuLimits.hpp
	$(CL) /W4 /c engine/src/GpuShaders.cpp /Fodist/GpuShaders.obj

dist/GpuShaderTrace.obj: engine/src/GpuShaderTrace.cpp engine/src/GpuShaders.hpp
	$(CL) /W4 /c engine/src/GpuShaderTrace.cpp /Fodist/GpuShaderTrace.obj

dist/GpuRaster.obj: engine/src/GpuRaster.cpp engine/src/GpuGl.hpp engine/include/GpuRayTracer.hpp engine/include/Camera.hpp
	$(CL) /W4 /c engine/src/GpuRaster.cpp /Fodist/GpuRaster.obj

dist/GpuRayTracer.obj: engine/src/GpuRayTracer.cpp engine/src/GpuGl.hpp engine/src/GpuShaders.hpp engine/include/GpuRayTracer.hpp engine/include/Material.hpp engine/include/Camera.hpp engine/include/Mesh.hpp
	$(CL) /W4 /c engine/src/GpuRayTracer.cpp /Fodist/GpuRayTracer.obj

dist/GpuRender.obj: engine/src/GpuRender.cpp engine/include/GpuRayTracer.hpp engine/src/GpuGl.hpp engine/src/GpuRenderDetail.hpp engine/src/GpuShaders.hpp
	$(CL) /W4 /c engine/src/GpuRender.cpp /Fodist/GpuRender.obj

dist/SceneFile.obj: engine/src/SceneFile.cpp engine/include/Material.hpp engine/include/Mesh.hpp engine/include/Camera.hpp
	$(CL) /W4 /c engine/src/SceneFile.cpp /Fodist/SceneFile.obj

dist/SceneLoad.obj: engine/src/SceneLoad.cpp engine/include/SceneFile.hpp engine/include/SceneParse.hpp
	$(CL) /W4 /c engine/src/SceneLoad.cpp /Fodist/SceneLoad.obj

dist/Prefab.obj: engine/src/Prefab.cpp engine/include/SceneFile.hpp
	$(CL) /W4 /c engine/src/Prefab.cpp /Fodist/Prefab.obj

dist/image_write.obj: engine/src/image_write.cpp
	$(CL) /W0 /c engine/src/image_write.cpp /Fodist/image_write.obj

dist/image_read.obj: engine/src/image_read.cpp
	$(CL) /W0 /c engine/src/image_read.cpp /Fodist/image_read.obj

dist/Mesh.obj: engine/src/Mesh.cpp engine/include/Mesh.hpp engine/include/Material.hpp
	$(CL) /W4 /c engine/src/Mesh.cpp /Fodist/Mesh.obj

dist/Sphere.obj: engine/src/Sphere.cpp engine/include/Sphere.hpp engine/include/SceneWrite.hpp engine/include/GpuContribute.hpp
	$(CL) /W4 /c engine/src/Sphere.cpp /Fodist/Sphere.obj

dist/Plane.obj: engine/src/Plane.cpp engine/include/Plane.hpp engine/include/SceneWrite.hpp engine/include/GpuContribute.hpp
	$(CL) /W4 /c engine/src/Plane.cpp /Fodist/Plane.obj

dist/SceneWrite.obj: engine/src/SceneWrite.cpp engine/include/SceneWrite.hpp
	$(CL) /W4 /c engine/src/SceneWrite.cpp /Fodist/SceneWrite.obj

dist/Play.obj: engine/src/Play.cpp engine/include/Play.hpp engine/include/PlayDetail.hpp engine/include/Collision.hpp engine/include/EngineSettings.hpp
	$(CL) /W4 /c engine/src/Play.cpp /Fodist/Play.obj

dist/PlayTests.obj: engine/src/PlayTests.cpp engine/include/Play.hpp engine/include/PlayDetail.hpp
	$(CL) /W4 /c engine/src/PlayTests.cpp /Fodist/PlayTests.obj

dist/Collision.obj: engine/src/Collision.cpp engine/include/Collision.hpp
	$(CL) /W4 /c engine/src/Collision.cpp /Fodist/Collision.obj

dist/Sound.obj: engine/src/Sound.cpp engine/include/Sound.hpp engine/include/EngineSettings.hpp
	$(CL) /W4 /c engine/src/Sound.cpp /Fodist/Sound.obj

dist/EngineSettings.obj: engine/src/EngineSettings.cpp engine/include/EngineSettings.hpp
	$(CL) /W4 /c engine/src/EngineSettings.cpp /Fodist/EngineSettings.obj

dist/Role.obj: engine/src/Role.cpp engine/include/Role.hpp
	$(CL) /W4 /c engine/src/Role.cpp /Fodist/Role.obj

dist/SceneDebug.obj: engine/src/SceneDebug.cpp engine/include/SceneDebug.hpp engine/src/GpuRenderDetail.hpp
	$(CL) /W4 /c engine/src/SceneDebug.cpp /Fodist/SceneDebug.obj

dist/DebugDraw.obj: editor/src/DebugDraw.cpp editor/include/DebugDraw.hpp engine/include/PlayDetail.hpp
	$(CL) /W4 /c editor/src/DebugDraw.cpp /Fodist/DebugDraw.obj

dist/imgui.obj: third_party/imgui/imgui.cpp
	$(CL) /W0 /c third_party/imgui/imgui.cpp /Fodist/imgui.obj

dist/imgui_draw.obj: third_party/imgui/imgui_draw.cpp
	$(CL) /W0 /c third_party/imgui/imgui_draw.cpp /Fodist/imgui_draw.obj

dist/imgui_tables.obj: third_party/imgui/imgui_tables.cpp
	$(CL) /W0 /c third_party/imgui/imgui_tables.cpp /Fodist/imgui_tables.obj

dist/imgui_widgets.obj: third_party/imgui/imgui_widgets.cpp
	$(CL) /W0 /c third_party/imgui/imgui_widgets.cpp /Fodist/imgui_widgets.obj

dist/imgui_impl_win32.obj: third_party/imgui/backends/imgui_impl_win32.cpp
	$(CL) /W0 /c third_party/imgui/backends/imgui_impl_win32.cpp /Fodist/imgui_impl_win32.obj

dist/imgui_impl_opengl3.obj: third_party/imgui/backends/imgui_impl_opengl3.cpp
	$(CL) /W0 /c third_party/imgui/backends/imgui_impl_opengl3.cpp /Fodist/imgui_impl_opengl3.obj

else

$(OBJS): | dist

dist:
	mkdir -p dist

$(EXE): $(OBJS)
	$(CXX) $(OBJS) -o $(EXE) -pthread $(LIBS)

dist/main.o: editor/src/main.cpp engine/include/Camera.hpp engine/include/Material.hpp engine/include/DemoScene.hpp
	$(CXX) $(CXXFLAGS) -c editor/src/main.cpp -o dist/main.o

dist/RayTracer.o: engine/src/RayTracer.cpp engine/include/GpuRayTracer.hpp engine/include/RayTracer.hpp engine/include/Camera.hpp engine/include/Material.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/RayTracer.cpp -o dist/RayTracer.o

dist/self_test.o: engine/src/self_test.cpp engine/include/Material.hpp engine/include/Camera.hpp engine/include/Mesh.hpp engine/include/DemoScene.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/self_test.cpp -o dist/self_test.o

dist/editor.o: editor/src/editor.cpp engine/include/GpuRayTracer.hpp editor/include/Editor.hpp engine/include/Material.hpp engine/include/Camera.hpp engine/include/Mesh.hpp engine/include/DemoScene.hpp
	$(CXX) $(CXXFLAGS) -c editor/src/editor.cpp -o dist/editor.o

dist/GpuGl.o: engine/src/GpuGl.cpp engine/src/GpuGl.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/GpuGl.cpp -o dist/GpuGl.o

dist/GpuShaders.o: engine/src/GpuShaders.cpp engine/src/GpuShaders.hpp engine/include/GpuLimits.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/GpuShaders.cpp -o dist/GpuShaders.o

dist/GpuRaster.o: engine/src/GpuRaster.cpp engine/src/GpuGl.hpp engine/include/GpuRayTracer.hpp engine/include/Camera.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/GpuRaster.cpp -o dist/GpuRaster.o

dist/GpuRayTracer.o: engine/src/GpuRayTracer.cpp engine/src/GpuGl.hpp engine/src/GpuShaders.hpp engine/include/GpuRayTracer.hpp engine/include/Material.hpp engine/include/Camera.hpp engine/include/Mesh.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/GpuRayTracer.cpp -o dist/GpuRayTracer.o

dist/SceneFile.o: engine/src/SceneFile.cpp engine/include/Material.hpp engine/include/Mesh.hpp engine/include/Camera.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/SceneFile.cpp -o dist/SceneFile.o

dist/image_write.o: engine/src/image_write.cpp
	$(CXX) $(CXXFLAGS) -w -c engine/src/image_write.cpp -o dist/image_write.o

dist/image_read.o: engine/src/image_read.cpp
	$(CXX) $(CXXFLAGS) -w -c engine/src/image_read.cpp -o dist/image_read.o

dist/Mesh.o: engine/src/Mesh.cpp engine/include/Mesh.hpp engine/include/Material.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/Mesh.cpp -o dist/Mesh.o

dist/Play.o: engine/src/Play.cpp engine/include/Play.hpp engine/include/PlayDetail.hpp engine/include/Collision.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/Play.cpp -o dist/Play.o

dist/Collision.o: engine/src/Collision.cpp engine/include/Collision.hpp
	$(CXX) $(CXXFLAGS) -c engine/src/Collision.cpp -o dist/Collision.o

dist/imgui.o: third_party/imgui/imgui.cpp
	$(CXX) $(CXXFLAGS) -w -c third_party/imgui/imgui.cpp -o dist/imgui.o

dist/imgui_draw.o: third_party/imgui/imgui_draw.cpp
	$(CXX) $(CXXFLAGS) -w -c third_party/imgui/imgui_draw.cpp -o dist/imgui_draw.o

dist/imgui_tables.o: third_party/imgui/imgui_tables.cpp
	$(CXX) $(CXXFLAGS) -w -c third_party/imgui/imgui_tables.cpp -o dist/imgui_tables.o

dist/imgui_widgets.o: third_party/imgui/imgui_widgets.cpp
	$(CXX) $(CXXFLAGS) -w -c third_party/imgui/imgui_widgets.cpp -o dist/imgui_widgets.o

dist/imgui_impl_win32.o: third_party/imgui/backends/imgui_impl_win32.cpp
	$(CXX) $(CXXFLAGS) -w -c third_party/imgui/backends/imgui_impl_win32.cpp -o dist/imgui_impl_win32.o

dist/imgui_impl_opengl3.o: third_party/imgui/backends/imgui_impl_opengl3.cpp
	$(CXX) $(CXXFLAGS) -w -c third_party/imgui/backends/imgui_impl_opengl3.cpp -o dist/imgui_impl_opengl3.o

endif

clean:
ifeq ($(OS),Windows_NT)
	cmd /C "if exist dist rmdir /S /Q dist"
else
	rm -rf dist
endif
