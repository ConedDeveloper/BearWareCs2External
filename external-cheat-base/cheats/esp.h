#pragma once

#include <vector>
#include <mutex>
#include <cstdint>

#include "../renderer/renderer.h"
#include "../SDK/client.dll.hpp"
#include "../SDK/offsets.hpp"

struct viewMatrix
{
	float m[16];
};

struct EspPlayer
{
	uintptr_t pawn = 0;
	vec3 origin{};
	vec3 head{};
	vec3 velocity{};
	int health = 0;
	BYTE team = 0;
	uint32_t flags = 0;
	uint16_t weapon_def = 0;
	bool spotted = false;
	bool visible = false; // true only if YOU spotted them (mask), with hysteresis
	int vis_streak = 0;
	int invis_streak = 0;
	wchar_t name[64]{};
	wchar_t weapon[32]{};
};

struct FootPulse
{
	vec3 pos{};
	float age = 0.f;
	float life = 1.55f;
	float max_radius = 52.f;
	D3DCOLOR color = D3DCOLOR_XRGB(220, 105, 30);
};

struct BulletTrace
{
	vec3 start{};
	vec3 end{};
	float age = 0.f;
	float life = 1.7f;
	D3DCOLOR color = D3DCOLOR_XRGB(220, 105, 30);
	bool hit_player = false; // true if clipped to a player, false if hit wall/geo
};

namespace esp
{
	inline std::vector<EspPlayer> players;
	inline std::mutex players_mutex;
	inline std::vector<FootPulse> pulses;
	inline std::mutex pulses_mutex;
	inline std::vector<BulletTrace> traces;
	inline std::mutex traces_mutex;
	inline viewMatrix vm = {};

	inline uintptr_t pID = 0;
	inline uintptr_t modBase = 0;

	inline bool menu_open = true;
	inline bool menu_minimized = false;
	inline float menu_x = 36.f;
	inline float menu_y = 36.f;
	inline int menu_tab = 1; // 0 Aim, 1 Visual, 2 Settings

	// Keybind settings
	inline int key_menu_toggle = VK_INSERT;
	inline int key_exit = VK_F9;
	inline bool changing_keybind = false; // Track when user is changing a keybind
	inline int changing_keybind_type = 0; // 0 = menu_toggle, 1 = exit

	inline int accent_index = 0;
	inline D3DCOLOR accent = D3DCOLOR_XRGB(180, 95, 30);
	inline D3DCOLOR fov_color = D3DCOLOR_XRGB(180, 95, 30);
	inline D3DCOLOR enemy_color = D3DCOLOR_XRGB(180, 95, 30);
	inline D3DCOLOR team_color = D3DCOLOR_XRGB(55, 120, 180);
	inline int enemy_color_index = 0;
	inline int team_color_index = 7;

	// Aim — always active when enabled (no hold key). Runs even with menu open.
	inline bool aim_enabled = false;
	inline bool aim_draw_fov = true;
	inline bool aim_wallcheck = true;  // only aim when enemy is spotted (visible)
	inline bool aim_teamcheck = true;  // only aim at enemies
	inline bool anti_recoil = false;   // anti-recoil via view-angle write (works with/without aimbot)
	inline bool aim_auto_part = true;  // pick best visible damage part (head→body→legs)
	inline bool trigger_enabled = false;
	inline float trigger_delay = 40.f; // ms before click once crosshair is on target
	inline float aim_fov = 140.0f;
	inline float aim_smooth = 5.0f;
	inline float aim_hitchance = 100.0f;
	// Multi-hitbox: enabled flags + priority order (aim_hit_order[0] = highest priority)
	// Used when aim_auto_part is off. Auto mode considers all parts by damage.
	inline bool aim_hit_enabled[5] = { true, true, true, true, true };
	inline int aim_hit_order[5] = { 0, 1, 2, 3, 4 }; // Head, Neck, Chest, Pelvis, Legs

	inline int local_player_index = 0; // 1..64 for spotted-by-mask

	// Visual
	inline bool esp_enabled = true;
	inline bool esp_boxes = true;
	inline bool esp_health = true;
	inline bool esp_weapon = true;
	inline bool esp_name = true;
	inline bool esp_teammates = false;
	inline bool esp_snaplines = false;
	inline bool esp_head_circle = false;
	inline bool esp_footsteps = false;
	inline bool esp_bullet_path = false;
	inline bool esp_grenade_path = false;
	inline bool esp_look_rays = false;

	void frame();
	void loop();
	void render();

	bool w2s(const vec3& world, vec2& screen, float m[16]);
	uintptr_t entity_from_handle(uintptr_t entity_list, uint32_t handle);
	void apply_accent_to_all();
}
