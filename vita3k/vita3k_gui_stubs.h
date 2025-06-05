#pragma once
#ifdef VITA3K_NO_GUI

// ImGui type stubs - minimal definitions for compilation
struct ImVec4 { 
    float x, y, z, w; 
    ImVec4() : x(0), y(0), z(0), w(0) {}
    ImVec4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

struct ImVec2 { 
    float x, y; 
    ImVec2() : x(0), y(0) {}
    ImVec2(float _x, float _y) : x(_x), y(_y) {}
};

typedef void* ImTextureID;

// Stub out Dear ImGui functions if needed
#define IMGUI_API

#endif // VITA3K_NO_GUI