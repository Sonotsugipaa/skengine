# id-generator

**id-generator** is a simple C++23 header-only library for generating (or managing)
unique identifiers based on scoped enumeration types.  
It only depends on a small part of the Standard Template Library (most notably,
its *&lt;deque&gt;* header), and, being based on scoped enums, can only guarantee
to generate `INTMAX_T` identifiers without any of them being recycled.

On most `x86_64` machines, this means that in the following example...

```C++
enum class Id : intmax_t { };

int main() {
	idgen::IdGenerator<Id> gen;
	Id id;
	do {
		id = gen.generate();
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	} while(id < idgen::maxId<Id>());
	return EXIT_SUCCESS;
}
```

... the `int main()` function would successfully exit after 593066617 years.


## Integration

Simply copy `/src/cxx/include/idgen.hpp` somewhere in your project, and
include it as any other single-header library.


## Usage

A `idgen::IdGenerator<E>` is an object that sequentially generates identifiers
(hereinafter "*IDs*") according to the capabilities of the ID type `E`:  
`E` must be a scoped enumeration type, whose underlying type is a signed or
unsigned integral type.

Both signed and unsigned ID types only allow positive types.  
An unsigned type has only one "impossible" value, returned by `idgen::invalidId<T>()`
(which can be used as a null ID);  
a signed type will never generate a negative value, which means the user can
reserve negative IDs as hardcoded or pre-determined ones - the null ID is a
negative number way below zero, presumably **-128** for `int8_t` on `x86_64`.

In order to generate an ID, use `IdGenerator<E>::generate()`:
on its own, the function does very little besides incrementing a counter and
returning its new value.

If the underlying type is small and the user needs to reintroduce unused
IDs into the pool of generateable numbers, they can use the
`IdGenerator<E>::recycle(E)` function to do so:
`IdGenerator<E>::generate()` will (re)generate previously recycled IDs before
resuming its normal counter-incrementing behavior.  
In order to function, `recycle(E)` stores all recycled IDs in segments -
the space occupied by those segments only increases when non-sequential IDs
are recycled.

### Example

```C++
#include <iostream>
#include "idgen.hpp"

using namespace std;

using id_e = int;
enum class Id : id_e { };

using Gen = idgen::IdGenerator<Id>;

int main() {
	Gen gen;
	Id  id[3];
	cout << '> ' << (id[0] = gen.generate()) << '\n';
	cout << '> ' << (id[1] = gen.generate()) << '\n';
	cout << '> ' << (id[2] = gen.generate()) << '\n';
	gen.recycle(id[2]);
	gen.recycle(id[1]);
	cout << '> ' << gen.generate() << '\n';
	cout << '> ' << gen.generate() << '\n';
	gen.recycle(id[0]);
	cout << '> ' << gen.generate() << '\n';
	cout << '> ' << gen.generate() << endl;
	return EXIT_SUCCESS;
}

//// Expected output:
// > 0
// > 1
// > 2
// > 1
// > 2
// > 0
// > 3
```
