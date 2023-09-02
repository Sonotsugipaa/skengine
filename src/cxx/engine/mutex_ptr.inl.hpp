#pragma once

#include <skengine_fwd.hpp>



namespace SKENGINE_NAME_NS {

	template <typename T>
	class MutexAccess {
	public:
		using Lock = std::unique_lock<std::mutex>;

		MutexAccess() = default;
		MutexAccess(T value, Lock::mutex_type& mutex): mp_value(std::move(value)), mp_lock(mutex) { }
		MutexAccess(MutexAccess&& mv): mp_value(std::move(mv.mp_value)), mp_lock(std::move(mp_lock)) { }
		MutexAccess& operator=(MutexAccess&& mv) { this->~MutexAccess(); return * new (this) MutexAccess(std::move(mv)); }

		const T& get() const &  noexcept { return mp_value; }
		T&       get()       &  noexcept { return mp_value; }
		const T  get() const && noexcept { return std::move(mp_value); }
		T        get()       && noexcept { return std::move(mp_value); }

		const T* operator->() const noexcept { return &mp_value; }
		T*       operator->()       noexcept { return &mp_value; }

		operator T&       ()       &  noexcept { return mp_value; }
		operator const T& () const &  noexcept { return mp_value; }
		operator T        ()       && noexcept { return std::move(mp_value); }
		operator const T  () const && noexcept { return std::move(mp_value); }

	protected:
		T    mp_value;
		Lock mp_lock;
	};

}
