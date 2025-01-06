#include "input.hpp"

#include <cassert>



namespace SKENGINE_NAME_NS {
inline namespace input {

	namespace {

		constexpr bool isCharContextLevel(char c) {
			// This is necessarily correct, according to C++'s basic character set
			return
				(c >= 'A' && c <= 'Z') ||
				(c >= 'a' && c <= 'z') ||
				(c >= '0' && c <= '9') ||
				(c == '_');
		}

	}


	bool isValidContextString(std::string_view str) noexcept {
		size_t cursor = 0;
		const size_t end = str.size();
		auto consumeLevel = [&]() {
			if(cursor >= end) [[unlikely]] return false;
			if(! isCharContextLevel(str[cursor])) [[unlikely]] return false;
			while(cursor < end) {
				if(! isCharContextLevel(str[cursor])) [[unlikely]] return true;
				++ cursor;
			}
			return true;
		};
		if(! consumeLevel()) return false;
		while(cursor < end) {
			char c = str[cursor];
			if(c != '.') return false;
			++ cursor;
			if(! consumeLevel()) return false;
		}
		return true;
	}
	// static_assert(isValidContextString("abc.xyz"));
	// static_assert(isValidContextString("ABC.XYZ"));
	// static_assert(isValidContextString("_.0.1.2.3.4.5.6.7.8.9"));
	// static_assert(! isValidContextString("-"));
	// static_assert(! isValidContextString(" "));
	// static_assert(! isValidContextString(""));
	// static_assert(! isValidContextString("."));
	// static_assert(! isValidContextString("a."));
	// static_assert(! isValidContextString(".a"));
	// static_assert(! isValidContextString("a..b"));


	Context::Context(std::string str):
		mId(std::move(str))
	{
		assert(isValidContextString(mId));
	}


	Context::Cmp Context::compareContexts(std::string_view str0, std::string_view str1) noexcept {
		assert(isValidContextString(str0));
		assert(isValidContextString(str1));
		size_t cursor = 0;
		const size_t end0 = str0.size();
		const size_t end1 = str1.size();
		bool cursorEnd0;
		bool cursorEnd1;
		while(
			(! (cursorEnd0 = cursor >= end0)) & /* NOT && */
			(! (cursorEnd1 = cursor >= end1))
		) {
			if(str0[cursor] != str1[cursor]) [[unlikely]] break;
			++ cursor;
		}

		// If the mismatch is not a new level, then the longer string is just a similar branch
		if(cursorEnd0 && ! cursorEnd1) return str1[cursor] == '.' ? Cmp::eRightIsSubcontext : Cmp::eDifferent;
		if(cursorEnd1 && ! cursorEnd0) return str0[cursor] == '.' ? Cmp::eLeftIsSubcontext  : Cmp::eDifferent;

		if(cursorEnd0 /* && cursorEnd1 */) {
			assert(cursorEnd1 /* The other case should be covered by `if(cursorEnd0 && ! cursorEnd1)` above */);
			return Cmp::eSame;
		}

		// If neither string mismatches at its end, then they're necessarily different branches
		return Cmp::eDifferent;
	}


	bool Context::operator<(std::string_view ctxStr) const noexcept {
		static_assert(std::string_view("ab") < std::string_view("ab.cd") /* cppreference.com says so, but ynk */);
		assert(isValidContextString(ctxStr));
		// short_string < long_string
		// subcontext < supercontext
		// supercontext : short_string
		// supercontext < subcontext
		bool lexicNotLess = ctxStr < mId;
		#ifndef NDEBUG
			bool& thisIsSubcontext = lexicNotLess;
			auto cmp = compareContexts(mId, ctxStr);
			if(cmp == Cmp::eLeftIsSubcontext || cmp == Cmp::eRightIsSubcontext) {
				assert(thisIsSubcontext == (cmp == Cmp::eLeftIsSubcontext));
			}
		#endif
		return lexicNotLess;
	}


	CommandId InputManager::addCommand(CommandCallback::Ptr cb) {
		auto id = mCommandIdGen.generate();
		mCommands.insert({ id, std::move(cb) });
		return id;
	}


	void InputManager::removeCommand(CommandId id) noexcept {
		auto found = mCommands.find(id);
		assert(found != mCommands.end());
		mCommands.erase(found);
	}


	void InputManager::bindCommand(CommandId id, Binding binding) {
		auto foundCmd = mCommands.find(id);
		assert(foundCmd != mCommands.end());
		if(inputStateIsNonbinary(binding.key.state)) {
			throw std::runtime_error("analog input isn't implemented yet");
		} else {
			if(foundCmd == mCommands.end()) [[unlikely]] return;
			auto& ins = mBindings[binding.key];
			ins.insert_or_assign(std::move(binding.context), id);
		}
	}


	CommandId InputManager::bindNewCommand(Binding b, CommandCallback::Ptr cb) {
		auto r = addCommand(std::move(cb));
		try { bindCommand(r, b); }
		catch(...) { removeCommand(r); std::rethrow_exception(std::current_exception()); }
		return r;
	}


	void InputManager::feedSdlEvent(std::string_view ctxStr, const SDL_Event& sdlEv) {
		using enum InputState;
		constexpr auto nullCmd = idgen::invalidId<CommandId>();

		struct Event {
			Input   input;
			InputId device;
		} event;

		auto findCmd = [&]() {
			using R = std::tuple<const Context*, CommandId, std::shared_ptr<CommandCallback>*, bool>;
			constexpr auto nullTuple = R { nullptr, nullCmd, nullptr, false };
			auto mapKeyWithChange    = InputMapKey { event.input.id, (event.input.state | InputState::eActive) | InputState::eDeactivated };
			auto mapKeyWithoutChange = InputMapKey { event.input.id, (event.input.state | InputState::eActive) & (~InputState::eDeactivated) };
			bool boundOnChange = true;
			auto bindingFound = mBindings.find(mapKeyWithChange);
			if(bindingFound == mBindings.end()) {
				bindingFound = mBindings.find(mapKeyWithoutChange);
				boundOnChange = false;
				if(bindingFound == mBindings.end()) return nullTuple;
			}
			auto ctxFound = bindingFound->second.lower_bound(ctxStr);
			if(ctxFound == bindingFound->second.end()) return nullTuple;
			auto ctxCmp = Context::compareContexts(ctxStr, ctxFound->first.string());
			if(ctxCmp == Context::Cmp::eSame || ctxCmp == Context::Cmp::eLeftIsSubcontext) {
				auto cmdFound = mCommands.find(ctxFound->second);
				assert(cmdFound != mCommands.end());
				return R { &ctxFound->first, ctxFound->second, &cmdFound->second, boundOnChange };
			} else {
				return nullTuple;
			}
		};
		auto activateCmd = [&]() {
			auto [ctx, cmdId, cmdPtrPtr, boundOnChange] = findCmd();
			if(cmdPtrPtr)
			if(*cmdPtrPtr) {
				(*cmdPtrPtr)->operator()(
					*ctx,
					Input { event.input.id, event.input.state, event.input.value } );
			}
			mActiveCommands.insert_or_assign(cmdId, InputMapKey { event.input.id, event.input.state });
		};
		auto deactivateCmd = [&]() {
			auto [ctx, cmdId, cmdPtr, boundOnChange] = findCmd();
			mActiveCommands.erase(cmdId);
		};
		auto unfocus = [&]() {
			mActiveCommands.clear();
		};

		assert(isValidContextString(ctxStr));

		switch(sdlEv.type) {
			default: return;
			case SDL_EventType::SDL_WINDOWEVENT: if(sdlEv.window.event == SDL_WINDOWEVENT_FOCUS_LOST) unfocus(); return;
			case SDL_EventType::SDL_KEYDOWN:         event.input = { { }, eActive,   InputValue::eActive   }; event.device = inputIdDeviceKeyboard; break;
			case SDL_EventType::SDL_KEYUP:           event.input = { { }, eInactive, InputValue::eInactive }; event.device = inputIdDeviceKeyboard; break;
			case SDL_EventType::SDL_MOUSEBUTTONDOWN: event.input = { { }, eActive,   InputValue::eActive   }; event.device = inputIdDeviceMouse; break;
			case SDL_EventType::SDL_MOUSEBUTTONUP:   event.input = { { }, eInactive, InputValue::eInactive }; event.device = inputIdDeviceMouse; break;
		}
		switch(event.device) {
			default: assert(false && "if the device is bad, then something went wrong in this scope"); return;
			case inputIdDeviceKeyboard:
				event.input.id = inputIdFromSdlKey(SDL_KeyCode(sdlEv.key.keysym.sym));
				event.input.state = event.input.state | eDeactivated;
				if(sdlEv.key.repeat) { return; } break;
			case inputIdDeviceMouse:
				event.input.id = inputIdFromSdlMouse(sdlEv.button.button); break;
		}

		if(event.input.state != eAnalog) {
			bool active = inputStateCurrentlyActive(event.input.state);
			if(active) activateCmd();
			else deactivateCmd();
		}
	}


	bool InputManager::isCommandActive(CommandId id) noexcept {
		assert(mCommands.contains(id));
		return mActiveCommands.contains(id);
	}

	void InputManager::setCommandActive(CommandId id, bool value) noexcept {
		auto found = mCommands.find(id);
		assert(mCommands.contains(id));
		if(found == mCommands.end()) [[unlikely]] return;
		if(value) mActiveCommands.insert({ id, InputMapKey { { }, InputState::eInactive } });
		else mActiveCommands.erase(id);
	}


	void InputManager::clear() noexcept {
		mBindings.clear();
		mBindingsCache.clear();
		mCommands.clear();
		mActiveCommands.clear();
		mCommandIdGen = { };
	}

}}
