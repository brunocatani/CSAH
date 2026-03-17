#pragma once
#include "PCH.h"

class Menu {
public:
    static Menu& GetSingleton();

    void Initialize(HWND hwnd, ID3D11Device* device, ID3D11DeviceContext* context);
    void Shutdown();
    void Draw();
    void Toggle();
    void ProcessWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    bool isVisible = false;
    bool initialized = false;

private:
    Menu() = default;
    void DrawFeatureList();
    void DrawShaderCacheStatus();
};
