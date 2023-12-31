#pragma once

#include <utility>



namespace util {

	/// \brief Wrapper for primitive values that automatically resets moved-from variables.
	/// \tparam T         The underlying type of the Moveable object.
	/// \tparam zero_init The value to assign to the moved-from variable.
	///
	/// `Moveable` member variables can be used for pointers or descriptors of
	/// owned resources that are not destroyed by the default destructor,
	/// without having to define (non-default) move constructor and assignment
	/// operators - as shown in the following example snippet.
	///
	///     struct Foo {
	///        Moveable<file_descriptor_t, 0> fdesc;
	///        size_t size;
	///
	///        Foo() = default;  // Moveable<...>() default-initializes its underlying value
	///
	///        template <typename Object>
	///        Foo(const char* filename):
	///           fdesc(filelib_open(filename)),
	///           size(filelib_getSize(fdesc))
	///        { }
	///
	///        ~Foo() {
	///           if(fdesc.value != 0) {
	///              filelib_close(fdesc);
	///              fdesc = 0;
	///           }
	///        }
	///
	///        Foo(Foo&& mv) = default;             // Moveable<...> sets mv's value to `0` here
	///        Foo& operator=(Foo&& mv) = default;  // ^ Ditto
	///
	///     }
	///
	template <typename T, T default_value_ = T { }>
	struct Moveable {
		using value_type = T;
		static constexpr T default_value = default_value_;

		T value;

		Moveable(): value(default_value) { }
		template <typename U> Moveable(const U& v): value(v) { }
		template <typename U> Moveable(U&& v): value(std::move(v)) { }
		~Moveable() { value = default_value; }

		template <typename U, U uz> Moveable(Moveable<U, uz>&& mv): value(std::move(mv.value)) {
			mv.value = uz;
		}

		template <typename U, U uz> Moveable& operator=(Moveable<U, uz>&& mv) {
			value = std::move(mv.value);
			mv.value = uz;
			return *this;
		}
	};

}
