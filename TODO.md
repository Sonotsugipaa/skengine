**Task at hand**:  
Figure out how to rework `Renderer`, because this isn't going anywhere.

---

- **cxx/engine**
  - Integrate cleanup queues

- **cxx/engine/engine.hpp**
  - ~~Address my initial **grave** misconception that (buffer queue families >
    1 => sharing is concurrent)~~
    - ~~Only do this when a test graphical application is up and running,
      this is going to be a mess full of refactoring and restructuring~~
    - I was wrong, *I was right*:  
      EXCLUSIVE means ownership transfer is manual,
      acquired automatically at first (but acquired nonetheless), and doesn't
      use the given queue families;  
      CONCURRENT means ownership transfer is always automatic, and *does* need
      a list of queue families to be provided.  
      If sharing is EXCLUSIVE and queue families are provided, it's either
      useless, or QoL - but since the utilities *are* the QoL,
      empty list => EXCLUSIVE.

- **cxx/engine/renderer.hpp**
  - Refactor `Renderer` so that it stores a context (references to `VkDevice`,
    `VmaAllocator`, etc) instead of just throwing an `Engine` reference to it
  - In fact, the whole header needs to be basically rewritten from scratch
- Refactor everything to follow the style guideline  
  (I can't see how this might possibly go wrong)
