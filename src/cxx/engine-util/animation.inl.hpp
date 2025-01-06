#pragma once

#include <skengine_fwd.hpp>

#include <atomic>
#include <memory>
#include <set>
#include <unordered_map>
#include <concepts>

#include <idgen.hpp>



namespace SKENGINE_NAME_NS {

	using anim_x_t = float;


	using anim_id_e = unsigned;
	enum class AnimId : anim_id_e { };


	template <typename T>
	requires std::is_trivially_copyable_v<T>
	class AnimationValue {
	public:
		using ValueType = T;

		AnimationValue(): ap_value(std::make_shared<T>(T())) { }
		AnimationValue(T value): ap_value(std::make_shared<T>(std::move(value))) { }
		AnimationValue(const AnimationValue&) = default;
		AnimationValue(AnimationValue&&) = default;
		~AnimationValue() = default;
		AnimationValue& operator=(const AnimationValue&) = default;
		AnimationValue& operator=(AnimationValue&&) = default;

		void reset() { ap_value.reset(); }

		const T& getValue() const noexcept { return *ap_value; }
		void setValue(T value) noexcept { *ap_value = std::move(value); }

		auto getValuePtr() noexcept { return std::weak_ptr<T>(ap_value); }

	private:
		std::shared_ptr<T> ap_value;
	};


	template <typename T>
	requires std::is_trivially_copyable_v<T>
	class Animation {
	public:
		using ValueType = T;

		Animation(): anim_value_ref(), anim_x(0) { }
		Animation(nullptr_t): Animation() { }
		Animation(AnimationValue<T>& v): anim_value_ref(v.getValuePtr()), anim_x(0) { }
		virtual ~Animation() = default;

		virtual void animation_setProgress(T& value, anim_x_t progress) noexcept = 0;
		virtual void restart() noexcept { }

		auto getProgress() const noexcept { return anim_x; }

		auto setProgress(anim_x_t x) noexcept { if(! anim_value_ref.expired()) [[likely]] {
			animation_setProgress(*anim_value_ref.lock(), x);
		} }

		void reset() noexcept { if(! anim_value_ref.expired()) [[likely]] {
			animation_setProgress(*anim_value_ref.lock(), anim_x_t(0));
			anim_x = anim_x_t(0);
		} }

		void fwd(anim_x_t xDelta) noexcept { if(! anim_value_ref.expired()) [[likely]] {
			anim_x += std::max(xDelta, - anim_x);
			animation_setProgress(*anim_value_ref.lock(), anim_x + xDelta);
		} }

		void fwdUpTo(anim_x_t xDelta, anim_x_t limit) noexcept { if(! anim_value_ref.expired()) [[likely]] {
			xDelta += std::clamp(xDelta - anim_x, anim_x_t(0), limit - anim_x);
			animation_setProgress(*anim_value_ref.lock(), anim_x + xDelta);
		} }

	private:
		std::weak_ptr<T> anim_value_ref;
		anim_x_t anim_x;
	};


	template <typename T, typename ValueType>
	concept AnimationType =
		std::is_copy_assignable_v<T> &&
		requires (T t, T&& rt, ValueType& tr) {
			t.animation_setProgress(tr, anim_x_t(0.1));
			t.restart();
			t.fwd(anim_x_t(0.1));
		};


	enum class AnimEndAction : anim_id_e {
		eRepeat             = 1,
		eTerminate          = 2,
		eClampThenTerminate = 3,
		ePause              = 4,
		eClampThenPause     = 5
	};

	enum class AnimState : anim_id_e {
		eNotSet = 1,
		eActive = 2,
		ePaused = 3
	};


	template <typename T>
	requires std::is_trivially_copyable_v<T>
	class AnimationSet {
	public:
		template <AnimationType<T> Anim, typename... ConstrArgs>
		auto start(AnimEndAction endAction, ConstrArgs&&... animConstructorArgs) {
			auto id = anim_set_idGenerator.generate();
			try {
				auto animPtr = std::make_shared<Anim>(std::forward<ConstrArgs>(animConstructorArgs)...);
				anim_set_activeAnims.insert(std::pair(
					id,
					std::pair(std::move(animPtr), endAction) ));
				return id;
			} catch(...) {
				anim_set_idGenerator.recycle(id);
				std::rethrow_exception(std::current_exception());
			}
		}

		template <AnimationType<T> Anim, typename... ConstrArgs>
		auto startAhead(AnimEndAction endAction, anim_x_t timeOffset, ConstrArgs&&... animConstructorArgs) {
			auto id = anim_set_idGenerator.generate();
			try {
				auto animPtr = std::make_shared<Anim>(std::forward<ConstrArgs>(animConstructorArgs)...);
				anim_set_activeAnims.insert(std::pair(
					id,
					std::pair(std::move(animPtr), endAction) ));
				animPtr->setProgress(timeOffset);
				return id;
			} catch(...) {
				anim_set_idGenerator.recycle(id);
				std::rethrow_exception(std::current_exception());
			}
		}

		void stop(AnimId id) {
			using enum AnimEndAction;
			auto anim = anim_set_activeAnims.find(id);
			if(anim != anim_set_activeAnims.end()) {
				switch(anim->second.second) {
					default: [[fallthrough]];
					case eRepeat: [[fallthrough]];
					case ePause: [[fallthrough]];
					case eTerminate:
						break;
					case eClampThenPause: [[fallthrough]];
					case eClampThenTerminate:
						anim->second.first->setProgress(anim_x_t(1));
						break;
				}
				anim_set_activeAnims.erase(anim);
			} else {
				anim_set_pausedAnims.erase(id);
			}
			anim_set_idGenerator.recycle(id);
		}

		void interrupt(AnimId id) {
			auto e  = anim_set_activeAnims.erase(id);
			e      += anim_set_pausedAnims.erase(id);
			if(e > 0) [[likely]] anim_set_idGenerator.recycle(id);
		}

		void resume(AnimId id) {
			auto anim = anim_set_pausedAnims.find(id);
			if(anim != anim_set_pausedAnims.end())
			if(anim->second.first->getProgress < anim_x_t(1)) {
				anim_set_activeAnims.insert(std::move(anim_set_pausedAnims));
				anim_set_pausedAnims.erase(id);
			}
		}

		void pause(AnimId id) {
			auto anim = anim_set_activeAnims.find(id);
			if(anim != anim_set_activeAnims.end()) {
				anim_set_pausedAnims.insert(std::move(anim_set_activeAnims));
				anim_set_activeAnims.erase(id);
			}
		}

		void fwd(anim_x_t xDelta) {
			using enum AnimEndAction;
			std::set<AnimId> stopIds;
			auto iter = anim_set_activeAnims.begin();
			const auto end = anim_set_activeAnims.end();
			while(iter != end) {
				auto& anim = *iter->second.first;
				anim.fwd(xDelta);
				if(anim.getProgress() >= anim_x_t(1)) [[unlikely]] stopIds.insert(iter->first);
				++ iter;
			}
			for(auto id : stopIds) {
				auto anim = anim_set_activeAnims.find(id);
				switch(anim->second.second) {
					default: [[fallthrough]];
					case eTerminate:
						anim_set_activeAnims.erase(anim);
						anim_set_idGenerator.recycle(id);
						break;
					case eClampThenTerminate:
						anim->second.first->setProgress(anim_x_t(1));
						anim_set_activeAnims.erase(anim);
						anim_set_idGenerator.recycle(id);
						break;
					case ePause:
						anim_set_pausedAnims.insert(std::move(*anim));
						anim_set_activeAnims.erase(id);
						break;
					case eClampThenPause:
						anim->second.first->setProgress(anim_x_t(1));
						anim_set_pausedAnims.insert(std::move(*anim));
						anim_set_activeAnims.erase(id);
						break;
					case eRepeat:
						anim->second.first->reset();
						break;
				}
			}
		}

		auto getAnimationState(AnimId id) const noexcept {
			using enum AnimState;
			if(anim_set_activeAnims.contains(id)) return eActive;
			if(anim_set_pausedAnims.contains(id)) return ePaused;
			return eNotSet;
		}

	private:
		std::unordered_map<AnimId, std::pair<std::shared_ptr<Animation<T>>, AnimEndAction>> anim_set_activeAnims;
		std::unordered_map<AnimId, std::pair<std::shared_ptr<Animation<T>>, AnimEndAction>> anim_set_pausedAnims;
		idgen::IdGenerator<AnimId> anim_set_idGenerator;
	};

}
