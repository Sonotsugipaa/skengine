#pragma once

#include <SDL2/SDL_keyboard.h>
#include <SDL2/SDL_mouse.h>

#include <type_traits>



namespace SKENGINE_NAME_NS {
inline namespace input {

	using input_id_e = uint_fast64_t;
	enum class InputId : input_id_e { };
	constexpr auto operator|(InputId l, InputId r) noexcept { using Ut = input_id_e; return InputId(Ut(l) | Ut(r)); }
	constexpr auto operator&(InputId l, InputId r) noexcept { using Ut = input_id_e; return InputId(Ut(l) & Ut(r)); }


	#define INPUT_ID_V_(NM_, V_) constexpr auto NM_ = InputId(V_);
	INPUT_ID_V_(inputIdDeviceMask, 0xff000000'00000000)
	INPUT_ID_V_(inputIdValueMask,  0x00ffffff'ffffffff)
	INPUT_ID_V_(inputIdDeviceKeyboard, 0x01'000000'00000000)
	INPUT_ID_V_(inputIdDeviceMouse,    0x02'000000'00000000)
	#undef INPUT_ID_V_
	constexpr auto inputIdDeviceMaskBits = input_id_e(8);
	constexpr auto inputIdValueMaskBits  = input_id_e(64 - inputIdDeviceMaskBits);


	constexpr InputId inputIdFromSdlKey(SDL_KeyCode key) {
		return inputIdDeviceKeyboard | InputId(key & input_id_e(inputIdValueMask));
	}

	constexpr InputId inputIdFromSdlMouse(unsigned key) {
		return inputIdDeviceMouse | InputId(key & input_id_e(inputIdValueMask));
	}

}}
