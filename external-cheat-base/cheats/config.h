#pragma once

#include <Windows.h>
#include <string>

namespace config
{
	// Autosave / remember path: <exe_dir>\bearware_last.cfg
	std::wstring default_path();

	bool save_to_file(const std::wstring& path);
	bool load_from_file(const std::wstring& path);

	bool save_last();   // writes bearware_last.cfg
	bool load_last();   // loads bearware_last.cfg if present

	bool export_dialog(HWND owner);
	bool import_dialog(HWND owner);

	inline wchar_t status[96] = L"";
	inline DWORD status_until = 0;

	void set_status(const wchar_t* msg);
	const wchar_t* status_text();
}
