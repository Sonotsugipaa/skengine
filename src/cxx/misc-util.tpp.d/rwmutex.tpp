#pragma once

#include <memory>
#include <mutex>
#include <atomic>



#ifdef assert
	#define TRY_ASSERT_(EXPR_) assert(EXPR_)
#else
	#define TRY_ASSERT_(EXPR_) (void) (EXPR_)
#endif



namespace util {

	class RwMutex {
	private:
		struct SharedState {
			std::mutex mutex;
			std::atomic_uint_fast16_t readCtr = 0;
		};
		std::shared_ptr<SharedState> rwmp_sharedState;

	public:
		template <bool isReadLock>
		class Lock {
		public:
			Lock() = default;
			Lock(Lock&&) = default;
			Lock(const Lock&) = delete;
			Lock& operator=(nullptr_t) {
				if(! rwmp_ss) return *this;
				if constexpr (isReadLock) {
					auto oldCtr = rwmp_ss->readCtr.fetch_sub(1, std::memory_order_relaxed);
					TRY_ASSERT_(oldCtr > 0);
					if(oldCtr == 1) rwmp_ss->mutex.unlock();
				} else {
					rwmp_ss->mutex.unlock();
				}
				rwmp_ss = nullptr;
				return *this;
			}
			~Lock() {
				(*this) = nullptr;
			}

		private:
			friend RwMutex;
			Lock(std::shared_ptr<SharedState> mutex): rwmp_ss(std::move(mutex)) { }
			std::shared_ptr<SharedState> rwmp_ss;
		};
		using ReadLock = Lock<true>;
		using WriteLock = Lock<false>;

		RwMutex(): rwmp_sharedState(std::make_shared<SharedState>()) { }
		RwMutex(nullptr_t): rwmp_sharedState(nullptr) { }

		[[nodiscard]]
		auto acquireReadLock() {
			TRY_ASSERT_(isInitialized());
			auto oldCtr = rwmp_sharedState->readCtr.fetch_add(1, std::memory_order_relaxed);
			if(oldCtr < 1) {
				rwmp_sharedState->mutex.lock(); }
			return ReadLock(rwmp_sharedState);
		}

		[[nodiscard]]
		auto acquireWriteLock() {
			TRY_ASSERT_(isInitialized());
			rwmp_sharedState->mutex.lock();
			return WriteLock(rwmp_sharedState);
		}

		template <typename Fn, typename... Args>
		void read(Fn&& fn, Args&&... args) {
			TRY_ASSERT_(isInitialized());
			auto lock = acquireReadLock();
			std::forward<Fn>(fn)(std::forward<Args>(args)...);
		}

		template <typename Fn, typename... Args>
		void write(Fn&& fn, Args&&... args) {
			TRY_ASSERT_(isInitialized());
			auto lock = acquireWriteLock();
			std::forward<Fn>(fn)(std::forward<Args>(args)...);
		}

		bool isInitialized() const noexcept { return bool(rwmp_sharedState); }
		operator bool() const noexcept { return isInitialized(); }
	};

}



#undef TRY_ASSERT_
