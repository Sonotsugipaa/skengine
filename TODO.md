**Task at hand**:  
Figure out how to rework `Renderer`, because this isn't going anywhere.

---

- **cxx/engine/engine.hpp**
  - Address my initial **grave** misconception that (buffer queue families >
    1 => sharing is concurrent)
    - Only do this when a test graphical application is up and running,
      this is going to be a mess full of refactoring and restructuring
- **cxx/engine/renderer.hpp**
  - Refactor `Renderer` so that it stores a context (references to `VkDevice`,
    `VmaAllocator`, etc) instead of just throwing an `Engine` reference to it
  - In fact, the whole header needs to be basically rewritten from scratch
- Refactor everything to follow the style guideline  
  (I can't see how this might possibly go wrong)
