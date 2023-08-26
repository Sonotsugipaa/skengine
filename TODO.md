- **cxx/**
  - Refactor everything to follow the style guideline  
    (I can't see how this might possibly go wrong)

- **cxx/engine/**
  - Make materials share textures instead of loading the same one multiple times
    - As things are currently, texture filenames **will** be relative to
      the working directory and nothing else.
  - Integrate cleanup queues
  - Asynchronous one-time transfer commands
    - Make them use the transfer queue
  - `ModelSupplierInterface` and `MaterialSupplierInterface` doxygenumentation
  - Optimize object buffer update procedure for simple object modifications
    - Multithreaded `Renderer` object buffer generation (mostly for matrices)
    - `VkPhysicalDeviceLimits::maxSamplerAllocationCount`
  - Implement `WorldRenderer::rotate` and `WorldRenderer::rotateTowards`
    - Optimize `WorldRenderer` rotation functions by doing old fashioned,
      matrix-on-paper math.
  - Try and simplify `AssetSupplier::msi_requestMaterial`
  - `WorldRenderer::getViewTransf` inverts the Z axis, because the
    glm clip-space is weird and Y is also inverted; find a way to do that
    with the projection matrix instead
  - Add missing model logic (as opposed to asserting that they *must* exist)
  - Reconsider how `Renderer` map members are initialized
  - Make `vkQueuePresentKHR` wait for swapchain images instead of gframes
    - It is probably not needed to have an acquired image in order to
      draw a gframe
  - `Renderer::setModel` has a dubious, prematurely optimized way of dealing
     with reassignment; test it
  - Seek a way not to construct a string from a string_view for
    every map access
