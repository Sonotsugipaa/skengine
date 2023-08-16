- **cxx/**
  - Refactor everything to follow the style guideline  
    (I can't see how this might possibly go wrong)

- **cxx/engine/**
  - Asynchronous one-time transfer commands
    - Make them use the transfer queue
  - Integrate cleanup queues
  - `MeshSupplierInterface` doxygenumentation
  - Protect `Renderer`-owned `RenderObject` members
  - Seek a way not to construct a string from a string_view for
    every map access
