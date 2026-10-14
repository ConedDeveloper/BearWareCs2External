#pragma once

#include "../memory/memory.h"
#include <d3d9.h>
#include <d3dx9.h>

#if !defined(D3DX_SDK_VERSION) && !defined(__D3DX9MATH_H__)
struct D3DXVECTOR2 { float x; float y; D3DXVECTOR2() {} D3DXVECTOR2(float _x, float _y) : x(_x), y(_y) {} };
#endif

#ifndef ID3DXLine
struct ID3DXLine;
#endif

#include "../math/vector.h"

namespace renderer
{
	inline HWND overlay_hwnd = nullptr;
	inline IDirect3D9* pD3D = nullptr;
	inline IDirect3DDevice9* pDevice = nullptr;
	inline ID3DXLine* mLine = nullptr;
	inline ID3DXFont* pFont = nullptr;
	inline ID3DXFont* pFontTitle = nullptr;
	inline ID3DXFont* pFontName = nullptr;
	inline ID3DXFont* pFontSmall = nullptr;
	inline ID3DXFont* pFontTab = nullptr;
	inline IDirect3DTexture9* pLogoTexture = nullptr;

	inline int screen_w = 1920;
	inline int screen_h = 1080;
	inline int game_x = 0;
	inline int game_y = 0;
	inline int game_w = 1920;
	inline int game_h = 1080;

	inline MSG msg;
	inline bool running = true;

	inline int mouse_x = 0;
	inline int mouse_y = 0;
	inline bool mouse_down = false;
	inline bool mouse_clicked = false;

	// Startup screen variables
	inline bool show_startup = true;
	inline DWORD startup_begin = 0;
	inline float startup_alpha = 0.0f;

	bool init(HWND hwnd);
	void destroy();
	void set_click_through(bool enabled); // kept for API; overlay stays click-through always
	void sync_to_game();
	void update_mouse();
	bool consume_click();
	bool present();

	void frame();
	void handle_events();

	namespace draw
	{
		void line(D3DXVECTOR2 p1, D3DXVECTOR2 p2, D3DCOLOR color, bool soft = false);
		void box(D3DXVECTOR2 tl, D3DXVECTOR2 br, D3DCOLOR color);
		void filled_box(float x, float y, float w, float h, D3DCOLOR color);
		void filled_circle(float cx, float cy, float radius, D3DCOLOR color, int segments = 16);
		void circle(float cx, float cy, float radius, D3DCOLOR color, int segments = 48, bool soft = false);
		void triangle_fan(const D3DXVECTOR2* pts, int count, D3DCOLOR color);
		void triangle_strip(const D3DXVECTOR2* pts, int count, D3DCOLOR color);
		void draw_text(const wchar_t* text, int x, int y, D3DCOLOR color);
		void draw_text_title(const wchar_t* text, int x, int y, D3DCOLOR color);
		void draw_text_name(const wchar_t* text, int x, int y, D3DCOLOR color);
		void draw_text_small(const wchar_t* text, int x, int y, D3DCOLOR color);
		void draw_text_tab(const wchar_t* text, int x, int y, D3DCOLOR color);
		void draw_texture(IDirect3DTexture9* texture, float x, float y, float w, float h, D3DCOLOR color = 0xFFFFFFFF);
		void draw_startup_screen();
	}
};

#define WINDOW_W (renderer::game_w)
#define WINDOW_H (renderer::game_h)

namespace window
{
	HWND InitWindow(HINSTANCE hInstance);
	LRESULT CALLBACK WinProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
}
