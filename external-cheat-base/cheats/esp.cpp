#include "esp.h"
#include "config.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unordered_map>

using namespace cs2_dumper::offsets;
using namespace cs2_dumper::schemas::client_dll;

namespace
{
	constexpr float MENU_W = 420.0f;
	constexpr float ROW_H = 28.0f;
	constexpr float PAD = 16.0f;
	constexpr float HEADER_H = 52.0f;
	constexpr float TAB_H = 32.0f;
	constexpr std::ptrdiff_t kEntityStride = 0x70;
	constexpr int FL_ONGROUND = (1 << 0);
	constexpr DWORD kFootPulseCooldownMs = 420;

	enum class Tab : int { Aim = 0, Visual = 1, Settings = 2 };

	struct MenuToggle { const wchar_t* label; bool* value; };
	struct ColorSwatch { const wchar_t* name; D3DCOLOR color; };

	const ColorSwatch kColors[] = {
		{ L"Burnt Orange", D3DCOLOR_XRGB(180, 95, 30) },
		{ L"Dark Orange",  D3DCOLOR_XRGB(160, 78, 22) },
		{ L"Amber",        D3DCOLOR_XRGB(200, 120, 35) },
		{ L"Crimson",      D3DCOLOR_XRGB(200, 40, 40) },
		{ L"Rose",         D3DCOLOR_XRGB(230, 60, 110) },
		{ L"Gold",         D3DCOLOR_XRGB(240, 190, 50) },
		{ L"Lime",         D3DCOLOR_XRGB(90, 220, 80) },
		{ L"Blue",         D3DCOLOR_XRGB(70, 140, 255) },
		{ L"Cyan",         D3DCOLOR_XRGB(50, 200, 230) },
		{ L"Violet",       D3DCOLOR_XRGB(160, 90, 255) },
		{ L"White",        D3DCOLOR_XRGB(235, 235, 240) },
	};
	constexpr int kColorCount = sizeof(kColors) / sizeof(kColors[0]);

	const wchar_t* kHitboxes[] = { L"Head", L"Neck", L"Chest", L"Pelvis", L"Legs" };
	constexpr int kHitboxCount = 5;

	bool point_in_rect(int px, int py, float x, float y, float w, float h)
	{
		return px >= (int)x && px <= (int)(x + w) && py >= (int)y && py <= (int)(y + h);
	}

	void utf8_to_wide(const char* src, wchar_t* dst, size_t dstCount)
	{
		if (!dst || !dstCount) return;
		dst[0] = 0;
		if (!src) return;
		MultiByteToWideChar(CP_UTF8, 0, src, -1, dst, (int)dstCount);
		dst[dstCount - 1] = 0;
	}

	const wchar_t* weapon_name(uint16_t id)
	{
		switch (id)
		{
		case 1: return L"Deagle"; case 4: return L"Glock"; case 7: return L"AK-47";
		case 8: return L"AUG"; case 9: return L"AWP"; case 10: return L"FAMAS";
		case 13: return L"Galil"; case 16: return L"M4A4"; case 19: return L"P90";
		case 24: return L"UMP-45"; case 26: return L"Bizon"; case 27: return L"MAG-7";
		case 28: return L"Negev"; case 29: return L"Sawed-Off"; case 30: return L"Tec-9";
		case 32: return L"P2000"; case 33: return L"MP7"; case 34: return L"MP9";
		case 35: return L"Nova"; case 36: return L"P250"; case 38: return L"SCAR-20";
		case 39: return L"SG 553"; case 40: return L"SSG 08"; case 42: return L"Knife";
		case 43: return L"Flash"; case 44: return L"HE"; case 45: return L"Smoke";
		case 46: return L"Molotov"; case 47: return L"Decoy"; case 48: return L"Incendiary";
		case 49: return L"C4"; case 60: return L"M4A1-S"; case 61: return L"USP-S";
		case 63: return L"CZ75"; case 64: return L"R8"; default: return L"Weapon";
		}
	}

	uintptr_t resolve_weapon(uintptr_t entity_list, uintptr_t pawn)
	{
		uintptr_t services = memory::Read<uintptr_t>(pawn + C_BasePlayerPawn::m_pWeaponServices);
		if (!services) return 0;
		uint32_t handle = memory::Read<uint32_t>(services + CPlayer_WeaponServices::m_hActiveWeapon);
		return esp::entity_from_handle(entity_list, handle);
	}

	uint16_t read_weapon_def(uintptr_t weapon)
	{
		if (!weapon) return 0;
		return memory::Read<uint16_t>(
			weapon + C_EconEntity::m_AttributeManager + C_AttributeContainer::m_Item + C_EconItemView::m_iItemDefinitionIndex);
	}

	bool get_pawn_origin(uintptr_t pawn, vec3& origin, vec3& head)
	{
		if (!pawn) return false;
		uintptr_t node = memory::Read<uintptr_t>(pawn + C_BaseEntity::m_pGameSceneNode);
		if (node)
		{
			origin = memory::Read<vec3>(node + CGameSceneNode::m_vecAbsOrigin);
			if (origin.x != 0.f || origin.y != 0.f || origin.z != 0.f)
			{
				vec3 view = memory::Read<vec3>(pawn + C_BaseModelEntity::m_vecViewOffset);
				if (view.z < 40.f || view.z > 90.f) view = { 0.f, 0.f, 64.f };
				head = origin + view;
				return true;
			}
		}
		origin = memory::Read<vec3>(pawn + C_BasePlayerPawn::m_vOldOrigin);
		vec3 view = memory::Read<vec3>(pawn + C_BaseModelEntity::m_vecViewOffset);
		if (view.z < 40.f || view.z > 90.f) view = { 0.f, 0.f, 64.f };
		head = origin + view;
		return origin.x != 0.f || origin.y != 0.f || origin.z != 0.f;
	}

	vec3 hitbox_point(const EspPlayer& p, int hitbox)
	{
		switch (hitbox)
		{
		case 1: return { p.head.x, p.head.y, p.head.z - 6.f };
		case 2: return { p.origin.x, p.origin.y, p.origin.z + 42.f };
		case 3: return { p.origin.x, p.origin.y, p.origin.z + 24.f };
		case 4: return { p.origin.x, p.origin.y, p.origin.z + 10.f };
		default: return p.head;
		}
	}

	void normalize_angles(float& pitch, float& yaw)
	{
		if (pitch > 89.f) pitch = 89.f;
		if (pitch < -89.f) pitch = -89.f;
		while (yaw > 180.f) yaw -= 360.f;
		while (yaw < -180.f) yaw += 360.f;
	}

	float angle_delta(float from, float to)
	{
		float d = to - from;
		while (d > 180.f) d -= 360.f;
		while (d < -180.f) d += 360.f;
		return d;
	}

	void calc_angle(const vec3& src, const vec3& dst, float& pitch, float& yaw)
	{
		vec3 d = dst - src;
		float hyp = std::sqrt(d.x * d.x + d.y * d.y);
		pitch = -std::atan2(d.z, hyp) * (180.0f / 3.14159265f);
		yaw = std::atan2(d.y, d.x) * (180.0f / 3.14159265f);
		normalize_angles(pitch, yaw);
	}

	float dist2d(float ax, float ay, float bx, float by)
	{
		float dx = ax - bx, dy = ay - by;
		return std::sqrt(dx * dx + dy * dy);
	}

	vec3 angle_forward(float pitch, float yaw)
	{
		float pr = pitch * (3.14159265f / 180.f);
		float yr = yaw * (3.14159265f / 180.f);
		float cp = std::cos(pr), sp = std::sin(pr);
		float cy = std::cos(yr), sy = std::sin(yr);
		return { cp * cy, cp * sy, -sp };
	}

	struct QAngle { float pitch, yaw, roll; };

	bool read_view_angles(float& pitch, float& yaw)
	{
		QAngle a = memory::Read<QAngle>(esp::modBase + client_dll::dwViewAngles);
		pitch = a.pitch;
		yaw = a.yaw;
		return std::isfinite(pitch) && std::isfinite(yaw);
	}

	bool read_aim_punch(uintptr_t local_pawn, QAngle& out)
	{
		out = {};
		if (!local_pawn) return false;
		uintptr_t svc = memory::Read<uintptr_t>(local_pawn + C_CSPlayerPawn::m_pAimPunchServices);
		if (!svc) return false;
		out = memory::Read<QAngle>(svc + CCSPlayer_AimPunchServices::m_predictableBaseAngle);
		return std::isfinite(out.pitch) && std::isfinite(out.yaw);
	}

	float hitbox_damage(int hitbox)
	{
		switch (hitbox)
		{
		case 0: return 100.f; // Head
		case 1: return 85.f;  // Neck
		case 2: return 65.f;  // Chest
		case 3: return 45.f;  // Pelvis
		case 4: return 30.f;  // Legs
		default: return 1.f;
		}
	}

	vec3 predict_pos(const EspPlayer& p, const vec3& pos)
	{
		// Short lead so moving targets stay under the crosshair
		constexpr float kPred = 0.018f;
		return {
			pos.x + p.velocity.x * kPred,
			pos.y + p.velocity.y * kPred,
			pos.z + p.velocity.z * kPred
		};
	}

	// Mouse aim with sub-pixel accumulation so small corrections aren't lost.
	void aim_mouse_smooth(float target_x, float target_y, float smooth)
	{
		static float acc_x = 0.f, acc_y = 0.f;

		const float cx = renderer::game_x + WINDOW_W * 0.5f;
		const float cy = renderer::game_y + WINDOW_H * 0.5f;

		float dx = target_x - cx;
		float dy = target_y - cy;
		const float dist = std::sqrt(dx * dx + dy * dy);
		if (dist < 0.75f)
		{
			acc_x = acc_y = 0.f;
			return;
		}

		// Higher smooth = slower. Adaptive: close = finer, far = snappier.
		float s = (std::max)(1.2f, smooth);
		if (dist > 80.f) s *= 0.72f;
		else if (dist < 18.f) s *= 1.15f;

		float step = dist / s;
		if (step > 36.f) step = 36.f;
		if (step < 0.35f) step = 0.35f;

		acc_x += (dx / dist) * step;
		acc_y += (dy / dist) * step;

		LONG idx = (LONG)acc_x;
		LONG idy = (LONG)acc_y;
		acc_x -= (float)idx;
		acc_y -= (float)idy;

		if (idx == 0 && idy == 0)
			return;

		INPUT input{};
		input.type = INPUT_MOUSE;
		input.mi.dx = idx;
		input.mi.dy = idy;
		input.mi.dwFlags = MOUSEEVENTF_MOVE;
		SendInput(1, &input, sizeof(INPUT));
	}

	// Project punch-compensated aim angles into screen space (sens-independent).
	bool aim_screen_from_world(const vec3& eye, const vec3& world_target, const QAngle& punch, bool use_rcs,
		int shots_fired, float& out_sx, float& out_sy)
	{
		float wish_p = 0.f, wish_y = 0.f;
		calc_angle(eye, world_target, wish_p, wish_y);

		if (use_rcs && shots_fired > 0)
		{
			// CS-style: bullets travel along view + punch*2
			wish_p -= punch.pitch * 2.f;
			wish_y -= punch.yaw * 2.f;
			normalize_angles(wish_p, wish_y);
		}

		float cur_p = 0.f, cur_y = 0.f;
		if (!read_view_angles(cur_p, cur_y))
			return false;

		const float err_p = angle_delta(cur_p, wish_p);
		const float err_y = angle_delta(cur_y, wish_y);

		// Approx vertical FOV → pixels/degree from view matrix scale
		const float v_fov = 74.f;
		const float aspect = (float)WINDOW_W / (float)WINDOW_H;
		const float h_fov = 2.f * std::atan(std::tan((v_fov * 0.5f) * (3.14159265f / 180.f)) * aspect) * (180.f / 3.14159265f);
		const float ppd_y = (WINDOW_H * 0.5f) / (v_fov * 0.5f);
		const float ppd_x = (WINDOW_W * 0.5f) / (h_fov * 0.5f);

		out_sx = renderer::game_x + WINDOW_W * 0.5f + err_y * ppd_x;
		out_sy = renderer::game_y + WINDOW_H * 0.5f - err_p * ppd_y;
		return true;
	}

	bool is_visible_to_local(const EspPlayer& p)
	{
		// Strict: only SpottedByMask-for-you (see loop). Never use team/radar m_bSpotted.
		return p.visible;
	}

	void draw_world_ring(const vec3& center, float radius, D3DCOLOR color, float* matrix, int segs = 48, float width = 1.6f)
	{
		if (!renderer::mLine || radius <= 0.5f || !matrix) return;
		if (segs < 8) segs = 8;
		if (segs > 48) segs = 48;

		BYTE r = (BYTE)((color >> 16) & 0xFF);
		BYTE g = (BYTE)((color >> 8) & 0xFF);
		BYTE b = (BYTE)(color & 0xFF);
		if (r < 8 && g < 8 && b < 8) { r = 245; g = 245; b = 248; }
		color = D3DCOLOR_XRGB(r, g, b);

		D3DXVECTOR2 pts[49];
		int count = 0;
		renderer::mLine->SetAntialias(FALSE);
		renderer::mLine->SetWidth(width);
		for (int i = 0; i <= segs; ++i)
		{
			float a = ((i % segs) / (float)segs) * 6.2831853f;
			vec3 world{ center.x + cosf(a) * radius, center.y + sinf(a) * radius, center.z + 1.2f };
			vec2 screen{};
			if (!esp::w2s(world, screen, matrix))
			{
				if (count >= 2)
				{
					renderer::mLine->Begin();
					renderer::mLine->Draw(pts, count, color);
					renderer::mLine->End();
				}
				count = 0;
				continue;
			}
			pts[count++] = D3DXVECTOR2(screen.x, screen.y);
		}
		if (count >= 2)
		{
			renderer::mLine->Begin();
			renderer::mLine->Draw(pts, count, color);
			renderer::mLine->End();
		}
		renderer::mLine->SetWidth(1.5f);
	}

	void draw_world_ring_dashed(const vec3& center, float radius, D3DCOLOR color, float* matrix, int segs, int dash_on, int dash_off, float width)
	{
		if (!renderer::mLine || radius <= 0.5f || !matrix) return;
		if (segs < 12) segs = 12;
		if (segs > 48) segs = 48;
		if (dash_on < 1) dash_on = 1;
		if (dash_off < 0) dash_off = 0;

		BYTE r = (BYTE)((color >> 16) & 0xFF);
		BYTE g = (BYTE)((color >> 8) & 0xFF);
		BYTE b = (BYTE)(color & 0xFF);
		color = D3DCOLOR_XRGB((std::max)(r, (BYTE)1), (std::max)(g, (BYTE)1), (std::max)(b, (BYTE)1));

		renderer::mLine->SetAntialias(FALSE);
		renderer::mLine->SetWidth(width);
		const int period = dash_on + dash_off;
		vec2 prev{};
		bool have = false;
		for (int i = 0; i <= segs; ++i)
		{
			float a = ((i % segs) / (float)segs) * 6.2831853f;
			vec2 screen{};
			if (!esp::w2s({ center.x + cosf(a) * radius, center.y + sinf(a) * radius, center.z + 1.2f }, screen, matrix))
			{
				have = false;
				continue;
			}
			const bool draw = (i % period) < dash_on;
			if (have && draw)
			{
				D3DXVECTOR2 pts[2] = { D3DXVECTOR2(prev.x, prev.y), D3DXVECTOR2(screen.x, screen.y) };
				renderer::mLine->Begin();
				renderer::mLine->Draw(pts, 2, color);
				renderer::mLine->End();
			}
			prev = screen;
			have = draw;
		}
		renderer::mLine->SetWidth(1.5f);
	}

	void draw_foot_wave(const FootPulse& pulse, float* matrix)
	{
		const float life_t = std::clamp(pulse.age / pulse.life, 0.f, 1.f);
		if (life_t >= 0.98f) return;

		const float ease = 1.f - (1.f - life_t) * (1.f - life_t);
		const float radius = 6.f + ease * pulse.max_radius;
		const D3DCOLOR col = D3DCOLOR_XRGB(220, 105, 30);

		// Single clean ring (no fill / no dash), matches overlay style.
		draw_world_ring(pulse.pos, radius, col, matrix, 48, 2.0f);
	}

	// Ray vs player AABB — returns hit distance along ray (0..len) or -1
	float ray_hit_player_t(const vec3& start, const vec3& dir, float len, const EspPlayer& p)
	{
		const float hw = 18.f;
		const float mins[3] = { p.origin.x - hw, p.origin.y - hw, p.origin.z };
		const float maxs[3] = { p.origin.x + hw, p.origin.y + hw, p.head.z + 6.f };
		const float orig[3] = { start.x, start.y, start.z };
		const float d[3] = { dir.x, dir.y, dir.z };

		float tmin = 0.f, tmax = len;
		for (int i = 0; i < 3; ++i)
		{
			if (fabsf(d[i]) < 1e-6f)
			{
				if (orig[i] < mins[i] || orig[i] > maxs[i]) return -1.f;
				continue;
			}
			float inv = 1.f / d[i];
			float t0 = (mins[i] - orig[i]) * inv;
			float t1 = (maxs[i] - orig[i]) * inv;
			if (t0 > t1) std::swap(t0, t1);
			tmin = (std::max)(tmin, t0);
			tmax = (std::min)(tmax, t1);
			if (tmax < tmin) return -1.f;
		}
		if (tmin < 0.f || tmin > len) return -1.f;
		return tmin;
	}

	// Clip bullet end to nearest player along the shot, or first W2S break (wall/occlusion approx).
	// Sets hit_player=true if the clip point is a player hitbox rather than geometry.
	vec3 clip_bullet_end(const vec3& start, const vec3& end, const std::vector<EspPlayer>& plist, BYTE local_team, bool teamcheck, float* matrix, bool& hit_player)
	{
		hit_player = false;
		vec3 d = end - start;
		float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
		if (len < 1.f) return end;
		vec3 dir{ d.x / len, d.y / len, d.z / len };

		float best_t = len;
		for (const EspPlayer& p : plist)
		{
			if (p.health <= 0) continue;
			if (teamcheck && local_team != 0 && p.team == local_team) continue;
			float t = ray_hit_player_t(start, dir, best_t, p);
			if (t > 2.f && t < best_t)
			{
				best_t = t;
				hit_player = true;
			}
		}

		// Wall approx: walk the ray; stop when a sample fails W2S after valid ones (went into geo / behind)
		if (matrix)
		{
			bool seen_ok = false;
			constexpr int kSamples = 64;
			for (int i = 1; i <= kSamples; ++i)
			{
				float t = (i / (float)kSamples) * best_t;
				vec3 p{ start.x + dir.x * t, start.y + dir.y * t, start.z + dir.z * t };
				vec2 s{};
				if (esp::w2s(p, s, matrix))
				{
					seen_ok = true;
				}
				else if (seen_ok && t > 40.f)
				{
					// Clipped to geo — override any player hit that was beyond the wall
					best_t = t;
					hit_player = false;
					break;
				}
			}
		}

		return { start.x + dir.x * best_t, start.y + dir.y * best_t, start.z + dir.z * best_t };
	}

	void draw_bullet_path_3d(const BulletTrace& tr, float* matrix)
	{
		if (!renderer::mLine || !matrix) return;
		const float life_t = std::clamp(tr.age / tr.life, 0.f, 1.f);
		if (life_t >= 0.98f) return;

		// Fade alpha from fully opaque → transparent over the trace lifetime
		const BYTE alpha = (BYTE)(255.f * (1.f - life_t));
		const D3DCOLOR col = D3DCOLOR_ARGB(alpha, 255, 255, 255); // white

		const float start_t = life_t * 0.85f;
		constexpr int kSegs = 40;
		vec2 prev{};
		bool have_prev = false;
		vec2 last_screen{};
		renderer::mLine->SetAntialias(TRUE);
		renderer::mLine->SetWidth(3.0f); // thicker white line

		for (int i = 0; i <= kSegs; ++i)
		{
			const float u = i / (float)kSegs;
			const float t = start_t + (1.f - start_t) * u;
			vec3 p{
				tr.start.x + (tr.end.x - tr.start.x) * t,
				tr.start.y + (tr.end.y - tr.start.y) * t,
				tr.start.z + (tr.end.z - tr.start.z) * t
			};

			vec2 screen{};
			if (!esp::w2s(p, screen, matrix))
			{
				// Stop drawing at wall / occlusion — don't continue past the break
				break;
			}
			if (have_prev)
			{
				D3DXVECTOR2 pts[2] = { D3DXVECTOR2(prev.x, prev.y), D3DXVECTOR2(screen.x, screen.y) };
				renderer::mLine->Begin();
				renderer::mLine->Draw(pts, 2, col);
				renderer::mLine->End();
			}
			prev = screen;
			last_screen = screen;
			have_prev = true;
		}

		// Draw X at the impact point (only when the end is visible on screen)
		if (have_prev)
		{
			vec2 end_s{};
			if (esp::w2s(tr.end, end_s, matrix))
			{
				const float sz = tr.hit_player ? 8.f : 5.f; // bigger X on player hit
				renderer::mLine->SetWidth(2.0f);
				renderer::mLine->Begin();
				D3DXVECTOR2 xa[2] = { D3DXVECTOR2(end_s.x - sz, end_s.y - sz), D3DXVECTOR2(end_s.x + sz, end_s.y + sz) };
				renderer::mLine->Draw(xa, 2, col);
				renderer::mLine->End();
				renderer::mLine->Begin();
				D3DXVECTOR2 xb[2] = { D3DXVECTOR2(end_s.x + sz, end_s.y - sz), D3DXVECTOR2(end_s.x - sz, end_s.y + sz) };
				renderer::mLine->Draw(xb, 2, col);
				renderer::mLine->End();
			}
		}

		renderer::mLine->SetWidth(1.5f);
		renderer::mLine->SetAntialias(FALSE);
	}

	bool is_grenade_def(uint16_t id)
	{
		return id == 43 || id == 44 || id == 45 || id == 46 || id == 47 || id == 48;
	}

	bool read_entity_designer_name(uintptr_t ent, char* out, size_t n)
	{
		if (!ent || !out || n < 2) return false;
		out[0] = 0;
		uintptr_t ident = memory::Read<uintptr_t>(ent + CEntityInstance::m_pEntity);
		if (!ident) return false;
		uintptr_t name_ptr = memory::Read<uintptr_t>(ident + CEntityIdentity::m_designerName);
		if (!name_ptr) return false;
		SIZE_T got = 0;
		if (!ReadProcessMemory(memory::gHandle, (LPCVOID)name_ptr, out, (SIZE_T)(n - 1), &got) || !got)
			return false;
		out[n - 1] = 0;
		return out[0] != 0;
	}

	bool is_grenade_projectile_name(const char* name)
	{
		if (!name || !name[0]) return false;
		if (!strstr(name, "projectile")) return false;
		return strstr(name, "hegrenade") || strstr(name, "flashbang") || strstr(name, "smoke")
			|| strstr(name, "molotov") || strstr(name, "incendiary") || strstr(name, "decoy");
	}

	void setup_grenade_throw(uintptr_t local_pawn, uintptr_t weapon, float pitch, float yaw, vec3& origin, vec3& velocity)
	{
		float strength = 1.f;
		if (weapon)
		{
			const bool pin = memory::Read<bool>(weapon + C_BaseCSGrenade::m_bPinPulled);
			if (pin)
			{
				strength = std::clamp(memory::Read<float>(weapon + C_BaseCSGrenade::m_flThrowStrength), 0.f, 1.f);
				if (fabsf(strength - 0.5f) <= 0.1f) strength = 0.5f;
			}
		}

		float ap = pitch;
		if (ap > 90.f) ap -= 360.f;
		else if (ap < -90.f) ap += 360.f;
		ap -= (90.f - fabsf(ap)) * 10.f / 90.f;

		vec3 feet{}, eye{};
		get_pawn_origin(local_pawn, feet, eye);
		eye.z += strength * 12.f - 12.f;

		vec3 fwd = angle_forward(ap, yaw);
		origin = { eye.x + fwd.x * 16.f, eye.y + fwd.y * 16.f, eye.z + fwd.z * 16.f };

		constexpr float kThrowVel = 750.f * 0.9f;
		const float throw_speed = (strength * 0.7f + 0.3f) * kThrowVel;
		vec3 player_vel = memory::Read<vec3>(local_pawn + C_BaseEntity::m_vecAbsVelocity);
		velocity = {
			fwd.x * throw_speed + player_vel.x * 1.25f,
			fwd.y * throw_speed + player_vel.y * 1.25f,
			fwd.z * throw_speed + player_vel.z * 1.25f
		};
	}

	void simulate_grenade_path(vec3 pos, vec3 vel, float ground_z, std::vector<vec3>& out_pts, vec3& land)
	{
		out_pts.clear();
		out_pts.reserve(128);
		constexpr float dt = 1.f / 64.f;
		constexpr float gravity = 800.f * 0.4f;
		constexpr float elasticity = 0.45f;
		land = pos;
		out_pts.push_back(pos);

		int bounces = 0;
		for (int tick = 0; tick < 512; ++tick)
		{
			vel.z -= gravity * dt;
			pos.x += vel.x * dt;
			pos.y += vel.y * dt;
			pos.z += vel.z * dt;

			if (pos.z <= ground_z + 1.5f && vel.z <= 0.f)
			{
				pos.z = ground_z + 1.5f;
				vel.z *= -elasticity;
				vel.x *= 0.7f;
				vel.y *= 0.7f;
				++bounces;
				const float spd2 = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
				if (spd2 < 400.f || bounces > 6)
				{
					land = pos;
					out_pts.push_back(pos);
					break;
				}
			}

			if ((tick % 2) == 0)
				out_pts.push_back(pos);

			const float spd2 = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
			if (tick > 8 && spd2 < 80.f)
			{
				land = pos;
				break;
			}
		}
		out_pts.push_back(land);
	}

	void draw_world_polyline(const std::vector<vec3>& pts, D3DCOLOR color, float* matrix, float width = 1.8f)
	{
		if (!renderer::mLine || !matrix || pts.size() < 2) return;
		BYTE r = (BYTE)((color >> 16) & 0xFF);
		BYTE g = (BYTE)((color >> 8) & 0xFF);
		BYTE b = (BYTE)(color & 0xFF);
		color = D3DCOLOR_XRGB((std::max)(r, (BYTE)1), (std::max)(g, (BYTE)1), (std::max)(b, (BYTE)1));

		renderer::mLine->SetAntialias(FALSE);
		renderer::mLine->SetWidth(width);
		vec2 prev{};
		bool have = false;
		for (const vec3& w : pts)
		{
			vec2 s{};
			if (!esp::w2s(w, s, matrix))
			{
				have = false;
				continue;
			}
			if (have)
			{
				D3DXVECTOR2 seg[2] = { D3DXVECTOR2(prev.x, prev.y), D3DXVECTOR2(s.x, s.y) };
				renderer::mLine->Begin();
				renderer::mLine->Draw(seg, 2, color);
				renderer::mLine->End();
			}
			prev = s;
			have = true;
		}
		renderer::mLine->SetWidth(1.5f);
	}

	void draw_grenade_path_viz(const std::vector<vec3>& pts, const vec3& land, D3DCOLOR color, float* matrix)
	{
		draw_world_polyline(pts, color, matrix, 2.0f);
		draw_world_ring(land, 18.f, color, matrix, 40, 2.0f);
		draw_world_ring(land, 8.f, color, matrix, 28, 1.4f);
	}

	void draw_toggle(float x, float y, float w, const wchar_t* label, bool on, bool hovered, D3DCOLOR accent)
	{
		D3DCOLOR bg = hovered ? D3DCOLOR_XRGB(44, 38, 32) : D3DCOLOR_XRGB(34, 30, 26);
		renderer::draw::filled_box(x, y, w, ROW_H - 4.f, bg);
		renderer::draw::filled_box(x, y, 3.f, ROW_H - 4.f, on ? accent : D3DCOLOR_XRGB(55, 50, 45));

		// pill switch
		float sx = x + w - 52.f;
		float sy = y + 5.f;
		renderer::draw::filled_box(sx, sy, 36.f, 16.f, on ? accent : D3DCOLOR_XRGB(55, 52, 48));
		renderer::draw::filled_box(on ? sx + 18.f : sx + 2.f, sy + 2.f, 14.f, 12.f, D3DCOLOR_XRGB(245, 245, 245));

		renderer::draw::draw_text(label, (int)(x + 14.f), (int)(y + 5.f), D3DCOLOR_XRGB(235, 230, 225));
	}

	int hit_stepper(float x, float y, float w, int mx, int my)
	{
		const float btn = 24.f;
		const float minus_x = x + w - btn * 2.f - 72.f;
		const float plus_x = x + w - btn - 8.f;
		if (point_in_rect(mx, my, minus_x, y + 2.f, btn, btn)) return -1;
		if (point_in_rect(mx, my, plus_x, y + 2.f, btn, btn)) return 1;
		return 0;
	}

	void draw_stepper(float x, float y, float w, const wchar_t* label, const wchar_t* value, D3DCOLOR accent)
	{
		renderer::draw::filled_box(x, y, w, ROW_H - 4.f, D3DCOLOR_XRGB(34, 30, 26));
		renderer::draw::draw_text(label, (int)(x + 12.f), (int)(y + 5.f), D3DCOLOR_XRGB(235, 230, 225));
		const float btn = 24.f;
		const float minus_x = x + w - btn * 2.f - 72.f;
		const float plus_x = x + w - btn - 8.f;
		renderer::draw::filled_box(minus_x, y + 2.f, btn, btn, D3DCOLOR_XRGB(50, 44, 38));
		renderer::draw::draw_text(L"-", (int)(minus_x + 6.f), (int)(y + 3.f), D3DCOLOR_XRGB(255, 255, 255));
		renderer::draw::draw_text_small(value, (int)(minus_x + btn + 10.f), (int)(y + 6.f), accent);
		renderer::draw::filled_box(plus_x, y + 2.f, btn, btn, D3DCOLOR_XRGB(50, 44, 38));
		renderer::draw::draw_text(L"+", (int)(plus_x + 6.f), (int)(y + 3.f), D3DCOLOR_XRGB(255, 255, 255));
	}

	// Visible selectable list (no click-cycling)
	float draw_list(float x, float y, float w, const wchar_t* title, const wchar_t* const* items, int count, int selected, D3DCOLOR accent, int mx, int my, bool clicked, int* out_clicked_index)
	{
		renderer::draw::draw_text_small(title, (int)x, (int)y, D3DCOLOR_XRGB(160, 150, 140));
		y += 16.f;
		const float item_h = 24.f;
		for (int i = 0; i < count; ++i)
		{
			bool sel = (i == selected);
			bool hover = point_in_rect(mx, my, x, y, w, item_h - 2.f);
			D3DCOLOR bg = sel ? accent : (hover ? D3DCOLOR_XRGB(44, 38, 32) : D3DCOLOR_XRGB(32, 28, 24));
			renderer::draw::filled_box(x, y, w, item_h - 2.f, bg);
			if (sel)
				renderer::draw::filled_box(x, y, 3.f, item_h - 2.f, D3DCOLOR_XRGB(255, 240, 220));
			renderer::draw::draw_text(items[i], (int)(x + 12.f), (int)(y + 3.f),
				sel ? D3DCOLOR_XRGB(20, 16, 12) : D3DCOLOR_XRGB(230, 225, 220));
			if (clicked && hover && out_clicked_index)
				*out_clicked_index = i;
			y += item_h;
		}
		return y + 6.f;
	}

	int hit_list(float x, float y, float w, int count, int mx, int my)
	{
		y += 16.f; // title spacer
		const float item_h = 24.f;
		for (int i = 0; i < count; ++i)
		{
			if (point_in_rect(mx, my, x, y, w, item_h - 2.f))
				return i;
			y += item_h;
		}
		return -1;
	}

	float draw_color_list(float x, float y, float w, const wchar_t* title, int selected, D3DCOLOR accent, int mx, int my, bool clicked, int* out_clicked)
	{
		renderer::draw::draw_text_small(title, (int)x, (int)y, D3DCOLOR_XRGB(160, 150, 140));
		y += 16.f;
		const float item_h = 24.f;
		for (int i = 0; i < kColorCount; ++i)
		{
			bool sel = (i == selected);
			bool hover = point_in_rect(mx, my, x, y, w, item_h - 2.f);
			D3DCOLOR bg = sel ? accent : (hover ? D3DCOLOR_XRGB(44, 38, 32) : D3DCOLOR_XRGB(32, 28, 24));
			renderer::draw::filled_box(x, y, w, item_h - 2.f, bg);
			if (sel)
				renderer::draw::filled_box(x, y, 3.f, item_h - 2.f, D3DCOLOR_XRGB(255, 240, 220));

			const float dot_cx = x + 18.f;
			const float dot_cy = y + (item_h - 2.f) * 0.5f;
			renderer::draw::filled_circle(dot_cx, dot_cy, 6.f, kColors[i].color, 14);
			renderer::draw::circle(dot_cx, dot_cy, 6.f,
				sel ? D3DCOLOR_XRGB(20, 16, 12) : D3DCOLOR_XRGB(60, 55, 50), 14);

			renderer::draw::draw_text(kColors[i].name, (int)(x + 32.f), (int)(y + 3.f),
				sel ? D3DCOLOR_XRGB(20, 16, 12) : D3DCOLOR_XRGB(230, 225, 220));
			if (clicked && hover && out_clicked)
				*out_clicked = i;
			y += item_h;
		}
		return y + 6.f;
	}
}

void esp::apply_accent_to_all()
{
	accent = kColors[accent_index % kColorCount].color;
	fov_color = accent;
	enemy_color = accent;
	enemy_color_index = accent_index % kColorCount;
}

uintptr_t esp::entity_from_handle(uintptr_t entity_list, uint32_t handle)
{
	if (!entity_list || !handle) return 0;
	const uint32_t index = handle & 0x7FFF;
	uintptr_t list_entry = memory::Read<uintptr_t>(entity_list + 0x8 * (index >> 9) + 0x10);
	if (!list_entry) return 0;
	return memory::Read<uintptr_t>(list_entry + kEntityStride * (index & 0x1FF));
}

void esp::loop()
{
	static bool seeded = false;
	if (!seeded) { srand((unsigned)time(nullptr)); seeded = true; }

	int tick = 0;
	while (renderer::running)
	{
		if (!modBase || !memory::gHandle)
		{
			Sleep(150);
			continue;
		}

		uintptr_t entity_list = memory::Read<uintptr_t>(modBase + client_dll::dwEntityList);
		if (!entity_list)
		{
			Sleep(40);
			continue;
		}

		uintptr_t local_pawn = memory::Read<uintptr_t>(modBase + client_dll::dwLocalPlayerPawn);
		BYTE local_team = local_pawn ? memory::Read<BYTE>(local_pawn + C_BaseEntity::m_iTeamNum) : 0;

		const bool full_update = (tick++ % 6) == 0; // names/weapons less often

		std::vector<EspPlayer> buffer;
		buffer.reserve(32);
		std::vector<FootPulse> new_pulses;
		static std::unordered_map<uintptr_t, DWORD> last_foot_pulse;
		static std::unordered_map<uintptr_t, EspPlayer> cache;

		const int mask_index = local_player_index; // previous frame is fine for wallcheck
		int found_local_index = 0;

		// Prefer matching local controller (more stable than pawn for mask bit)
		uintptr_t local_controller = memory::Read<uintptr_t>(modBase + client_dll::dwLocalPlayerController);

		for (int i = 1; i <= 64; ++i)
		{
			uintptr_t list_entry = memory::Read<uintptr_t>(entity_list + 0x8 * ((i & 0x7FFF) >> 9) + 0x10);
			if (!list_entry) continue;
			uintptr_t controller = memory::Read<uintptr_t>(list_entry + kEntityStride * (i & 0x1FF));
			if (!controller) continue;

			uint32_t pawn_handle = memory::Read<uint32_t>(controller + CCSPlayerController::m_hPlayerPawn);
			uintptr_t pawn = entity_from_handle(entity_list, pawn_handle);
			if (!pawn) continue;

			if (local_controller && controller == local_controller)
				found_local_index = i;
			else if (!local_controller && pawn == local_pawn)
				found_local_index = i;

			if (pawn == local_pawn)
				continue;

			BYTE team = memory::Read<BYTE>(pawn + C_BaseEntity::m_iTeamNum);
			int health = memory::Read<int32_t>(pawn + C_BaseEntity::m_iHealth);
			uint8_t life = memory::Read<uint8_t>(pawn + C_BaseEntity::m_lifeState);
			if (health <= 0 || health > 100 || life != 0) continue;

			EspPlayer player{};
			auto it = cache.find(pawn);
			if (it != cache.end())
				player = it->second;

			player.pawn = pawn;
			player.health = health;
			player.team = team;
			player.flags = memory::Read<uint32_t>(pawn + C_BaseEntity::m_fFlags);
			player.velocity = memory::Read<vec3>(pawn + C_BaseEntity::m_vecAbsVelocity);
			if (!get_pawn_origin(pawn, player.origin, player.head)) continue;

			const uintptr_t spot = pawn + C_CSPlayerPawn::m_entitySpottedState;
			player.spotted = memory::Read<bool>(spot + EntitySpottedState_t::m_bSpotted);

			// ONLY SpottedByMask bit for local player — m_bSpotted is team/radar and causes
			// aim-through-wall when a teammate saw them.
			bool raw_vis = false;
			const int idx_for_mask = (found_local_index > 0) ? found_local_index : mask_index;
			if (idx_for_mask > 0)
			{
				const int bit = idx_for_mask - 1;
				uint32_t word = memory::Read<uint32_t>(
					spot + EntitySpottedState_t::m_bSpottedByMask + ((bit >> 5) * 4));
				raw_vis = (word & (1u << (bit & 31))) != 0;
			}

			if (raw_vis)
			{
				player.invis_streak = 0;
				if (player.vis_streak < 40) ++player.vis_streak;
			}
			else
			{
				if (player.invis_streak < 40) ++player.invis_streak;
				// Drop visibility after a few misses so stale mask bits don't linger
				if (player.invis_streak >= 2)
					player.vis_streak = 0;
			}
			// Need 2 consecutive visible samples before aimable (~12ms+)
			player.visible = player.vis_streak >= 2;

			if (full_update || !player.name[0])
			{
				char name8[128]{};
				ReadProcessMemory(memory::gHandle, (LPCVOID)(controller + CBasePlayerController::m_iszPlayerName), name8, sizeof(name8) - 1, nullptr);
				utf8_to_wide(name8, player.name, 64);
				if (!player.name[0]) wcsncpy_s(player.name, L"Player", _TRUNCATE);

				uintptr_t weapon = resolve_weapon(entity_list, pawn);
				player.weapon_def = read_weapon_def(weapon);
				wcsncpy_s(player.weapon, weapon_name(player.weapon_def), _TRUNCATE);
			}

			cache[pawn] = player;
			buffer.push_back(player);

			if (esp_footsteps && (esp_teammates || !local_team || team != local_team))
			{
				float speed = std::sqrt(player.velocity.x * player.velocity.x + player.velocity.y * player.velocity.y);
				if ((player.flags & FL_ONGROUND) && speed > 100.f)
				{
					const DWORD now = GetTickCount();
					DWORD& last = last_foot_pulse[pawn];
					if (now - last >= kFootPulseCooldownMs)
					{
						last = now;
						FootPulse pulse{};
						pulse.pos = player.origin;
						pulse.life = 1.55f;
						pulse.max_radius = 52.f;
						pulse.color = D3DCOLOR_XRGB(220, 105, 30);
						new_pulses.push_back(pulse);
					}
				}
			}
		}

		if (found_local_index)
			local_player_index = found_local_index;

		// drop dead/stale cache entries occasionally
		if (full_update)
		{
			for (auto it = cache.begin(); it != cache.end();)
			{
				bool alive = false;
				for (const auto& p : buffer)
					if (p.pawn == it->first) { alive = true; break; }
				if (!alive) it = cache.erase(it);
				else ++it;
			}
		}

		{
			std::lock_guard<std::mutex> lock(players_mutex);
			players.swap(buffer);
		}
		if (!new_pulses.empty())
		{
			std::lock_guard<std::mutex> lock(pulses_mutex);
			for (auto& p : new_pulses)
				if (pulses.size() < 16) pulses.push_back(p);
		}

		Sleep(6);
	}
}

void esp::frame()
{
	__try
	{
		renderer::sync_to_game();
		renderer::handle_events();
		renderer::update_mouse();

		if (!renderer::pDevice)
			return;

		renderer::pDevice->Clear(0, nullptr, D3DCLEAR_TARGET, D3DCOLOR_XRGB(0, 0, 0), 1.f, 0);
		renderer::pDevice->BeginScene();
		render();
		renderer::pDevice->EndScene();
		renderer::present();
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		Sleep(16);
	}
}

void esp::render()
{
	// Handle startup screen first
	if (renderer::show_startup) {
		renderer::draw::draw_startup_screen();
		return; // Don't render anything else during startup
	}

	static DWORD menu_input_unlock_at = 0;
	static bool prev_menu_open = menu_open;

	static bool menu_toggle_pressed = false;
	if (GetAsyncKeyState(key_menu_toggle) & 0x8000)
	{
		if (!menu_toggle_pressed)
		{
			menu_open = !menu_open;
			menu_input_unlock_at = GetTickCount() + 160;
			menu_toggle_pressed = true;
		}
	}
	else menu_toggle_pressed = false;
	
	// Handle keybind changing
	if (changing_keybind)
	{
		for (int key = 0x01; key <= 0xFF; ++key)
		{
			if (GetAsyncKeyState(key) & 0x8000)
			{
				// Skip mouse buttons and common keys that shouldn't be used
				if (key != VK_LBUTTON && key != VK_RBUTTON && key != VK_MBUTTON && 
					key != VK_SHIFT && key != VK_CONTROL && key != VK_MENU &&
					key != key_menu_toggle && key != key_exit)
				{
					if (changing_keybind_type == 0)
						key_menu_toggle = key;
					else if (changing_keybind_type == 1)
						key_exit = key;
					changing_keybind = false;
					menu_input_unlock_at = GetTickCount() + 160;
					break;
				}
			}
		}
	}

	if (menu_open != prev_menu_open)
	{
		menu_input_unlock_at = GetTickCount() + 160;
		prev_menu_open = menu_open;
		renderer::mouse_clicked = false;
	}

	std::vector<EspPlayer> local_players;
	{
		std::lock_guard<std::mutex> lock(players_mutex);
		local_players = players;
	}

	vm = {};
	uintptr_t local_pawn = 0;
	BYTE local_team = 0;
	if (modBase)
	{
		vm = memory::Read<viewMatrix>(modBase + client_dll::dwViewMatrix);
		local_pawn = memory::Read<uintptr_t>(modBase + client_dll::dwLocalPlayerPawn);
		local_team = local_pawn ? memory::Read<BYTE>(local_pawn + C_BaseEntity::m_iTeamNum) : 0;
	}

	const float cx = renderer::game_x + WINDOW_W * 0.5f;
	const float cy = renderer::game_y + WINDOW_H * 0.5f;
	const float dt = 1.f / 60.f;

	static int last_shots = 0;
	static DWORD last_bullet_trace_ms = 0;
	if (esp_bullet_path && local_pawn)
	{
		int shots = memory::Read<int32_t>(local_pawn + C_CSPlayerPawn::m_iShotsFired);
		if (shots < last_shots)
		{
			last_shots = shots;
		}
		else if (shots > last_shots)
		{
			const DWORD now = GetTickCount();
			// One trail per shot. Ignore duplicate detections of the same bullet.
			if (now - last_bullet_trace_ms >= 20)
			{
				float pitch = 0.f, yaw = 0.f;
				read_view_angles(pitch, yaw);
				QAngle punch{};
				if (read_aim_punch(local_pawn, punch))
				{
					pitch += punch.pitch * 2.f;
					yaw += punch.yaw * 2.f;
					normalize_angles(pitch, yaw);
				}
				vec3 origin{}, head{};
				if (get_pawn_origin(local_pawn, origin, head))
				{
					vec3 fwd = angle_forward(pitch, yaw);
					BulletTrace t{};
					t.start = head;
					t.end = { head.x + fwd.x * 3800.f, head.y + fwd.y * 3800.f, head.z + fwd.z * 3800.f };
					t.end = clip_bullet_end(t.start, t.end, local_players, local_team, aim_teamcheck, vm.m, t.hit_player);
					t.life = 1.7f;
					t.color = D3DCOLOR_ARGB(255, 255, 255, 255); // white
					std::lock_guard<std::mutex> lock(traces_mutex);
					if (traces.size() >= 8)
						traces.erase(traces.begin());
					traces.push_back(t);
					last_bullet_trace_ms = now;
				}
			}
			last_shots = shots;
		}
	}

	{
		std::lock_guard<std::mutex> lock(pulses_mutex);
		for (auto& p : pulses) p.age += dt;
		pulses.erase(std::remove_if(pulses.begin(), pulses.end(), [](const FootPulse& p) { return p.age >= p.life; }), pulses.end());
	}
	{
		std::lock_guard<std::mutex> lock(traces_mutex);
		for (auto& t : traces) t.age += dt;
		traces.erase(std::remove_if(traces.begin(), traces.end(), [](const BulletTrace& t) { return t.age >= t.life; }), traces.end());
	}

	// Aimbot: mouse-only + optional RCS. Auto-part picks best damage hitbox near crosshair.
	if (aim_enabled && local_pawn && modBase)
	{
		struct AimCand
		{
			float dist = 0.f;
			float score = 0.f;
			float sx = 0.f;
			float sy = 0.f;
			uintptr_t pawn = 0;
			int hitbox = 0;
		};

		static uintptr_t locked_pawn = 0;
		static int locked_hitbox = -1;
		static int miss_frames = 0;
		static int head_ready_frames = 0;
		static AimCand last_aim{};
		static DWORD lock_lost_at = 0;

		vec3 local_origin{}, local_eye{};
		if (!get_pawn_origin(local_pawn, local_origin, local_eye))
			local_eye = local_origin;

		QAngle punch{};
		const int shots_fired = memory::Read<int32_t>(local_pawn + C_CSPlayerPawn::m_iShotsFired);
		read_aim_punch(local_pawn, punch); // always read — AR corrects view angles so aimbot benefits too

		auto try_candidate = [&](const EspPlayer& p, int hb, AimCand& out) -> bool
		{
			if (aim_teamcheck && local_team != 0 && p.team == local_team)
				return false;
			if (p.health <= 0)
				return false;
			if (aim_wallcheck && !is_visible_to_local(p))
				return false;

			vec3 aim_pos = predict_pos(p, hitbox_point(p, hb));
			float sx = 0.f, sy = 0.f;

			if (shots_fired > 0)
			{
				// If AR is NOT on, still compensate aim screen position for remaining punch
				if (!anti_recoil)
				{
					if (!aim_screen_from_world(local_eye, aim_pos, punch, true, shots_fired, sx, sy))
					{
						vec2 screen{};
						if (!w2s(aim_pos, screen, vm.m)) return false;
						sx = screen.x;
						sy = screen.y;
					}
				}
				else
				{
					// AR already corrected view angles, just use normal w2s
					vec2 screen{};
					if (!w2s(aim_pos, screen, vm.m)) return false;
					sx = screen.x;
					sy = screen.y;
				}
			}
			else
			{
				vec2 screen{};
				if (!w2s(aim_pos, screen, vm.m)) return false;
				sx = screen.x;
				sy = screen.y;
			}

			float d = dist2d(sx, sy, cx, cy);
			float fov_limit = aim_fov;
			if (local_pawn)
			{
				bool is_scoped = memory::Read<bool>(local_pawn + C_CSPlayerPawn::m_bIsScoped);
				if (is_scoped)
				{
					fov_limit = WINDOW_H * 0.45f;
				}
			}
			if (d >= fov_limit) return false;

			out = { d, 0.f, sx, sy, p.pawn, hb };
			return true;
		};

		// Ideal pick: head first, else closest other part
		auto ideal_on_player = [&](const EspPlayer& p, AimCand& best) -> bool
		{
			if (aim_auto_part)
			{
				AimCand head{};
				if (try_candidate(p, 0, head))
				{
					best = head;
					return true;
				}
				bool any = false;
				AimCand closest{};
				for (int hb = 1; hb < kHitboxCount; ++hb)
				{
					AimCand c{};
					if (!try_candidate(p, hb, c)) continue;
					if (!any || c.dist < closest.dist)
					{
						closest = c;
						any = true;
					}
				}
				if (any) best = closest;
				return any;
			}
			for (int slot = 0; slot < kHitboxCount; ++slot)
			{
				const int hb = aim_hit_order[slot];
				if (!aim_hit_enabled[hb]) continue;
				AimCand c{};
				if (!try_candidate(p, hb, c)) continue;
				best = c;
				return true;
			}
			return false;
		};

		// Sticky hitbox — only switch after sustained miss / sustained head available (kills jitter)
		auto pick_best_on_player = [&](const EspPlayer& p, AimCand& best, bool sticky) -> bool
		{
			AimCand ideal{};
			if (!ideal_on_player(p, ideal))
				return false;

			if (!aim_auto_part || !sticky || locked_hitbox < 0 || p.pawn != locked_pawn)
			{
				best = ideal;
				locked_hitbox = ideal.hitbox;
				miss_frames = 0;
				head_ready_frames = 0;
				last_aim = ideal;
				return true;
			}

			AimCand cur{};
			if (try_candidate(p, locked_hitbox, cur))
			{
				miss_frames = 0;

				if (ideal.hitbox == 0 && locked_hitbox != 0)
				{
					++head_ready_frames;
					if (head_ready_frames >= 12)
					{
						locked_hitbox = 0;
						head_ready_frames = 0;
						best = ideal;
						last_aim = ideal;
						return true;
					}
				}
				else
				{
					head_ready_frames = 0;
				}

				best = cur;
				last_aim = cur;
				return true;
			}

			++miss_frames;
			head_ready_frames = 0;
			if (miss_frames < 10 && last_aim.pawn == p.pawn)
			{
				best = last_aim;
				return true;
			}

			locked_hitbox = ideal.hitbox;
			miss_frames = 0;
			best = ideal;
			last_aim = ideal;
			return true;
		};

		AimCand chosen{};
		bool found = false;

		if (locked_pawn)
		{
			const EspPlayer* locked = nullptr;
			for (const EspPlayer& p : local_players)
				if (p.pawn == locked_pawn) { locked = &p; break; }

			if (locked && pick_best_on_player(*locked, chosen, true))
			{
				found = true;
				lock_lost_at = 0;
			}
			else
			{
				if (!lock_lost_at) lock_lost_at = GetTickCount();
				if (GetTickCount() - lock_lost_at > 120)
				{
					locked_pawn = 0;
					locked_hitbox = -1;
					miss_frames = 0;
					head_ready_frames = 0;
				}
			}
		}

		if (!found && !locked_pawn)
		{
			AimCand best{};
			bool any = false;
			for (const EspPlayer& p : local_players)
			{
				AimCand c{};
				if (!pick_best_on_player(p, c, false)) continue;
				if (!any || c.dist < best.dist)
				{
					best = c;
					any = true;
				}
			}
			if (any)
			{
				chosen = best;
				found = true;
				locked_pawn = chosen.pawn;
				locked_hitbox = chosen.hitbox;
				miss_frames = 0;
				head_ready_frames = 0;
				lock_lost_at = 0;
			}
		}

		if (!found && !locked_pawn)
		{
			locked_pawn = 0;
			locked_hitbox = -1;
			miss_frames = 0;
			head_ready_frames = 0;
		}

		if (found && ((rand() % 100) + 1) <= (int)aim_hitchance)
			aim_mouse_smooth(chosen.sx, chosen.sy, aim_smooth);
	}

	// ── Anti-Recoil (view-angle write — sensitivity-independent, works with/without aimbot) ────
	// Each frame we compute how much the punch angle changed and subtract that from
	// dwViewAngles directly. This is 100% accurate and bypasses mouse sensitivity.
	if (anti_recoil && local_pawn && !menu_open)
	{
		const int shots = memory::Read<int32_t>(local_pawn + C_CSPlayerPawn::m_iShotsFired);

		static QAngle prev_punch{};

		if (shots <= 0)
		{
			// Not firing — reset so next burst starts clean
			prev_punch = {};
		}
		else
		{
			QAngle punch{};
			if (read_aim_punch(local_pawn, punch))
			{
				// How much did the punch change this frame? (scaled x2 = actual angle applied)
				const float dp  = (punch.pitch - prev_punch.pitch) * 2.f;
				const float dy  = (punch.yaw   - prev_punch.yaw)   * 2.f;

				if (std::abs(dp) > 0.0001f || std::abs(dy) > 0.0001f)
				{
					// Read current angles, subtract the punch delta, write back
					QAngle va = memory::Read<QAngle>(modBase + client_dll::dwViewAngles);
					va.pitch -= dp;
					va.yaw   -= dy;
					// Clamp to valid range
					if (va.pitch >  89.f) va.pitch =  89.f;
					if (va.pitch < -89.f) va.pitch = -89.f;
					while (va.yaw >  180.f) va.yaw -= 360.f;
					while (va.yaw < -180.f) va.yaw += 360.f;
					memory::Write<QAngle>(modBase + client_dll::dwViewAngles, va);
				}

				prev_punch = punch;
			}
		}
	}

	// Triggerbot: click after delay when crosshair is on a valid visible part
	if (trigger_enabled && local_pawn && modBase && !menu_open)
	{
		static DWORD hover_since = 0;
		static bool fired_this_hover = false;

		bool on_target = false;
		for (const EspPlayer& p : local_players)
		{
			if (aim_teamcheck && local_team != 0 && p.team == local_team)
				continue;
			if (p.health <= 0) continue;
			if (aim_wallcheck && !is_visible_to_local(p)) continue;

			for (int hb = 0; hb < kHitboxCount; ++hb)
			{
				if (!aim_auto_part && !aim_hit_enabled[hb]) continue;
				vec2 screen{};
				if (!w2s(hitbox_point(p, hb), screen, vm.m)) continue;
				if (dist2d(screen.x, screen.y, cx, cy) <= 6.5f)
				{
					on_target = true;
					break;
				}
			}
			if (on_target) break;
		}

		const DWORD now = GetTickCount();
		if (on_target)
		{
			if (!hover_since) hover_since = now;
			const float need = std::clamp(trigger_delay, 0.f, 500.f);
			if (!fired_this_hover && (now - hover_since) >= (DWORD)need)
			{
				INPUT down{};
				down.type = INPUT_MOUSE;
				down.mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
				INPUT up{};
				up.type = INPUT_MOUSE;
				up.mi.dwFlags = MOUSEEVENTF_LEFTUP;
				SendInput(1, &down, sizeof(INPUT));
				SendInput(1, &up, sizeof(INPUT));
				fired_this_hover = true;
			}
		}
		else
		{
			hover_since = 0;
			fired_this_hover = false;
		}
	}

	if (aim_enabled && aim_draw_fov)
	{
		float r = aim_fov;
		if (local_pawn)
		{
			bool is_scoped = memory::Read<bool>(local_pawn + C_CSPlayerPawn::m_bIsScoped);
			if (is_scoped)
			{
				r = WINDOW_H * 0.45f;
			}
		}
		renderer::draw::circle(cx, cy, r, fov_color, 48, false);
	}

	if (esp_footsteps)
	{
		std::vector<FootPulse> local_pulses;
		{
			std::lock_guard<std::mutex> lock(pulses_mutex);
			local_pulses = pulses;
		}
		for (const auto& pulse : local_pulses)
			draw_foot_wave(pulse, vm.m);
	}

	if (esp_bullet_path)
	{
		std::vector<BulletTrace> local_traces;
		{
			std::lock_guard<std::mutex> lock(traces_mutex);
			local_traces = traces;
		}
		for (const auto& tr : local_traces)
			draw_bullet_path_3d(tr, vm.m);
	}

	if (esp_grenade_path && local_pawn && modBase)
	{
		const D3DCOLOR nade_col = D3DCOLOR_XRGB(245, 245, 248);
		uintptr_t entity_list = memory::Read<uintptr_t>(modBase + client_dll::dwEntityList);
		vec3 feet{}, eye{};
		get_pawn_origin(local_pawn, feet, eye);
		const float ground_z = feet.z;

		// Preview while holding a grenade
		if (entity_list)
		{
			uintptr_t weapon = resolve_weapon(entity_list, local_pawn);
			uint16_t def = read_weapon_def(weapon);
			if (weapon && is_grenade_def(def))
			{
				float pitch = 0.f, yaw = 0.f;
				if (read_view_angles(pitch, yaw))
				{
					vec3 start{}, vel{};
					setup_grenade_throw(local_pawn, weapon, pitch, yaw, start, vel);
					std::vector<vec3> path;
					vec3 land{};
					simulate_grenade_path(start, vel, ground_z, path, land);
					draw_grenade_path_viz(path, land, nade_col, vm.m);
				}
			}
		}

		// Live path for grenades already in the air — only show OUR grenades, hide after 2s post-land
		// Track per-entity land timestamps so we can fade them out 2s after landing.
		static std::unordered_map<uintptr_t, DWORD> nade_land_times;
		if (entity_list)
		{
			const DWORD now_ms = GetTickCount();
			for (int i = 64; i < 1024; ++i)
			{
				uintptr_t list_entry = memory::Read<uintptr_t>(entity_list + 0x8 * ((i & 0x7FFF) >> 9) + 0x10);
				if (!list_entry) continue;
				uintptr_t ent = memory::Read<uintptr_t>(list_entry + kEntityStride * (i & 0x1FF));
				if (!ent) continue;

				char dname[64]{};
				if (!read_entity_designer_name(ent, dname, sizeof(dname))) continue;
				if (!is_grenade_projectile_name(dname)) continue;

				// Only show grenades thrown by the local player
				{
					uint32_t thrower_handle = memory::Read<uint32_t>(ent + C_BaseGrenade::m_hThrower);
					uintptr_t thrower_pawn = esp::entity_from_handle(entity_list, thrower_handle);
					if (thrower_pawn != local_pawn) continue;
				}

				vec3 pos{}, vel{};
				uintptr_t node = memory::Read<uintptr_t>(ent + C_BaseEntity::m_pGameSceneNode);
				if (node)
					pos = memory::Read<vec3>(node + CGameSceneNode::m_vecAbsOrigin);
				else
					pos = memory::Read<vec3>(ent + C_BaseCSGrenadeProjectile::m_vInitialPosition);

				vel = memory::Read<vec3>(ent + C_BaseEntity::m_vecAbsVelocity);
				if (vel.x == 0.f && vel.y == 0.f && vel.z == 0.f)
					vel = memory::Read<vec3>(ent + C_BaseCSGrenadeProjectile::m_vInitialVelocity);

				if (pos.x == 0.f && pos.y == 0.f && pos.z == 0.f) continue;

				// Check if the grenade has stopped (landed). Speed² < 100 means it's at rest.
				const float spd2 = vel.x * vel.x + vel.y * vel.y + vel.z * vel.z;
				const bool landed = (spd2 < 100.f);

				if (landed)
				{
					// Record first time we see it at rest
					if (nade_land_times.find(ent) == nade_land_times.end())
						nade_land_times[ent] = now_ms;
					// Hide path 2 seconds after landing; also skip drawing once stopped
					if (now_ms - nade_land_times[ent] >= 2000)
						continue;
					// Grenade is at rest — no future path to draw, skip it entirely
					continue;
				}
				else
				{
					// Still in flight — remove any stale land record
					nade_land_times.erase(ent);
				}

				float gz = ground_z;
				if (pos.z < gz) gz = pos.z - 2.f;

				std::vector<vec3> path;
				vec3 land{};
				simulate_grenade_path(pos, vel, gz, path, land);
				draw_grenade_path_viz(path, land, nade_col, vm.m);
			}

			// Clean up stale entries for entities that no longer exist
			for (auto it = nade_land_times.begin(); it != nade_land_times.end(); )
			{
				if (now_ms - it->second >= 3000)
					it = nade_land_times.erase(it);
				else
					++it;
			}
		}
	}

	if (esp_enabled)
	{
		for (const EspPlayer& p : local_players)
		{
			if (!esp_teammates && local_team != 0 && p.team == local_team)
				continue;

			vec2 feet{}, head{};
			if (!w2s(p.origin, feet, vm.m) || !w2s(p.head, head, vm.m))
				continue;

			float height = std::abs(feet.y - head.y);
			if (height < 8.f) continue;
			float width = height * 0.45f;
			float left = feet.x - width * 0.5f;
			float right = feet.x + width * 0.5f;
			float top = head.y;
			float bottom = feet.y;

			bool is_enemy = (local_team == 0) || (p.team != local_team);
			D3DCOLOR col = is_enemy ? enemy_color : team_color;

			if (esp_boxes)
				renderer::draw::box(D3DXVECTOR2(left, top), D3DXVECTOR2(right, bottom), col);

			if (esp_health)
			{
				int hp = std::clamp(p.health, 0, 100);
				float hh = height * (hp / 100.f);
				renderer::draw::filled_box(left - 6.f, top, 3.f, height, D3DCOLOR_XRGB(24, 22, 20));
				D3DCOLOR hc = hp > 60 ? D3DCOLOR_XRGB(70, 210, 100) : (hp > 30 ? D3DCOLOR_XRGB(230, 180, 50) : D3DCOLOR_XRGB(230, 60, 60));
				renderer::draw::filled_box(left - 6.f, top + (height - hh), 3.f, hh, hc);
			}

			if (esp_name && p.name[0])
				renderer::draw::draw_text_name(p.name, (int)left, (int)(top - 18.f), D3DCOLOR_XRGB(245, 245, 250));

			if (esp_weapon && p.weapon[0])
				renderer::draw::draw_text_small(p.weapon, (int)left, (int)(bottom + 3.f), D3DCOLOR_XRGB(220, 210, 190));

			if (esp_snaplines)
				renderer::draw::line(D3DXVECTOR2(cx, (float)(renderer::game_y + WINDOW_H)), D3DXVECTOR2(feet.x, feet.y), col, false);

			if (esp_head_circle)
			{
				// Radius scales with the player's on-screen height so it looks consistent at any range
				const float head_r = (std::max)(3.f, height * 0.14f);
				renderer::draw::circle(head.x, head.y, head_r, col, 32, false);
			}

			if (esp_look_rays)
			{
				vec3 chest = hitbox_point(p, 2);
				vec2 c2{};
				if (w2s(chest, c2, vm.m))
					renderer::draw::line(D3DXVECTOR2(c2.x, c2.y), D3DXVECTOR2(head.x, head.y), D3DCOLOR_XRGB(200, 200, 210), false);
			}
		}
	}

	POINT mouse{ renderer::mouse_x, renderer::mouse_y };

	if (!menu_open)
	{
		renderer::draw::filled_box(14, 14, 220, 24, D3DCOLOR_XRGB(28, 24, 20));
		renderer::draw::filled_box(14, 14, 3, 24, accent);
		renderer::draw::draw_text_small(L"INSERT  menu    ·    F9  exit", 24, 18, D3DCOLOR_XRGB(210, 200, 190));
		renderer::consume_click();
		return;
	}

	MenuToggle aim_toggles[] = {
		{ L"Aimbot",          &aim_enabled },
		{ L"Triggerbot",      &trigger_enabled },
		{ L"Draw FOV Circle", &aim_draw_fov },
		{ L"Team Check",      &aim_teamcheck },
		{ L"Wallcheck",       &aim_wallcheck },
		{ L"Anti Recoil",     &anti_recoil },
	};
	MenuToggle visual_toggles[] = {
		{ L"Enable ESP", &esp_enabled },
		{ L"Boxes", &esp_boxes },
		{ L"Head Circle", &esp_head_circle },
		{ L"Health Bar", &esp_health },
		{ L"Player Name", &esp_name },
		{ L"Weapon Name", &esp_weapon },
		{ L"Snaplines", &esp_snaplines },
		{ L"Floor Footsteps", &esp_footsteps },
		{ L"Bullet Path", &esp_bullet_path },
		{ L"Grenade Path", &esp_grenade_path },
		{ L"Skeleton Hint", &esp_look_rays },
		{ L"Show Teammates", &esp_teammates },
	};

	int toggle_count = 0;
	MenuToggle* active = nullptr;
	if (menu_tab == (int)Tab::Aim) { active = aim_toggles; toggle_count = sizeof(aim_toggles) / sizeof(aim_toggles[0]); }
	else if (menu_tab == (int)Tab::Visual) { active = visual_toggles; toggle_count = sizeof(visual_toggles) / sizeof(visual_toggles[0]); }

	// Rough content height for list-heavy tabs
	float content_extra = 0.f;
	if (menu_tab == (int)Tab::Aim) content_extra = 4 * ROW_H + 24.f + kHitboxCount * 30.f + 36.f;
	else if (menu_tab == (int)Tab::Visual) content_extra = 16.f + kColorCount * 24.f + 10.f; // Reduced height
	else content_extra = 100.f + kColorCount * 24.f + 150.f; // Keybinds section + accent + config (increased height)

	const float tabs_y = menu_y + HEADER_H;
	const float body_top = tabs_y + TAB_H + 14.f;
	const float full_h = HEADER_H + TAB_H + 14.f + toggle_count * ROW_H + content_extra + 40.f;
	const float menu_h = menu_minimized ? HEADER_H : full_h;

	const bool input_ready = GetTickCount() >= menu_input_unlock_at;
	const bool clicked = input_ready && renderer::consume_click();

	static bool dragging = false;
	static float drag_ox = 0.f, drag_oy = 0.f;
	const float btn_x = menu_x + MENU_W - 44.f;
	const float btn_y = menu_y + 14.f;
	const bool on_min = point_in_rect(mouse.x, mouse.y, btn_x, btn_y, 28.f, 24.f);
	const bool on_header = point_in_rect(mouse.x, mouse.y, menu_x, menu_y, MENU_W - 50.f, HEADER_H);

	if (clicked && on_min)
		menu_minimized = !menu_minimized;

	if (renderer::mouse_down && on_header && !on_min && !dragging && input_ready)
	{
		dragging = true;
		drag_ox = mouse.x - menu_x;
		drag_oy = mouse.y - menu_y;
	}
	if (!renderer::mouse_down) dragging = false;
	if (dragging)
	{
		menu_x = std::clamp(mouse.x - drag_ox, 0.f, (float)renderer::screen_w - MENU_W);
		menu_y = std::clamp(mouse.y - drag_oy, 0.f, (float)renderer::screen_h - HEADER_H);
	}

	const float mx = menu_x;
	const float my = menu_y;
	const float bx = mx + MENU_W - 44.f;
	const float by = my + 14.f;

	if (clicked && !menu_minimized && !dragging &&
		point_in_rect(mouse.x, mouse.y, mx, my, MENU_W, menu_h) &&
		!point_in_rect(mouse.x, mouse.y, bx, by, 28.f, 24.f))
	{
		const float tw = MENU_W / 3.f;
		const float ty = my + HEADER_H;
		for (int t = 0; t < 3; ++t)
			if (point_in_rect(mouse.x, mouse.y, mx + t * tw, ty, tw, TAB_H))
				menu_tab = t;

		const float bt = ty + TAB_H + 14.f;
		if (active)
		{
			for (int i = 0; i < toggle_count; ++i)
			{
				float y = bt + i * ROW_H;
				if (point_in_rect(mouse.x, mouse.y, mx + PAD, y, MENU_W - PAD * 2.f, ROW_H - 4.f))
					*active[i].value = !*active[i].value;
			}
		}

		if (menu_tab == (int)Tab::Aim)
		{
			float y0 = bt + toggle_count * ROW_H;
			int h = hit_stepper(mx + PAD, y0, MENU_W - PAD * 2.f, mouse.x, mouse.y);
			if (h) aim_fov = std::clamp(aim_fov + h * 10.f, 20.f, 500.f);
			h = hit_stepper(mx + PAD, y0 + ROW_H, MENU_W - PAD * 2.f, mouse.x, mouse.y);
			if (h) aim_smooth = std::clamp(aim_smooth + h * 0.5f, 1.f, 25.f);
			h = hit_stepper(mx + PAD, y0 + ROW_H * 2.f, MENU_W - PAD * 2.f, mouse.x, mouse.y);
			if (h) aim_hitchance = std::clamp(aim_hitchance + h * 5.f, 5.f, 100.f);
			h = hit_stepper(mx + PAD, y0 + ROW_H * 3.f, MENU_W - PAD * 2.f, mouse.x, mouse.y);
			if (h) trigger_delay = std::clamp(trigger_delay + h * 10.f, 0.f, 500.f);

			const float list_y = y0 + ROW_H * 4.f + 18.f;
			const float row_h = 28.f;
			const float list_w = MENU_W - PAD * 2.f;
			if (!aim_auto_part)
			{
				for (int i = 0; i < kHitboxCount; ++i)
				{
					float ry = list_y + i * row_h;
					const float toggle_w = list_w - 64.f;
					if (point_in_rect(mouse.x, mouse.y, mx + PAD, ry, toggle_w, row_h - 3.f))
					{
						const int hb = aim_hit_order[i];
						aim_hit_enabled[hb] = !aim_hit_enabled[hb];
					}
					const float up_x = mx + PAD + list_w - 58.f;
					const float dn_x = mx + PAD + list_w - 28.f;
					if (point_in_rect(mouse.x, mouse.y, up_x, ry + 2.f, 24.f, 20.f) && i > 0)
						std::swap(aim_hit_order[i], aim_hit_order[i - 1]);
					if (point_in_rect(mouse.x, mouse.y, dn_x, ry + 2.f, 24.f, 20.f) && i < kHitboxCount - 1)
						std::swap(aim_hit_order[i], aim_hit_order[i + 1]);
				}
			}
		}
		else if (menu_tab == (int)Tab::Visual)
		{
			float y0 = bt + toggle_count * ROW_H + 4.f;
			float col_w = (MENU_W - PAD * 2.f) * 0.48f;
			int picked = hit_list(mx + PAD, y0, col_w, kColorCount, mouse.x, mouse.y);
			if (picked >= 0) { enemy_color_index = picked; enemy_color = kColors[picked].color; }
			picked = hit_list(mx + PAD + col_w + (MENU_W - PAD * 2.f) * 0.04f, y0, col_w, kColorCount, mouse.x, mouse.y);
			if (picked >= 0) { team_color_index = picked; team_color = kColors[picked].color; }
		}
		else if (menu_tab == (int)Tab::Settings)
		{
			float y0 = bt;
			float keybind_h = 28.f;
			float keybind_w = MENU_W - PAD * 2.f;
			
			// Menu Toggle keybind click
			if (point_in_rect(mouse.x, mouse.y, mx + PAD, y0 + 20.f, keybind_w, keybind_h - 4.f) && clicked)
			{
				changing_keybind = true;
				changing_keybind_type = 0;
			}
			y0 += keybind_h;
			
			// Exit keybind click
			if (point_in_rect(mouse.x, mouse.y, mx + PAD, y0 + 20.f, keybind_w, keybind_h - 4.f) && clicked)
			{
				changing_keybind = true;
				changing_keybind_type = 1;
			}
			
			y0 = bt + 80.f; // Skip keybinds section for accent color
			int picked = hit_list(mx + PAD, y0, MENU_W - PAD * 2.f, kColorCount, mouse.x, mouse.y);
			if (picked >= 0)
			{
				accent_index = picked;
				accent = kColors[picked].color;
			}
			float apply_y = y0 + 16.f + kColorCount * 24.f + 10.f;
			if (point_in_rect(mouse.x, mouse.y, mx + PAD, apply_y, MENU_W - PAD * 2.f, 30.f))
				apply_accent_to_all();

			float config_btn_y = apply_y + 56.f;
			const float btn_h = 30.f;
			const float gap = 8.f;
			if (point_in_rect(mouse.x, mouse.y, mx + PAD, config_btn_y, MENU_W - PAD * 2.f, btn_h))
				config::save_last();
			config_btn_y += btn_h + gap;
			if (point_in_rect(mouse.x, mouse.y, mx + PAD, config_btn_y, MENU_W - PAD * 2.f, btn_h))
				config::export_dialog(renderer::overlay_hwnd);
			config_btn_y += btn_h + gap;
			if (point_in_rect(mouse.x, mouse.y, mx + PAD, config_btn_y, MENU_W - PAD * 2.f, btn_h))
				config::import_dialog(renderer::overlay_hwnd);
		}
	}

	// —— Draw panel ——
	renderer::draw::filled_box(mx - 1.f, my - 1.f, MENU_W + 2.f, menu_h + 2.f, D3DCOLOR_XRGB(40, 32, 24));
	renderer::draw::filled_box(mx, my, MENU_W, menu_h, D3DCOLOR_XRGB(26, 22, 18));
	renderer::draw::filled_box(mx, my, MENU_W, HEADER_H, accent);
	renderer::draw::filled_box(mx, my + HEADER_H - 2.f, MENU_W, 2.f, D3DCOLOR_XRGB(255, 220, 180));

	// Draw logo to the left of BearWare title (WHITE color)
	if (renderer::pLogoTexture) {
		renderer::draw::draw_texture(renderer::pLogoTexture, mx + PAD, my + 6.f, 32.f, 32.f, D3DCOLOR_XRGB(255, 255, 255));
	}
	else {
		// Fallback: draw white circle if texture loading failed
		renderer::draw::filled_circle(mx + PAD + 16.f, my + 22.f, 14.f, D3DCOLOR_XRGB(255, 255, 255), 16);
	}

	// BearWare title - positioned after logo
	renderer::draw::draw_text_title(L"BearWare", (int)(mx + PAD + 40.f), (int)(my + 8.f), D3DCOLOR_XRGB(255, 255, 255));
	// Always show CS2 EXTERNAL UNDETECTED + STREAMABLE under the title
	renderer::draw::draw_text_small(L"CS2 EXTERNAL UNDETECTED + STREAMABLE",
		(int)(mx + PAD + 40.f), (int)(my + 28.f), D3DCOLOR_XRGB(255, 235, 210));

	// Add minimize button back (using existing btn_x, btn_y from above)
	bool min_hov = point_in_rect(mouse.x, mouse.y, btn_x, btn_y, 28.f, 24.f);
	renderer::draw::filled_box(btn_x, btn_y, 28.f, 24.f, min_hov ? D3DCOLOR_XRGB(40, 34, 28) : D3DCOLOR_XRGB(28, 24, 20));
	if (menu_minimized)
	{
		renderer::draw::box(D3DXVECTOR2(btn_x + 8.f, btn_y + 7.f), D3DXVECTOR2(btn_x + 20.f, btn_y + 17.f), D3DCOLOR_XRGB(255, 255, 255));
	}
	else
	{
		renderer::draw::filled_box(btn_x + 7.f, btn_y + 14.f, 14.f, 2.f, D3DCOLOR_XRGB(255, 255, 255));
	}

	if (menu_minimized)
		return;

	const float tw = MENU_W / 3.f;
	const float ty = my + HEADER_H;
	const float bt = ty + TAB_H + 14.f;
	const wchar_t* tabs[3] = { L"AIM", L"VISUAL", L"SETTINGS" };
	for (int t = 0; t < 3; ++t)
	{
		bool sel = menu_tab == t;
		bool hov = point_in_rect(mouse.x, mouse.y, mx + t * tw, ty, tw, TAB_H);
		D3DCOLOR bg = sel ? D3DCOLOR_XRGB(36, 30, 24) : (hov ? D3DCOLOR_XRGB(32, 28, 24) : D3DCOLOR_XRGB(22, 20, 17));
		renderer::draw::filled_box(mx + t * tw, ty, tw - 1.f, TAB_H, bg);
		if (sel)
			renderer::draw::filled_box(mx + t * tw + 12.f, ty + TAB_H - 3.f, tw - 25.f, 2.f, accent);
		renderer::draw::draw_text_tab(tabs[t], (int)(mx + t * tw + 28.f), (int)(ty + 8.f),
			sel ? D3DCOLOR_XRGB(255, 245, 235) : D3DCOLOR_XRGB(170, 160, 150));
	}

	if (menu_tab == (int)Tab::Aim && active)
	{
		for (int i = 0; i < toggle_count; ++i)
		{
			float y = bt + i * ROW_H;
			bool hov = point_in_rect(mouse.x, mouse.y, mx + PAD, y, MENU_W - PAD * 2.f, ROW_H - 4.f);
			draw_toggle(mx + PAD, y, MENU_W - PAD * 2.f, active[i].label, *active[i].value, hov, accent);
		}
		wchar_t a[32], b[32], c[32], d[32];
		swprintf_s(a, L"%.0f", aim_fov);
		swprintf_s(b, L"%.1f", aim_smooth);
		swprintf_s(c, L"%.0f%%", aim_hitchance);
		swprintf_s(d, L"%.0f ms", trigger_delay);
		float y0 = bt + toggle_count * ROW_H;
		draw_stepper(mx + PAD, y0, MENU_W - PAD * 2.f, L"FOV Size", a, accent);
		draw_stepper(mx + PAD, y0 + ROW_H, MENU_W - PAD * 2.f, L"Smooth", b, accent);
		draw_stepper(mx + PAD, y0 + ROW_H * 2.f, MENU_W - PAD * 2.f, L"Hit Chance", c, accent);
		draw_stepper(mx + PAD, y0 + ROW_H * 3.f, MENU_W - PAD * 2.f, L"Trigger Delay", d, accent);

		const float list_y = y0 + ROW_H * 4.f + 4.f;
		const float rows_y = list_y + 16.f;
		const float row_h = 28.f;
		const float list_w = MENU_W - PAD * 2.f;
		for (int i = 0; i < kHitboxCount; ++i)
		{
			const int hb = aim_hit_order[i];
			const bool on = aim_hit_enabled[hb];
			float ry = rows_y + i * row_h;
			bool hov = point_in_rect(mouse.x, mouse.y, mx + PAD, ry, list_w - 64.f, row_h - 3.f);

			D3DCOLOR bg = on ? accent : (hov ? D3DCOLOR_XRGB(44, 38, 32) : D3DCOLOR_XRGB(32, 28, 24));
			if (aim_auto_part)
				bg = hov ? D3DCOLOR_XRGB(44, 38, 32) : D3DCOLOR_XRGB(32, 28, 24);
			renderer::draw::filled_box(mx + PAD, ry, list_w - 64.f, row_h - 3.f, bg);
			wchar_t label[48];
			swprintf_s(label, L"%d.  %s", i + 1, kHitboxes[hb]);
			renderer::draw::draw_text(label, (int)(mx + PAD + 12.f), (int)(ry + 4.f),
				(on && !aim_auto_part) ? D3DCOLOR_XRGB(20, 16, 12) : D3DCOLOR_XRGB(230, 225, 220));

			const float up_x = mx + PAD + list_w - 58.f;
			const float dn_x = mx + PAD + list_w - 28.f;
			bool up_hov = point_in_rect(mouse.x, mouse.y, up_x, ry + 2.f, 24.f, 20.f);
			bool dn_hov = point_in_rect(mouse.x, mouse.y, dn_x, ry + 2.f, 24.f, 20.f);
			renderer::draw::filled_box(up_x, ry + 2.f, 24.f, 20.f, up_hov ? accent : D3DCOLOR_XRGB(48, 42, 36));
			renderer::draw::filled_box(dn_x, ry + 2.f, 24.f, 20.f, dn_hov ? accent : D3DCOLOR_XRGB(48, 42, 36));
			renderer::draw::draw_text(L"^", (int)(up_x + 7.f), (int)(ry + 2.f), D3DCOLOR_XRGB(255, 255, 255));
			renderer::draw::draw_text(L"v", (int)(dn_x + 7.f), (int)(ry + 2.f), D3DCOLOR_XRGB(255, 255, 255));
		}
	}
	else if (menu_tab == (int)Tab::Visual && active)
	{
		for (int i = 0; i < toggle_count; ++i)
		{
			float y = bt + i * ROW_H;
			bool hov = point_in_rect(mouse.x, mouse.y, mx + PAD, y, MENU_W - PAD * 2.f, ROW_H - 4.f);
			draw_toggle(mx + PAD, y, MENU_W - PAD * 2.f, active[i].label, *active[i].value, hov, accent);
		}
		float y0 = bt + toggle_count * ROW_H + 4.f;
		float col_w = (MENU_W - PAD * 2.f) * 0.48f;
		draw_color_list(mx + PAD, y0, col_w, L"ENEMY COLOR", enemy_color_index, accent, mouse.x, mouse.y, false, nullptr);
		draw_color_list(mx + PAD + col_w + (MENU_W - PAD * 2.f) * 0.04f, y0, col_w, L"TEAM COLOR", team_color_index, accent, mouse.x, mouse.y, false, nullptr);
	}
	else
	{
		float y0 = bt;
		
		// Keybinds section
		renderer::draw::draw_text_small(L"KEYBINDS", (int)(mx + PAD), (int)y0, D3DCOLOR_XRGB(160, 150, 140));
		y0 += 20.f;
		
		float keybind_h = 28.f;
		float keybind_w = MENU_W - PAD * 2.f;
		
		// Menu Toggle keybind
		bool menu_toggle_hov = point_in_rect(mouse.x, mouse.y, mx + PAD, y0, keybind_w, keybind_h - 4.f);
		renderer::draw::filled_box(mx + PAD, y0, keybind_w, keybind_h - 4.f, (changing_keybind && changing_keybind_type == 0) ? accent : (menu_toggle_hov ? D3DCOLOR_XRGB(44, 38, 32) : D3DCOLOR_XRGB(34, 30, 26)));
		renderer::draw::draw_text(L"Menu Toggle", (int)(mx + PAD + 12.f), (int)(y0 + 5.f), D3DCOLOR_XRGB(235, 230, 225));
		
		wchar_t key_name[32];
		if (changing_keybind && changing_keybind_type == 0)
			swprintf_s(key_name, L"PRESS KEY...");
		else
			swprintf_s(key_name, L"%s", key_menu_toggle == VK_INSERT ? L"INSERT" : key_menu_toggle == VK_F9 ? L"F9" : key_menu_toggle == VK_F1 ? L"F1" : key_menu_toggle == VK_F2 ? L"F2" : key_menu_toggle == VK_F3 ? L"F3" : key_menu_toggle == VK_F4 ? L"F4" : key_menu_toggle == VK_F5 ? L"F5" : key_menu_toggle == VK_F6 ? L"F6" : key_menu_toggle == VK_F7 ? L"F7" : key_menu_toggle == VK_F8 ? L"F8" : key_menu_toggle == VK_F10 ? L"F10" : key_menu_toggle == VK_F11 ? L"F11" : key_menu_toggle == VK_F12 ? L"F12" : L"UNKNOWN");
		renderer::draw::draw_text_small(key_name, (int)(mx + PAD + keybind_w - 80.f), (int)(y0 + 6.f), (changing_keybind && changing_keybind_type == 0) ? D3DCOLOR_XRGB(20, 16, 12) : accent);
		y0 += keybind_h;
		
		// Exit keybind
		bool exit_hov = point_in_rect(mouse.x, mouse.y, mx + PAD, y0, keybind_w, keybind_h - 4.f);
		renderer::draw::filled_box(mx + PAD, y0, keybind_w, keybind_h - 4.f, (changing_keybind && changing_keybind_type == 1) ? accent : (exit_hov ? D3DCOLOR_XRGB(44, 38, 32) : D3DCOLOR_XRGB(34, 30, 26)));
		renderer::draw::draw_text(L"Exit", (int)(mx + PAD + 12.f), (int)(y0 + 5.f), D3DCOLOR_XRGB(235, 230, 225));
		
		if (changing_keybind && changing_keybind_type == 1)
			swprintf_s(key_name, L"PRESS KEY...");
		else
			swprintf_s(key_name, L"%s", key_exit == VK_INSERT ? L"INSERT" : key_exit == VK_F9 ? L"F9" : key_exit == VK_F1 ? L"F1" : key_exit == VK_F2 ? L"F2" : key_exit == VK_F3 ? L"F3" : key_exit == VK_F4 ? L"F4" : key_exit == VK_F5 ? L"F5" : key_exit == VK_F6 ? L"F6" : key_exit == VK_F7 ? L"F7" : key_exit == VK_F8 ? L"F8" : key_exit == VK_F10 ? L"F10" : key_exit == VK_F11 ? L"F11" : key_exit == VK_F12 ? L"F12" : L"UNKNOWN");
		renderer::draw::draw_text_small(key_name, (int)(mx + PAD + keybind_w - 80.f), (int)(y0 + 6.f), (changing_keybind && changing_keybind_type == 1) ? D3DCOLOR_XRGB(20, 16, 12) : accent);
		y0 += keybind_h + 10.f;
		
		draw_color_list(mx + PAD, y0, MENU_W - PAD * 2.f, L"ACCENT COLOR", accent_index, accent, mouse.x, mouse.y, false, nullptr);
		float apply_y = y0 + 16.f + kColorCount * 24.f + 10.f;
		bool hov = point_in_rect(mouse.x, mouse.y, mx + PAD, apply_y, MENU_W - PAD * 2.f, 30.f);
		renderer::draw::filled_box(mx + PAD, apply_y, MENU_W - PAD * 2.f, 30.f, hov ? accent : D3DCOLOR_XRGB(40, 34, 28));
		renderer::draw::draw_text(L"Apply Accent To All", (int)(mx + PAD + 14.f), (int)(apply_y + 6.f), D3DCOLOR_XRGB(255, 255, 255));

		float draw_btn_y = apply_y + 56.f;
		const float btn_h = 30.f;
		const float gap = 8.f;
		const wchar_t* labels[3] = { L"Save Config", L"Export Config...", L"Import Config..." };
		for (int i = 0; i < 3; ++i)
		{
			bool bh = point_in_rect(mouse.x, mouse.y, mx + PAD, draw_btn_y, MENU_W - PAD * 2.f, btn_h);
			renderer::draw::filled_box(mx + PAD, draw_btn_y, MENU_W - PAD * 2.f, btn_h, bh ? accent : D3DCOLOR_XRGB(40, 34, 28));
			renderer::draw::draw_text(labels[i], (int)(mx + PAD + 14.f), (int)(draw_btn_y + 6.f), D3DCOLOR_XRGB(255, 255, 255));
			draw_btn_y += btn_h + gap;
		}

		const wchar_t* st = config::status_text();
		if (st && st[0])
			renderer::draw::draw_text_small(st, (int)(mx + PAD), (int)draw_btn_y, accent);
	}
}

bool esp::w2s(const vec3& world, vec2& screen, float m[16])
{
	float w = world.x * m[12] + world.y * m[13] + world.z * m[14] + m[15];
	if (w < 0.01f) return false;
	float x = world.x * m[0] + world.y * m[1] + world.z * m[2] + m[3];
	float y = world.x * m[4] + world.y * m[5] + world.z * m[6] + m[7];
	screen.x = renderer::game_x + (WINDOW_W * 0.5f) + (WINDOW_W * 0.5f) * (x / w);
	screen.y = renderer::game_y + (WINDOW_H * 0.5f) - (WINDOW_H * 0.5f) * (y / w);
	return !(std::isnan(screen.x) || std::isnan(screen.y) || std::isinf(screen.x) || std::isinf(screen.y));
}
