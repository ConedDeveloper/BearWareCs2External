#include "renderer.h"
#include <cmath>
#include <string>

struct OverlayVertex
{
	float x, y, z, rhw;
	D3DCOLOR color;
};

LRESULT CALLBACK window::WinProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
	case WM_CLOSE:
		renderer::running = false;
		return 0;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
}

HWND window::InitWindow(HINSTANCE hInstance)
{
	SetProcessDPIAware();

	renderer::screen_w = GetSystemMetrics(SM_CXSCREEN);
	renderer::screen_h = GetSystemMetrics(SM_CYSCREEN);
	renderer::game_w = renderer::screen_w;
	renderer::game_h = renderer::screen_h;

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(WNDCLASSEXW);
	wc.lpfnWndProc = WinProc;
	wc.hInstance = hInstance;
	wc.lpszClassName = L"BearWareOverlay";
	wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

	if (!RegisterClassExW(&wc))
		return nullptr;

	// Always WS_EX_TRANSPARENT — ESP/FOV/footsteps must NEVER steal mouse from CS2.
	// Menu clicks use GetAsyncKeyState + cursor position instead.
	HWND hwnd = CreateWindowExW(
		WS_EX_TOPMOST | WS_EX_TRANSPARENT | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
		L"BearWareOverlay",
		L"BearWare",
		WS_POPUP,
		0, 0, renderer::screen_w, renderer::screen_h,
		nullptr, nullptr, hInstance, nullptr);

	if (!hwnd)
		return nullptr;

	SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
	ShowWindow(hwnd, SW_SHOWNOACTIVATE);
	return hwnd;
}

bool renderer::init(HWND hwnd)
{
	overlay_hwnd = hwnd;
	pD3D = Direct3DCreate9(D3D_SDK_VERSION);
	if (!pD3D)
		return false;

	D3DPRESENT_PARAMETERS d3dpp = {};
	d3dpp.BackBufferWidth = screen_w;
	d3dpp.BackBufferHeight = screen_h;
	d3dpp.BackBufferCount = 1;
	d3dpp.BackBufferFormat = D3DFMT_X8R8G8B8;
	d3dpp.SwapEffect = D3DSWAPEFFECT_DISCARD;
	d3dpp.Windowed = TRUE;
	d3dpp.hDeviceWindow = hwnd;
	d3dpp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;

	HRESULT hr = pD3D->CreateDevice(
		D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
		D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_MULTITHREADED | D3DCREATE_FPU_PRESERVE,
		&d3dpp, &pDevice);
	if (FAILED(hr))
	{
		if (FAILED(pD3D->CreateDevice(
			D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
			D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,
			&d3dpp, &pDevice)))
			return false;
	}

	if (FAILED(D3DXCreateLine(pDevice, &mLine)))
		return false;
	mLine->SetAntialias(FALSE);
	mLine->SetWidth(1.5f);

	auto make_font = [&](ID3DXFont** out, int size, int weight, const wchar_t* face) -> bool {
		return SUCCEEDED(D3DXCreateFontW(pDevice, size, 0, weight, 1, FALSE, DEFAULT_CHARSET,
			OUT_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, face, out));
	};

	// Prefer distinctive faces; fall back gracefully
	if (!make_font(&pFontTitle, 22, FW_BOLD, L"Bahnschrift") && !make_font(&pFontTitle, 22, FW_BOLD, L"Segoe UI"))
		return false;
	if (!make_font(&pFontTab, 13, FW_SEMIBOLD, L"Bahnschrift") && !make_font(&pFontTab, 13, FW_SEMIBOLD, L"Segoe UI"))
		return false;
	if (!make_font(&pFont, 13, FW_NORMAL, L"Candara") && !make_font(&pFont, 13, FW_NORMAL, L"Segoe UI"))
		return false;
	if (!make_font(&pFontName, 15, FW_SEMIBOLD, L"Bahnschrift") && !make_font(&pFontName, 15, FW_SEMIBOLD, L"Segoe UI"))
		return false;
	if (!make_font(&pFontSmall, 11, FW_NORMAL, L"Candara") && !make_font(&pFontSmall, 11, FW_NORMAL, L"Segoe UI"))
		return false;

	// Load bear logo texture - try multiple formats and locations
	pLogoTexture = nullptr;
	
	// Get executable directory
	wchar_t logo_path[MAX_PATH];
	GetModuleFileNameW(nullptr, logo_path, (DWORD)MAX_PATH);
	std::wstring exe_dir(logo_path);
	size_t slash = exe_dir.find_last_of(L"\\/");
	if (slash != std::wstring::npos) {
		exe_dir = exe_dir.substr(0, slash);
	}

	// Try different file formats and locations
	std::wstring paths[] = {
		exe_dir + L"\\bear_logo.png",      // PNG in exe directory (most compatible)
		exe_dir + L"\\bear_logo.ico",      // ICO in exe directory  
		exe_dir + L"\\bear_logo.bmp",      // BMP in exe directory
		L".\\bear_logo.png",               // PNG in current directory
		L".\\bear_logo.ico",               // ICO in current directory
		L".\\bear_logo.bmp"                // BMP in current directory
	};

	// Try loading each path until one works
	for (int i = 0; i < 6; i++) {
		hr = D3DXCreateTextureFromFileW(pDevice, paths[i].c_str(), &pLogoTexture);
		if (SUCCEEDED(hr) && pLogoTexture) {
			break; // Successfully loaded
		}
	}

	// If no texture loaded, create a simple bear-like logo programmatically
	if (!pLogoTexture) {
		// Create a 64x64 texture with a simple bear design
		hr = pDevice->CreateTexture(64, 64, 1, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pLogoTexture, nullptr);
		if (SUCCEEDED(hr)) {
			D3DLOCKED_RECT lockedRect;
			hr = pLogoTexture->LockRect(0, &lockedRect, nullptr, D3DLOCK_DISCARD);
			if (SUCCEEDED(hr)) {
				DWORD* pixels = (DWORD*)lockedRect.pBits;
				
				// Create a simple bear face design
				for (int y = 0; y < 64; y++) {
					for (int x = 0; x < 64; x++) {
						int dx = x - 32;
						int dy = y - 32;
						int dist = dx * dx + dy * dy;
						
						DWORD color = 0x00000000; // Transparent
						
						// Main head circle
						if (dist < 26 * 26) {
							color = 0xFFFF8C42; // Bright burnt orange accent color
						}
						// Ears
						else if (((dx + 15) * (dx + 15) + (dy + 20) * (dy + 20) < 8 * 8) ||
								 ((dx - 15) * (dx - 15) + (dy + 20) * (dy + 20) < 8 * 8)) {
							color = 0xFFFF8C42; // Bright burnt orange ears
						}
						// Eyes
						else if (((dx + 8) * (dx + 8) + (dy - 5) * (dy - 5) < 3 * 3) ||
								 ((dx - 8) * (dx - 8) + (dy - 5) * (dy - 5) < 3 * 3)) {
							color = 0xFF000000; // Black eyes
						}
						// Nose
						else if (dx * dx + (dy + 8) * (dy + 8) < 2 * 2) {
							color = 0xFF000000; // Black nose
						}
						
						pixels[y * (lockedRect.Pitch / 4) + x] = color;
					}
				}
				pLogoTexture->UnlockRect(0);
			}
		}
	}

	// Initialize startup screen
	startup_begin = GetTickCount();
	show_startup = true;
	startup_alpha = 0.0f;

	return true;
}

void renderer::set_click_through(bool /*enabled*/)
{
	// no-op — styles set once at window creation. Calling SetWindowLong every frame lags the game.
}

void renderer::sync_to_game()
{
	static HWND cached_game = nullptr;
	static DWORD last_find = 0;
	const DWORD now = GetTickCount();
	if (!cached_game || (now - last_find) > 1000)
	{
		cached_game = FindWindowW(nullptr, L"Counter-Strike 2");
		last_find = now;
	}

	HWND game = cached_game;
	if (!game)
	{
		game_x = 0; game_y = 0; game_w = screen_w; game_h = screen_h;
		return;
	}

	RECT client{};
	if (!GetClientRect(game, &client))
		return;

	POINT tl{ 0, 0 };
	ClientToScreen(game, &tl);
	const int w = client.right - client.left;
	const int h = client.bottom - client.top;
	if (w < 64 || h < 64)
		return;

	game_x = tl.x;
	game_y = tl.y;
	game_w = w;
	game_h = h;
}

void renderer::update_mouse()
{
	POINT p{};
	GetCursorPos(&p);
	if (overlay_hwnd)
		ScreenToClient(overlay_hwnd, &p);
	mouse_x = p.x;
	mouse_y = p.y;

	const bool down = (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0;
	static bool prev_down = false;
	if (down && !prev_down)
		mouse_clicked = true;
	mouse_down = down;
	prev_down = down;
}

bool renderer::consume_click()
{
	if (!mouse_clicked) return false;
	mouse_clicked = false;
	return true;
}

bool renderer::present()
{
	if (!pDevice) return false;
	HRESULT hr = pDevice->Present(nullptr, nullptr, nullptr, nullptr);
	if (hr == D3DERR_DEVICELOST)
	{
		Sleep(30);
		return false;
	}
	return SUCCEEDED(hr);
}

void renderer::frame()
{
	handle_events();
	update_mouse();
}

void renderer::destroy()
{
	if (pLogoTexture) { pLogoTexture->Release(); pLogoTexture = nullptr; }
	if (pFontTab) { pFontTab->Release(); pFontTab = nullptr; }
	if (pFontSmall) { pFontSmall->Release(); pFontSmall = nullptr; }
	if (pFontName) { pFontName->Release(); pFontName = nullptr; }
	if (pFontTitle) { pFontTitle->Release(); pFontTitle = nullptr; }
	if (pFont) { pFont->Release(); pFont = nullptr; }
	if (mLine) { mLine->Release(); mLine = nullptr; }
	if (pDevice) { pDevice->Release(); pDevice = nullptr; }
	if (pD3D) { pD3D->Release(); pD3D = nullptr; }
}

void renderer::draw::line(D3DXVECTOR2 p1, D3DXVECTOR2 p2, D3DCOLOR color, bool soft)
{
	if (!mLine || !pDevice) return;
	if (!std::isfinite(p1.x) || !std::isfinite(p1.y) || !std::isfinite(p2.x) || !std::isfinite(p2.y))
		return;
	mLine->SetAntialias(soft ? TRUE : FALSE);
	const D3DXVECTOR2 list[] = { p1, p2 };
	mLine->Begin();
	mLine->Draw(list, 2, color);
	mLine->End();
	mLine->SetAntialias(FALSE);
}

void renderer::draw::box(D3DXVECTOR2 tl, D3DXVECTOR2 br, D3DCOLOR color)
{
	if (!mLine || !pDevice) return;
	const D3DXVECTOR2 list[] = {
		tl, D3DXVECTOR2(tl.x, br.y), br, D3DXVECTOR2(br.x, tl.y), tl
	};
	mLine->SetAntialias(FALSE);
	mLine->Begin();
	mLine->Draw(list, 5, color);
	mLine->End();
}

void renderer::draw::filled_box(float x, float y, float w, float h, D3DCOLOR color)
{
	if (!pDevice) return;
	if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h))
		return;
	if (w <= 0.f || h <= 0.f) return;

	x = floorf(x); y = floorf(y); w = ceilf(w); h = ceilf(h);
	BYTE a = (BYTE)((color >> 24) & 0xFF);
	BYTE r = (BYTE)((color >> 16) & 0xFF);
	BYTE g = (BYTE)((color >> 8) & 0xFF);
	BYTE b = (BYTE)(color & 0xFF);
	if (r == 0 && g == 0 && b == 0)
		color = D3DCOLOR_ARGB(a ? a : 255, 1, 1, 1);

	OverlayVertex v[4] = {
		{ x, y, 0.f, 1.f, color },
		{ x + w, y, 0.f, 1.f, color },
		{ x, y + h, 0.f, 1.f, color },
		{ x + w, y + h, 0.f, 1.f, color },
	};

	DWORD oldFVF = 0;
	pDevice->GetFVF(&oldFVF);
	pDevice->SetTexture(0, nullptr);
	const bool use_alpha = a < 255;
	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, use_alpha ? TRUE : FALSE);
	if (use_alpha)
	{
		pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
		pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	}
	pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
	pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(OverlayVertex));
	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	pDevice->SetFVF(oldFVF);
}

void renderer::draw::filled_circle(float cx, float cy, float radius, D3DCOLOR color, int segments)
{
	if (!pDevice) return;
	if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(radius) || radius <= 0.f)
		return;
	if (segments < 8) segments = 8;
	if (segments > 32) segments = 32;

	BYTE a = (BYTE)((color >> 24) & 0xFF);
	BYTE r = (BYTE)((color >> 16) & 0xFF);
	BYTE g = (BYTE)((color >> 8) & 0xFF);
	BYTE b = (BYTE)(color & 0xFF);
	if (r == 0 && g == 0 && b == 0)
		color = D3DCOLOR_ARGB(a ? a : 255, 1, 1, 1);

	OverlayVertex v[34];
	v[0] = { cx, cy, 0.f, 1.f, color };
	for (int i = 0; i <= segments; ++i)
	{
		float ang = (i / (float)segments) * 6.2831853f;
		v[i + 1] = { cx + cosf(ang) * radius, cy + sinf(ang) * radius, 0.f, 1.f, color };
	}

	DWORD oldFVF = 0;
	pDevice->GetFVF(&oldFVF);
	pDevice->SetTexture(0, nullptr);
	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
	pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, segments, v, sizeof(OverlayVertex));
	pDevice->SetFVF(oldFVF);
}

void renderer::draw::circle(float cx, float cy, float radius, D3DCOLOR color, int segments, bool soft)
{
	if (!mLine || !pDevice) return;
	if (!std::isfinite(cx) || !std::isfinite(cy) || !std::isfinite(radius) || radius <= 0.f)
		return;
	if (segments < 8) segments = 8;
	if (segments > 48) segments = 48;

	D3DXVECTOR2 pts[49];
	for (int i = 0; i <= segments; ++i)
	{
		float a = (i / (float)segments) * 6.2831853f;
		pts[i] = D3DXVECTOR2(cx + cosf(a) * radius, cy + sinf(a) * radius);
	}
	// Antialias on color-key overlays blends toward black and looks like a dark outline.
	mLine->SetAntialias(soft ? TRUE : FALSE);
	mLine->Begin();
	mLine->Draw(pts, segments + 1, color);
	mLine->End();
	mLine->SetAntialias(FALSE);
}

void renderer::draw::triangle_fan(const D3DXVECTOR2* pts, int count, D3DCOLOR color)
{
	if (!pDevice || !pts || count < 3) return;

	// Force opaque RGB — alpha blend over black clear looks black on color-key overlays
	BYTE r = (BYTE)((color >> 16) & 0xFF);
	BYTE g = (BYTE)((color >> 8) & 0xFF);
	BYTE b = (BYTE)(color & 0xFF);
	if (r == 0 && g == 0 && b == 0)
		color = D3DCOLOR_XRGB(1, 1, 1);
	else
		color = D3DCOLOR_XRGB(r, g, b);

	OverlayVertex v[64];
	if (count > 64) count = 64;
	for (int i = 0; i < count; ++i)
		v[i] = { pts[i].x, pts[i].y, 0.f, 1.f, color };

	DWORD oldFVF = 0;
	pDevice->GetFVF(&oldFVF);
	pDevice->SetTexture(0, nullptr);
	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
	pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLEFAN, count - 2, v, sizeof(OverlayVertex));
	pDevice->SetFVF(oldFVF);
}

void renderer::draw::triangle_strip(const D3DXVECTOR2* pts, int count, D3DCOLOR color)
{
	if (!pDevice || !pts || count < 3) return;

	BYTE r = (BYTE)((color >> 16) & 0xFF);
	BYTE g = (BYTE)((color >> 8) & 0xFF);
	BYTE b = (BYTE)(color & 0xFF);
	if (r == 0 && g == 0 && b == 0)
		color = D3DCOLOR_XRGB(1, 1, 1);
	else
		color = D3DCOLOR_XRGB(r, g, b);

	OverlayVertex v[96];
	if (count > 96) count = 96;
	for (int i = 0; i < count; ++i)
		v[i] = { pts[i].x, pts[i].y, 0.f, 1.f, color };

	DWORD oldFVF = 0;
	pDevice->GetFVF(&oldFVF);
	pDevice->SetTexture(0, nullptr);
	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
	pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, count - 2, v, sizeof(OverlayVertex));
	pDevice->SetFVF(oldFVF);
}

void renderer::draw::draw_text(const wchar_t* text, int x, int y, D3DCOLOR color)
{
	if (!pFont || !text) return;
	RECT rect = { x, y, x + 900, y + 40 };
	pFont->DrawTextW(nullptr, text, -1, &rect, DT_NOCLIP, color);
}

void renderer::draw::draw_text_title(const wchar_t* text, int x, int y, D3DCOLOR color)
{
	if (!pFontTitle || !text) return;
	RECT rect = { x, y, x + 900, y + 40 };
	pFontTitle->DrawTextW(nullptr, text, -1, &rect, DT_NOCLIP, color);
}

void renderer::draw::draw_text_name(const wchar_t* text, int x, int y, D3DCOLOR color)
{
	if (!pFontName || !text) return;
	RECT rect = { x, y, x + 900, y + 40 };
	pFontName->DrawTextW(nullptr, text, -1, &rect, DT_NOCLIP, color);
}

void renderer::draw::draw_text_small(const wchar_t* text, int x, int y, D3DCOLOR color)
{
	if (!pFontSmall || !text) return;
	RECT rect = { x, y, x + 900, y + 40 };
	pFontSmall->DrawTextW(nullptr, text, -1, &rect, DT_NOCLIP, color);
}

void renderer::draw::draw_text_tab(const wchar_t* text, int x, int y, D3DCOLOR color)
{
	if (!pFontTab || !text) return;
	RECT rect = { x, y, x + 900, y + 40 };
	pFontTab->DrawTextW(nullptr, text, -1, &rect, DT_NOCLIP, color);
}

void renderer::draw::draw_texture(IDirect3DTexture9* texture, float x, float y, float w, float h, D3DCOLOR color)
{
	if (!pDevice || !texture) return;

	struct TextureVertex
	{
		float x, y, z, rhw;
		D3DCOLOR color;
		float u, v;
	};

	TextureVertex vertices[4] = {
		{ x,     y,     0.0f, 1.0f, color, 0.0f, 0.0f },
		{ x + w, y,     0.0f, 1.0f, color, 1.0f, 0.0f },
		{ x,     y + h, 0.0f, 1.0f, color, 0.0f, 1.0f },
		{ x + w, y + h, 0.0f, 1.0f, color, 1.0f, 1.0f }
	};

	DWORD oldFVF = 0;
	pDevice->GetFVF(&oldFVF);
	
	pDevice->SetTexture(0, texture);
	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
	pDevice->SetRenderState(D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
	pDevice->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
	pDevice->SetRenderState(D3DRS_ZENABLE, FALSE);
	pDevice->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1);
	
	pDevice->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(TextureVertex));
	
	pDevice->SetTexture(0, nullptr);
	pDevice->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
	pDevice->SetFVF(oldFVF);
}

void renderer::draw::draw_startup_screen()
{
	if (!renderer::show_startup || !renderer::pDevice) return;

	const DWORD elapsed = GetTickCount() - renderer::startup_begin;
	const float total_time = 3000.0f; // 3 seconds total
	const float fade_in_time = 800.0f; // 0.8 seconds fade in
	const float hold_time = 1400.0f; // 1.4 seconds hold
	const float fade_out_time = 800.0f; // 0.8 seconds fade out

	if (elapsed > total_time) {
		renderer::show_startup = false;
		return;
	}

	// Calculate alpha based on current phase
	if (elapsed < fade_in_time) {
		// Fade in phase
		renderer::startup_alpha = elapsed / fade_in_time;
	}
	else if (elapsed < fade_in_time + hold_time) {
		// Hold phase
		renderer::startup_alpha = 1.0f;
	}
	else {
		// Fade out phase
		float fade_out_progress = (elapsed - fade_in_time - hold_time) / fade_out_time;
		renderer::startup_alpha = 1.0f - fade_out_progress;
	}

	// Clamp startup_alpha between 0.0 and 1.0
	if (renderer::startup_alpha < 0.0f) renderer::startup_alpha = 0.0f;
	if (renderer::startup_alpha > 1.0f) renderer::startup_alpha = 1.0f;
	
	BYTE alpha = (BYTE)(255 * renderer::startup_alpha);

	// Draw dark background
	renderer::draw::filled_box(0, 0, (float)renderer::screen_w, (float)renderer::screen_h, D3DCOLOR_ARGB(200, 0, 0, 0));

	// Calculate center position for logo
	const float logo_size = 128.0f;
	const float logo_x = (renderer::screen_w - logo_size) * 0.5f;
	const float logo_y = (renderer::screen_h - logo_size) * 0.5f - 50.0f;

	// Draw logo with orange tint and fade
	D3DCOLOR logo_color = D3DCOLOR_ARGB(alpha, 180, 95, 30); // Orange accent color

	if (renderer::pLogoTexture) {
		renderer::draw::draw_texture(renderer::pLogoTexture, logo_x, logo_y, logo_size, logo_size, logo_color);
	}
	else {
		// Fallback: draw orange filled circle if texture loading failed
		renderer::draw::filled_circle(logo_x + logo_size * 0.5f, logo_y + logo_size * 0.5f, logo_size * 0.4f, logo_color, 32);
	}

	// Draw "BearWare" text below logo - properly centered
	const wchar_t* title = L"BearWare";
	
	// Calculate actual text width using DirectX font measurement
	RECT text_rect = { 0, 0, 0, 0 };
	if (renderer::pFontTitle) {
		renderer::pFontTitle->DrawTextW(nullptr, title, -1, &text_rect, DT_CALCRECT, D3DCOLOR_XRGB(255, 255, 255));
	}
	
	// Center the text based on actual measured width
	const float text_width = (float)(text_rect.right - text_rect.left);
	const float text_x = (renderer::screen_w - text_width) * 0.5f;
	const float text_y = logo_y + logo_size + 30;
	
	renderer::draw::draw_text_title(title, (int)text_x, (int)text_y, D3DCOLOR_ARGB(alpha, 255, 255, 255));
}

void renderer::handle_events()
{
	while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
}
