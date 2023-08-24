- **cxx/**
  - Refactor everything to follow the style guideline  
    (I can't see how this might possibly go wrong)

- **cxx/engine/**
  - Release `AssetSupplier` materials along with meshes
  - Asynchronous one-time transfer commands
    - Make them use the transfer queue
  - Integrate cleanup queues
  - `MeshSupplierInterface` doxygenumentation
  - Protect `Renderer`-owned `RenderObject` members
  - Seek a way not to construct a string from a string_view for
    every map access
  - Multithreaded `Renderer` object buffer generation (mostly for matrices)
  - Implement `WorldRenderer::rotate` and `WorldRenderer::rotateTowards`
    - Optimize `WorldRenderer` rotation functions by doing old fashioned,
      matrix-on-paper math.
  - `WorldRenderer::getViewTransf` inverts the Z axis, because the
    glm clip-space is weird and Y is also inverted; find a way to do that
    with the projection matrix instead
  - Make `vkQueuePresentKHR` wait for swapchain images instead of gframes
    - It is probably not needed to have an acquired image in order to
      draw a gframe
  - Try and simplify `AssetSupplier::msi_requestMaterial`
