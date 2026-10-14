#include "config.h"
#include "esp.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

namespace
{
	std::wstring exe_dir()
	{
		wchar_t path[MAX_PATH]{};
		GetModuleFileNameW(nullptr, path, (DWORD)MAX_PATH);
		std::wstring full(path);
		const size_t slash = full.find_last_of(L"\\/");
		if (slash == std::wstring::npos)
			return L".";
		return full.substr(0, slash);
	}

	void write_bool(std::wofstream& f, const wchar_t* key, bool v)
	{
		f << key << L' ' << (v ? 1 : 0) << L'\n';
	}

	void write_int(std::wofstream& f, const wchar_t* key, int v)
	{
		f << key << L' ' << v << L'\n';
	}

	void write_float(std::wofstream& f, const wchar_t* key, float v)
	{
		f << key << L' ' << v << L'\n';
	}

	void write_color(std::wofstream& f, const wchar_t* key, D3DCOLOR c)
	{
		f << key << L' '
			<< ((c >> 16) & 0xFF) << L' '
			<< ((c >> 8) & 0xFF) << L' '
			<< (c & 0xFF) << L'\n';
	}

	bool read_bool(const std::wstring& v, bool& out)
	{
		out = (v != L"0");
		return true;
	}
}

std::wstring config::default_path()
{
	return exe_dir() + L"\\bearware_last.cfg";
}

void config::set_status(const wchar_t* msg)
{
	wcsncpy_s(status, msg, _TRUNCATE);
	status_until = GetTickCount() + 3500;
}

const wchar_t* config::status_text()
{
	if (!status[0] || GetTickCount() > status_until)
		return L"";
	return status;
}

bool config::save_to_file(const std::wstring& path)
{
	std::wofstream f(path);
	if (!f)
		return false;

	f << L"bearware_cfg 2\n";
	write_int(f, L"menu_tab", esp::menu_tab);
	write_float(f, L"menu_x", esp::menu_x);
	write_float(f, L"menu_y", esp::menu_y);
	write_bool(f, L"menu_minimized", esp::menu_minimized);
	write_int(f, L"key_menu_toggle", esp::key_menu_toggle);
	write_int(f, L"key_exit", esp::key_exit);

	write_int(f, L"accent_index", esp::accent_index);
	write_int(f, L"enemy_color_index", esp::enemy_color_index);
	write_int(f, L"team_color_index", esp::team_color_index);
	write_color(f, L"accent", esp::accent);
	write_color(f, L"fov_color", esp::fov_color);
	write_color(f, L"enemy_color", esp::enemy_color);
	write_color(f, L"team_color", esp::team_color);

	write_bool(f, L"aim_enabled", esp::aim_enabled);
	write_bool(f, L"aim_draw_fov", esp::aim_draw_fov);
	write_bool(f, L"aim_wallcheck", esp::aim_wallcheck);
	write_bool(f, L"aim_teamcheck", esp::aim_teamcheck);
	write_bool(f, L"anti_recoil", esp::anti_recoil);
	write_bool(f, L"aim_auto_part", esp::aim_auto_part);
	write_bool(f, L"trigger_enabled", esp::trigger_enabled);
	write_float(f, L"trigger_delay", esp::trigger_delay);
	write_float(f, L"aim_fov", esp::aim_fov);
	write_float(f, L"aim_smooth", esp::aim_smooth);
	write_float(f, L"aim_hitchance", esp::aim_hitchance);

	f << L"aim_hit_enabled";
	for (int i = 0; i < 5; ++i) f << L' ' << (esp::aim_hit_enabled[i] ? 1 : 0);
	f << L'\n';
	f << L"aim_hit_order";
	for (int i = 0; i < 5; ++i) f << L' ' << esp::aim_hit_order[i];
	f << L'\n';

	write_bool(f, L"esp_enabled", esp::esp_enabled);
	write_bool(f, L"esp_boxes", esp::esp_boxes);
	write_bool(f, L"esp_health", esp::esp_health);
	write_bool(f, L"esp_weapon", esp::esp_weapon);
	write_bool(f, L"esp_name", esp::esp_name);
	write_bool(f, L"esp_teammates", esp::esp_teammates);
	write_bool(f, L"esp_snaplines", esp::esp_snaplines);
	write_bool(f, L"esp_head_circle", esp::esp_head_circle);
	write_bool(f, L"esp_footsteps", esp::esp_footsteps);
	write_bool(f, L"esp_bullet_path", esp::esp_bullet_path);
	write_bool(f, L"esp_grenade_path", esp::esp_grenade_path);
	write_bool(f, L"esp_look_rays", esp::esp_look_rays);

	return true;
}

bool config::load_from_file(const std::wstring& path)
{
	std::wifstream f(path);
	if (!f)
		return false;

	std::wstring key;
	while (f >> key)
	{
		if (key == L"bearware_cfg")
		{
			int ver = 0; f >> ver;
		}
		else if (key == L"menu_tab") f >> esp::menu_tab;
		else if (key == L"menu_x") f >> esp::menu_x;
		else if (key == L"menu_y") f >> esp::menu_y;
		else if (key == L"menu_minimized")
		{
			int v; f >> v; esp::menu_minimized = v != 0;
		}
		else if (key == L"key_menu_toggle") f >> esp::key_menu_toggle;
		else if (key == L"key_exit") f >> esp::key_exit;
		else if (key == L"accent_index") f >> esp::accent_index;
		else if (key == L"enemy_color_index") f >> esp::enemy_color_index;
		else if (key == L"team_color_index") f >> esp::team_color_index;
		else if (key == L"accent" || key == L"fov_color" || key == L"enemy_color" || key == L"team_color")
		{
			int r, g, b; f >> r >> g >> b;
			D3DCOLOR c = D3DCOLOR_XRGB(r, g, b);
			if (key == L"accent") esp::accent = c;
			else if (key == L"fov_color") esp::fov_color = c;
			else if (key == L"enemy_color") esp::enemy_color = c;
			else esp::team_color = c;
		}
		else if (key == L"aim_enabled") { int v; f >> v; esp::aim_enabled = v != 0; }
		else if (key == L"aim_draw_fov") { int v; f >> v; esp::aim_draw_fov = v != 0; }
		else if (key == L"aim_wallcheck") { int v; f >> v; esp::aim_wallcheck = v != 0; }
		else if (key == L"aim_teamcheck") { int v; f >> v; esp::aim_teamcheck = v != 0; }
		else if (key == L"anti_recoil") { int v; f >> v; esp::anti_recoil = v != 0; }
		else if (key == L"aim_auto_part") { int v; f >> v; esp::aim_auto_part = v != 0; }
		else if (key == L"trigger_enabled") { int v; f >> v; esp::trigger_enabled = v != 0; }
		else if (key == L"trigger_delay") f >> esp::trigger_delay;
		else if (key == L"aim_fov") f >> esp::aim_fov;
		else if (key == L"aim_smooth") f >> esp::aim_smooth;
		else if (key == L"aim_hitchance") f >> esp::aim_hitchance;
		else if (key == L"aim_hit_enabled")
		{
			for (int i = 0; i < 5; ++i) { int v; f >> v; esp::aim_hit_enabled[i] = v != 0; }
		}
		else if (key == L"aim_hit_order")
		{
			for (int i = 0; i < 5; ++i) f >> esp::aim_hit_order[i];
		}
		else if (key == L"esp_enabled") { int v; f >> v; esp::esp_enabled = v != 0; }
		else if (key == L"esp_boxes") { int v; f >> v; esp::esp_boxes = v != 0; }
		else if (key == L"esp_health") { int v; f >> v; esp::esp_health = v != 0; }
		else if (key == L"esp_weapon") { int v; f >> v; esp::esp_weapon = v != 0; }
		else if (key == L"esp_name") { int v; f >> v; esp::esp_name = v != 0; }
		else if (key == L"esp_teammates") { int v; f >> v; esp::esp_teammates = v != 0; }
		else if (key == L"esp_snaplines") { int v; f >> v; esp::esp_snaplines = v != 0; }
		else if (key == L"esp_head_circle") { int v; f >> v; esp::esp_head_circle = v != 0; }
		else if (key == L"esp_footsteps") { int v; f >> v; esp::esp_footsteps = v != 0; }
		else if (key == L"esp_bullet_path") { int v; f >> v; esp::esp_bullet_path = v != 0; }
		else if (key == L"esp_grenade_path") { int v; f >> v; esp::esp_grenade_path = v != 0; }
		else if (key == L"esp_look_rays") { int v; f >> v; esp::esp_look_rays = v != 0; }
		else
		{
			// skip unknown value token
			std::wstring junk; f >> junk;
		}
	}

	esp::aim_fov = std::clamp(esp::aim_fov, 20.f, 500.f);
	esp::aim_smooth = std::clamp(esp::aim_smooth, 1.f, 25.f);
	esp::aim_hitchance = std::clamp(esp::aim_hitchance, 5.f, 100.f);
	esp::trigger_delay = std::clamp(esp::trigger_delay, 0.f, 500.f);
	esp::menu_tab = std::clamp(esp::menu_tab, 0, 2);

	// validate hit order contains 0..4
	bool seen[5]{};
	bool order_ok = true;
	for (int i = 0; i < 5; ++i)
	{
		int h = esp::aim_hit_order[i];
		if (h < 0 || h > 4 || seen[h]) { order_ok = false; break; }
		seen[h] = true;
	}
	if (!order_ok)
	{
		for (int i = 0; i < 5; ++i) esp::aim_hit_order[i] = i;
	}

	// never leave every hitbox disabled
	bool any_hit = false;
	for (int i = 0; i < 5; ++i)
		if (esp::aim_hit_enabled[i]) { any_hit = true; break; }
	if (!any_hit)
		esp::aim_hit_enabled[0] = true;

	return true;
}

bool config::save_last()
{
	const bool ok = save_to_file(default_path());
	set_status(ok ? L"Config saved" : L"Save failed");
	return ok;
}

bool config::load_last()
{
	const std::wstring path = default_path();
	if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES)
	{
		set_status(L"No saved config yet");
		return false;
	}
	const bool ok = load_from_file(path);
	set_status(ok ? L"Loaded last config" : L"Load failed");
	return ok;
}

bool config::export_dialog(HWND owner)
{
	wchar_t file[MAX_PATH] = L"bearware_export.cfg";
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	ofn.lpstrFilter = L"BearWare Config (*.cfg)\0*.cfg\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrDefExt = L"cfg";
	ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
	ofn.lpstrTitle = L"Export BearWare Config";

	if (!GetSaveFileNameW(&ofn))
		return false;

	const bool ok = save_to_file(file);
	if (ok)
	{
		save_to_file(default_path()); // also remember as last
		set_status(L"Config exported");
	}
	else set_status(L"Export failed");
	return ok;
}

bool config::import_dialog(HWND owner)
{
	wchar_t file[MAX_PATH]{};
	OPENFILENAMEW ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = owner;
	ofn.lpstrFilter = L"BearWare Config (*.cfg)\0*.cfg\0All Files (*.*)\0*.*\0";
	ofn.lpstrFile = file;
	ofn.nMaxFile = MAX_PATH;
	ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
	ofn.lpstrTitle = L"Import BearWare Config";

	if (!GetOpenFileNameW(&ofn))
		return false;

	const bool ok = load_from_file(file);
	if (ok)
	{
		save_to_file(default_path()); // remember imported as last
		set_status(L"Config imported");
	}
	else set_status(L"Import failed");
	return ok;
}
