#pragma once



namespace SKENGINE_NAME_NS {
inline namespace ui_util {

	template <typename Container>
	class ContainerIterable {
	public:
		ContainerIterable(Container::iterator begin, Container::iterator end): ei_beg(std::move(begin)), ei_end(std::move(end)) { }

		auto begin() noexcept { return ei_beg; }
		auto end()   noexcept { return ei_end; }

	private:
		Container::iterator ei_beg;
		Container::iterator ei_end;
	};

}}
