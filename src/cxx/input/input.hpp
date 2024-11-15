#pragma once

#include <skengine_fwd.hpp>

#include <idgen.hpp>

#include "input_id.hpp"

#include <stdexcept>
#include <bit>
#include <memory>
#include <deque>
#include <ranges>
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



namespace SKENGINE_NAME_NS {
inline namespace input {

	using input_state_e = uint_least8_t;
	/// - bit 1: just switched from active to inactive, or viceversa
	/// - bit 2: state is now active
	/// - bit 4: non-binary value is not 0
	///
	enum class InputState : input_state_e {
		eInactive     = 0b000,
		eDeactivated  = 0b001,
		eActive       = 0b010,
		eActivated    = 0b011,
		eAnalog       = 0b110
	};
	constexpr auto operator|(InputState l, InputState r) noexcept { return InputState(input_state_e(l) | input_state_e(r)); }
	constexpr auto operator&(InputState l, InputState r) noexcept { return InputState(input_state_e(l) & input_state_e(r)); }
	constexpr auto operator~(InputState s) noexcept { return InputState(~ input_state_e(s)); }
	constexpr bool inputStateCurrentlyActive(InputState s) noexcept { return InputState(0) != (s & InputState::eActive); }
	constexpr bool inputStateChanged(InputState s) noexcept { return InputState(0) != (s & InputState::eDeactivated); }
	constexpr bool inputStateIsNonbinary(InputState s) noexcept { return InputState(0) != (InputState(input_state_e(s) >> input_state_e(2)) & InputState(1)); }


	using input_value_e = uint_least8_t;
	enum class InputValue : input_value_e {
		eInactive = 0x00,
		eActive   = 0xff
	};


	enum class CommandId : uint_fast32_t { };


	struct InputMapKey {
		InputId    id;
		InputState state;

		bool operator<(const InputMapKey& rh) const noexcept {
			return (state < rh.state) || (state == rh.state && id < rh.id);
		}

		bool operator==(const InputMapKey& rh) const noexcept { return (id == rh.id) && (state == rh.state); }
	};


	struct InputMapHash {
		std::size_t operator()(const InputMapKey& k) const noexcept {
			using Rt = uint_fast64_t;
			return
				Rt(k.id) ^
				std::rotr(
					Rt(k.state),
					inputIdDeviceMaskBits + 3 /* avoid XORing into the ID's device bits, and alter those that are very likely zeroes */ );
		}
	};


	struct Input {
		InputId    id;
		InputState state;
		InputValue value;
	};


	class Context {
	public:
		enum class Cmp { eDifferent = 0, eRightIsSubcontext = 1, eLeftIsSubcontext, eSame = 3 };

		Context() = default;
		Context(std::string);

		/// \brief If A is a substring of B, B is a subcontext of A.
		///
		static Cmp compareContexts(std::string_view, std::string_view) noexcept;

		/// \fn bool operator<(std::string_view) const noexcept
		/// \brief Lexicographically compares two contexts.
		///
		/// If one context is a subcontext of the other, then the subcontext-comparison
		/// supersedes the lexicographical comparison: the subcontext is "less" than the "supercontext";
		/// the operator returns `true` if this is a subcontext of the right operand.
		///
		/// This odd behavior makes it so that a search operation, such as std::map::lower_bound,
		/// returns a potential subcontext of the search parameter if the latter itself isn't found.
		///
		bool operator<(std::string_view) const noexcept;
		bool operator<(const Context& cmpWith) const noexcept { return operator<(std::string_view(cmpWith.mId)); }

		std::string_view string() const& noexcept { return mId; }
		std::string      string() &&     noexcept { return std::move(mId); }

	private:
		std::string mId;
	};

	/// \brief Checks whether the string is a valid context.
	///
	/// Valid context strings match the regular expression: `^[A-Za-z0-9_]*(\.[A-Za-z0-9_]*)*$`
	///
	/// Example: "abc.def_1.3._"
	///
	bool isValidContextString(std::string_view) noexcept;


	template <typename T>
	concept CommandCallbackFunction = requires (T t, Context ctx, Input i) {
		t(ctx, i);
		[](T tr) { T tl = tr; (void) tl /* GCC complains that tl is unused... really? */; };
	};


	class CommandCallback {
	public:
		using Ptr = std::shared_ptr<CommandCallback>;
		struct RecoverableError final { }; // If this is thrown by the function, it will be caught and ignored
		virtual void operator()(const Context&, Input) = 0;
	};

	template <CommandCallbackFunction T>
	class CommandCallbackWrapper : public CommandCallback {
	public:
		T wrappedFn;
		CommandCallbackWrapper(T&& fn): wrappedFn(std::forward<T>(fn)) { }
		void operator()(const Context& ctx, Input in) noexcept(noexcept(wrappedFn(ctx, in))) override { wrappedFn(ctx, in); }
	};


	struct Binding {
		InputMapKey key;
		Context context;
	};


	class InputManager {
	public:
		CommandId addCommand    (CommandCallback::Ptr); /// \note The command shared pointer may be null, in which case nothing is done
		void      removeCommand (CommandId) noexcept;
		void      bindCommand   (CommandId, Binding);

		CommandId bindNewCommand(Binding b, CommandCallback::Ptr cb);

		void feedSdlEvent(std::string_view context, const SDL_Event&);

		bool isCommandActive(CommandId) noexcept;
		void setCommandActive(CommandId, bool value) noexcept;
		auto getActiveCommands() const noexcept { return std::views::all(mActiveCommands); }

		void clear() noexcept;

	private:
		#define UMAP_ std::unordered_map
		#define MAP_  std::map
		using ContextMap   = MAP_ <Context,          CommandId, std::less<>>;
		using ContextCache = UMAP_<std::string_view, CommandId>;
		UMAP_<InputMapKey, ContextMap,   InputMapHash> mBindings;
		UMAP_<InputMapKey, ContextCache, InputMapHash> mBindingsCache;
		UMAP_<CommandId,   CommandCallback::Ptr>       mCommands;
		UMAP_<CommandId,   InputMapKey>                mActiveCommands;
		idgen::IdGenerator<CommandId> mCommandIdGen;
		#undef MAP_
		#undef UMAP_
	};

}}
