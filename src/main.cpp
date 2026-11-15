#include <Windows.h>

#include <exception>

#include "App.h"

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nShow) {
    try {
        App app(hInst);
        return app.Run(nShow);
    } catch (const std::exception& e) {
        MessageBoxA(nullptr, e.what(), "curvtex - Error", MB_ICONERROR | MB_OK);
        return 1;
    }
}
