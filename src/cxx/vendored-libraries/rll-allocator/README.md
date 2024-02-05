# Reverse-Logarihmic Linear Allocator

I haven't gotten around putting effort into this document,
nor the library's name, so I'll just write here a few "simple"
examples.

## Examples

```
# CMakeLists.txt

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_subdirectory("${PATH_TO_THIS_REPO}")

add_executable("example_0" "example_0.cpp")
add_executable("example_1" "example_1.cpp")
target_link_libraries("example_0" "rll-alloc")
target_link_libraries("example_1" "rll-alloc")
```

---

``` C++
// example_0.cpp

#include <rll-alloc/static_allocator.hpp>

#include <span>
#include <cstdint>
#include <cstring>


std::span<uint32_t>& get_large_external_resource();
void                 use_large_external_resource();


int main() {
	// The Allocator manages endianness-agnostic quads of bytes
	using address_type = uint8_t*;

	// The available large resource comes in quads of bytes,
	// so allocations have to be aligned as such
	size_t page_size = sizeof(uint32_t) / sizeof(uint8_t);

	using Allocator = rll_alloc::StaticAllocator<address_type, page_size>;

	auto& ler       = get_large_external_resource();
	auto  allocator = Allocator((uint8_t*) ler.data(), ler.size());

	// We will deallocate this later
	uint8_t* last_allocation;

	for(size_t i = 0; i < 32; ++i) {
		char     hello_cstr[] = "Hello!";
		size_t   hello_pages  = sizeof(hello_cstr) / page_size;
		uint8_t* ler_slice    = allocator.alloc(hello_pages);
		last_allocation       = ler_slice;
		
		memcpy(ler_slice, hello_cstr, hello_pages);
	}

	use_large_external_resource();

	// No need to deallocate the addresses, since
	// the Allocator does not actually hold resources;
	// let's deallocate something just for show
	allocator.dealloc(last_allocation);

	return EXIT_SUCCESS;
}
```

---

``` C++
// example_1.cpp

#include <rll-alloc/static_allocator.hpp>

#include <cstdint>
#include <cstring>


// This function arranges up to 4 triangles (12 vertices)
// to be drawn somewhere, somehow; it returns the amount of
// vertices do draw
size_t compute_triangles(float[3*12] tmp_buffer);

void draw_triangles(float* vertices, size_t vertex_count);


int main() {
	using address_type = unsigned; // An address is not necessarily a pointer
	size_t page_size = 3;          // We're managing indexes of an array of 3D vectors
	using Allocator = rll_alloc::StaticAllocator<address_type, page_size>;

	// We could use a struct,  OR  we could do this
	// the old fashioned way for the sake of the example
	float vector_storage[3*128];

	// Arrays start at 0, so the base address is 0 (duh);
	// the allocator manages 128 vectors,
	// every page is a vector, so we got 128 pages.
	auto allocator = Allocator(0, 128);

	unsigned tmp_buffer = allocator.alloc(3*12);
	unsigned draw_buffer = SIZE_MAX;
	size_t   draw_buffer_size = 0;
	for(size_t i = 0; i < 4; ++i) {
		// In 4 iterations, no more than 48 vertices are prepared;
		// there's no danger of running out of space.
		// Compute the triangle, grow the draw buffer
		// then insert the newly prepared vertices.
		size_t   prepared_vertices = compute_triangles(tmp_buffer);
		size_t   draw_buffer_swap_size = draw_buffer_size + (3 * prepared_vertices);
		unsigned draw_buffer_swap = allocator.alloc(draw_buffer_swap_size);
		// Copy the previous buffer to the new one
		memcpy(
			&vector_storage[draw_buffer_swap],
			&vector_storage[draw_buffer], sizeof(float) * draw_buffer_size );
		// Copy the new vertices to the new buffer
		memcpy(
			&vector_storage[draw_buffer_swap + draw_buffer_size],
			&vector_storage[tmpbuffer],   sizeof(float) * prepared_vertices );
		draw_buffer      = draw_buffer_swap;
		draw_buffer_size = draw_buffer_swap_size;
		// `draw_buffer_swap` contains the old buffer
		allocator.dealloc(draw_buffer_swap);
	}

	draw_triangles(
		&vector_storage[draw_buffer],
		draw_buffer_size / 3 );

	// Doing this isn't needed at the end of the Allocator's lifetime,
	// but deallocating everything is neat
	allocator.dealloc(draw_buffer);
	allocator.dealloc(tmp_buffer);

	return EXIT_SUCCESS;
}
```
