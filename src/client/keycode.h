// Luanti
// SPDX-License-Identifier: LGPL-2.1-or-later
// Copyright (C) 2010-2013 celeron55, Perttu Ahola <celeron55@gmail.com>

#pragma once

#include "irrlichttypes.h"
#include <Keycodes.h>
#include <IEventReceiver.h>
#include <string>
#include <variant>

/* A key press, consisting of a scancode or a keycode.
 * This fits into 64 bits, so prefer passing this by value.
*/
class KeyPress
{
public:
	KeyPress() = default;

	KeyPress(const std::string &name);

	KeyPress(const irr::SEvent::SKeyInput &in);

	// Get a string representation that is suitable for use in minetest.conf
	std::string sym() const;

	// Get a human-readable string representation
	std::string name() const;

	// Get the corresponding keycode or KEY_UNKNOWN if one is not available
	irr::EKEY_CODE getKeycode() const;

	// Get the corresponding keychar or '\0' if one is not available
	wchar_t getKeychar() const;

	// Get the scancode or 0 is one is not available
	u32 getScancode() const
	{
		if (auto pv = std::get_if<u32>(&scancode))
			return *pv;
		return 0;
	}

	bool operator==(KeyPress o) const {
		return scancode == o.scancode;
	}
	bool operator!=(KeyPress o) const {
		return !(*this == o);
	}

	// Used for e.g. std::set
	bool operator<(KeyPress o) const {
		return scancode < o.scancode;
	}

	// Check whether the keypress is valid
	operator bool() const
	{
		return std::holds_alternative<irr::EKEY_CODE>(scancode) ?
			Keycode::isValid(std::get<irr::EKEY_CODE>(scancode)) :
			std::get<u32>(scancode) != 0;
	}

	static KeyPress getSpecialKey(const std::string &name);

private:
	using value_type = std::variant<u32, irr::EKEY_CODE>;
	bool loadFromScancode(const std::string &name);
	void loadFromKey(irr::EKEY_CODE keycode, wchar_t keychar);
	std::string formatScancode() const;

	value_type scancode = irr::KEY_UNKNOWN;

	friend std::hash<KeyPress>;
};

template <>
struct std::hash<KeyPress>
{
	size_t operator()(KeyPress kp) const noexcept {
		return std::hash<KeyPress::value_type>{}(kp.scancode);
	}
};

// Key Actions
enum class GameKeyType
{
	FORWARD = 0,
	BACKWARD,
	LEFT,
	RIGHT,
	JUMP,
	AUX1,
	SNEAK,
	DIG,
	PLACE,
	ESC,
	AUTOFORWARD,
	DROP,
	INVENTORY,
	CHAT,
	CMD,
	CMD_LOCAL,
	CONSOLE,
	MINIMAP,
	FREEMOVE,
	PITCHMOVE,
	FASTMOVE,
	NOCLIP,
	HOTBAR_PREV,
	HOTBAR_NEXT,
	MUTE,
	INC_VOLUME,
	DEC_VOLUME,
	CINEMATIC,
	SCREENSHOT,
	TOGGLE_BLOCK_BOUNDS,
	TOGGLE_HUD,
	TOGGLE_CHAT,
	TOGGLE_FOG,
	TOGGLE_UPDATE_CAMERA,
	TOGGLE_DEBUG,
	TOGGLE_PROFILER,
	CAMERA_MODE,
	INCREASE_VIEWING_RANGE,
	DECREASE_VIEWING_RANGE,
	RANGESELECT,
	ZOOM,
	QUICKTUNE_NEXT,
	QUICKTUNE_PREV,
	QUICKTUNE_INC,
	QUICKTUNE_DEC,
	SLOT_1,
	SLOT_2,
	SLOT_3,
	SLOT_4,
	SLOT_5,
	SLOT_6,
	SLOT_7,
	SLOT_8,
	SLOT_9,
	SLOT_10,
	SLOT_11,
	SLOT_12,
	SLOT_13,
	SLOT_14,
	SLOT_15,
	SLOT_16,
	SLOT_17,
	SLOT_18,
	SLOT_19,
	SLOT_20,
	SLOT_21,
	SLOT_22,
	SLOT_23,
	SLOT_24,
	SLOT_25,
	SLOT_26,
	SLOT_27,
	SLOT_28,
	SLOT_29,
	SLOT_30,
	SLOT_31,
	SLOT_32,
	VOICE_CHAT,

	INTERNAL_ENUM_COUNT
};

// Global defines for convenience
// This implementation defers creation of the objects to make sure that the
// IrrlichtDevice is initialized.
#define EscapeKey KeyPress::getSpecialKey("KEY_ESCAPE")
#define LMBKey KeyPress::getSpecialKey("KEY_LBUTTON")
#define MMBKey KeyPress::getSpecialKey("KEY_MBUTTON") // Middle Mouse Button
#define RMBKey KeyPress::getSpecialKey("KEY_RBUTTON")

// Key configuration getter
KeyPress getKeySetting(const std::string &settingname);

// Clear fast lookup cache
void clearKeyCache();
