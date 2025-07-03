#pragma once

#include <skengine_fwd.hpp>

#include <atomic>
#include <memory>
#include <set>
#include <map>
#include <unordered_map>
#include <concepts>

#include <idgen.hpp>



namespace SKENGINE_NAME_NS {

	using anim_x_t = float;


	using anim_id_e = unsigned;
	enum class AnimId : anim_id_e { };


	class Animation {
	public:
		Animation(): anim_x(0) { }
		virtual ~Animation() = default;

		virtual void animation_onSetProgress(anim_x_t progress) noexcept = 0;

		auto getProgress() const noexcept { return anim_x; }
		void setProgress(anim_x_t x) noexcept { anim_x = x; animation_onSetProgress(anim_x); }

		void reset() noexcept {
			setProgress(anim_x_t(0));
			anim_x = anim_x_t(0);
		}

		void fwd(anim_x_t xDelta) noexcept {
			xDelta = std::max(xDelta, - anim_x);
			setProgress(anim_x + xDelta);
		}

	private:
		anim_x_t anim_x;
	};


	template <typename T>
	concept BasicAnimationValueType = requires {
		std::make_shared<T>();
	};


	template <BasicAnimationValueType T>
	class BasicAnimationVar {
	public:
		using ValueType = T;

		BasicAnimationVar(): bav_valuePtr(std::make_shared<T>()) { }

		auto& operator*(this auto& self) noexcept { return *self.bav_valuePtr; }
		auto& operator->(this auto& self) noexcept { return *self.bav_valuePtr; }
		auto& value(this auto& self) noexcept { return *self; }

	protected:
		std::shared_ptr<T> bav_valuePtr;
	};


	template <BasicAnimationValueType value_type_tp>
	class BasicAnimation : public Animation {
	public:
		using VarType = BasicAnimationVar<value_type_tp>;

		BasicAnimation(BasicAnimationVar<value_type_tp> var): ba_var(std::move(var)) { }
		BasicAnimation(const BasicAnimation&) = default;
		BasicAnimation(BasicAnimation&&) = default;
		virtual ~BasicAnimation() = default;
		BasicAnimation& operator=(const BasicAnimation&) = default;
		BasicAnimation& operator=(BasicAnimation&&) = default;

		auto& value(this auto& self) noexcept { return *self.ba_var; }
		auto var() const noexcept { return ba_var; }

	private:
		VarType ba_var;
	};


	template <typename T>
	concept ConcurrentAnimationValueType = BasicAnimationValueType<T> && requires (T t, T& tr) {
		{ tr = t };
		{ t + t } -> std::convertible_to<T>;
		{ t + t + t } -> std::convertible_to<T>;
	};


	template <ConcurrentAnimationValueType> class ConcurrentAnimation;

	template <ConcurrentAnimationValueType T>
	class ConcurrentAnimationVar {
	public:
		#warning "TODO: add a permanent addend"

		using ValueType = T;

		using token_e = size_t; // Most likely well aligned for a std::map key+value pair
		enum class Token { };

		ConcurrentAnimationVar(): bav_sharedState(std::make_shared<SharedState>()) { }

		auto value() const noexcept { auto r = T(); for(auto&& v : bav_sharedState->values) r = r + v.second; return r; }

	protected:
		friend ConcurrentAnimation<T>;
		struct SharedState { Token tokenCtr = Token(0); std::map<Token, T> values; };
public:
		std::shared_ptr<SharedState> bav_sharedState;
	};


	template <ConcurrentAnimationValueType value_type_tp>
	class ConcurrentAnimation : public Animation {
	public:
		using VarType = ConcurrentAnimationVar<value_type_tp>;

		ConcurrentAnimation(const ConcurrentAnimation&) = delete;
		ConcurrentAnimation(ConcurrentAnimation&&) = default;
		ConcurrentAnimation& operator=(const ConcurrentAnimation&) = delete;
		ConcurrentAnimation& operator=(ConcurrentAnimation&&) = default;

		ConcurrentAnimation(ConcurrentAnimationVar<value_type_tp> var):
			ca_var(std::move(var))
		{
			auto& tokenCtr = ca_var.bav_sharedState->tokenCtr;
			bool tokenIsUnique;
			do {
				ca_token = typename VarType::Token(tokenCtr);
				tokenCtr = typename VarType::Token(typename VarType::token_e(tokenCtr) + 1);
				tokenIsUnique = ca_var.bav_sharedState->values.try_emplace(ca_token, value_type_tp { }).second;
			} while(! tokenIsUnique);
		}

		virtual ~ConcurrentAnimation() {
			ca_var.bav_sharedState->values.erase(ca_token);
		}

		auto& value() noexcept { return ca_var.bav_sharedState->values.at(ca_token); }

	private:
		VarType::Token ca_token;
		VarType ca_var;
	};


	template <typename T>
	concept AnimationType =
		(std::is_copy_assignable_v<T> || std::is_move_assignable_v<T>) &&
		requires (T t) {
			typename T::VarType;
			typename T::VarType::ValueType;
			t.animation_onSetProgress(anim_x_t(0.1));
			t.fwd(anim_x_t(0.1));
			{ t.value() } -> std::convertible_to<typename T::VarType::ValueType>;
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


	class AnimationSet {
	public:
		template <AnimationType Anim, typename... ConstrArgs>
		auto start(AnimEndAction endAction, ConstrArgs&&... animConstructorArgs) {
			auto id = anim_set_idGenerator.generate();
			try {
				auto animPtr = std::make_shared<Anim>(std::forward<ConstrArgs>(animConstructorArgs)...);
				anim_set_activeAnims.insert(std::pair(
					id,
					std::pair(animPtr, endAction) ));
				return id;
			} catch(...) {
				anim_set_idGenerator.recycle(id);
				std::rethrow_exception(std::current_exception());
			}
		}

		template <AnimationType Anim, typename... ConstrArgs>
		auto startAhead(AnimEndAction endAction, anim_x_t timeOffset, ConstrArgs&&... animConstructorArgs) {
			auto id = anim_set_idGenerator.generate();
			try {
				auto animPtr = std::make_shared<Anim>(std::forward<ConstrArgs>(animConstructorArgs)...);
				anim_set_activeAnims.insert(std::pair(
					id,
					std::pair(animPtr, endAction) ));
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
			if(anim->second.first->getProgress() < anim_x_t(1)) {
				anim_set_activeAnims.insert_range(std::move(anim_set_pausedAnims));
				anim_set_pausedAnims.erase(id);
			}
		}

		void pause(AnimId id) {
			auto anim = anim_set_activeAnims.find(id);
			if(anim != anim_set_activeAnims.end()) {
				anim_set_pausedAnims.insert_range(std::move(anim_set_activeAnims));
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
		std::unordered_map<AnimId, std::pair<std::shared_ptr<Animation>, AnimEndAction>> anim_set_activeAnims;
		std::unordered_map<AnimId, std::pair<std::shared_ptr<Animation>, AnimEndAction>> anim_set_pausedAnims;
		idgen::IdGenerator<AnimId> anim_set_idGenerator;
	};

}
