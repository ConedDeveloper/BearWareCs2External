#include "renderer/renderer.h"
#include "cheats/esp.h"
#include "cheats/config.h"
#include <thread>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
    LPSTR lpCmdLine, int nCmdShow)
{
    esp::pID = memory::GetProcID(L"cs2.exe");
    esp::modBase = memory::GetModuleBaseAddress(esp::pID, L"client.dll");

    // Restore last saved settings before the overlay starts
    config::load_last();

    HWND hwnd = window::InitWindow(hInstance);
    if (!hwnd) return -1;

    if (!renderer::init(hwnd))
    {
        renderer::destroy();
        return -1;
    }

    std::thread read(esp::loop);

    while (!GetAsyncKeyState(esp::key_exit) && renderer::running)
    {
        esp::frame();
    }

    // Remember settings for next launch
    config::save_last();

    renderer::running = false;
    if (read.joinable())
        read.join();

    renderer::destroy();
    UnregisterClassW(L"BearWareOverlay", hInstance);

    return 0;
}
