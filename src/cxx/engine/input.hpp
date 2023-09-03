#pragma once

#include <skengine_fwd.hpp>

#include <functional>
#include <stdexcept>
#include <deque>
#include <vector>
#include <string>
#include <string_view>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include <SDL2/SDL_events.h>



/// \file
///
/// \remark This header contains the term "Key" used in many places:
///         it's important to note that it is used as "key of a map",
///         *not* "key of a keyboard".
///



namespace SKENGINE_NAME_NS::input {

	/// - bit 1: just switched from active to inactive, or viceversa
	/// - bit 2: state is now active
	/// - bit 4: non-binary value is not 0
	///
	enum class InputState : uint_least8_t {
		eInactive    = 0b000,
		eDeactivated = 0b001,
		eActive      = 0b010,
		eActivated   = 0b011,
		eAnalog      = 0b110
	};


	enum InputValue : uint_least8_t {
		input_value_inactive = 0,
		input_value_active   = 1,
		input_value_max      = 0xff
	};


	enum class InputId : uint_fast64_t { };

	enum class CommandId : uint_fast32_t { };


	struct InputMapKey {
		InputState state;
		InputId    id;

		bool operator<(InputMapKey rh) const noexcept {
			return (state < rh.state) || (state == rh.state && id < rh.id);
		}
	};


	struct InputMapHash {
		std::size_t operator()(const InputMapKey& k) const noexcept {
			using Rt = uint_fast64_t;
			constexpr Rt mask32 = Rt(0xffff);
			return
				(((Rt(k.id) & mask32) - ((Rt(k.id) >> Rt(32)) & mask32)) << Rt(3)) |
				Rt(k.state);
		}
	};


	class Context {
	public:
		/// \brief If A is a substring of B, B is a subcontext of A.
		///
		bool isSubcontextOf(const Context&)     const noexcept;
		bool isSubcontextOf(const std::string&) const noexcept;
		bool isSubcontextOf(const char*)        const noexcept;

		bool operator<(const Context&) const noexcept;

		const std::string& string() const noexcept { return mId; };

	private:
		std::string mId;
	};


	using CommandCallback = std::function<void (InputState, InputId, InputValue)>;


	struct Binding {
		std::deque<InputMapKey> keys;
		std::deque<Context>     contexts;
	};


	struct ConcurrentRemovalError : std::logic_error { using std::logic_error::logic_error; };


	class InputManager {
	public:
		CommandId addCommand    (CommandCallback);
		void      removeCommand (CommandId);
		void      bindCommand   (CommandId, const Binding&);

		bool feedSdlEvent           (const SDL_Event&);
		void triggerRepeatingInputs ();

		void clear() noexcept;

	private:
		#define UMAP_ std::unordered_map
		#define MAP_  std::map
		using ContextMap   = MAP_<Context,          CommandId>;
		using ContextCache = MAP_<std::string_view, CommandId>;
		UMAP_<InputMapKey, ContextMap,   InputMapHash> mBindings;
		UMAP_<InputMapKey, ContextCache, InputMapHash> mBindingsCache;
		UMAP_<CommandId,   CommandCallback>            mCommands;
		std::unordered_set<InputMapKey, InputMapHash>  mRepeatingInputs;
		#undef MAP_
		#undef UMAP_
	};

}
