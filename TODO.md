- **cxx/**
  - Refactor everything to follow the style guideline  
    (I can't see how this might possibly go wrong)

- **cxx/engine/**
  - Investigate what's up with view directions and signs and stuff
  - Separate the `dev::Light` types to obtain user-modifiable types
    - For example, `dev::PointLight` has a vec4 position instead of
      a sensible vec3
  - Make `WorldRenderer::modifyLight` like `WorldRenderer::modifyObject`,
    and similar stuff (this is easy)
  - Make materials share textures instead of loading the same one multiple times
    - As things are currently, texture filenames **will** be relative to
      the working directory and nothing else.
  - Fragment shader point light computations are highly unoptimized
  - Integrate cleanup queues
  - `ModelSupplierInterface` and `MaterialSupplierInterface` doxygenumentation
  - Optimize object buffer update procedure for simple object modifications
    - Multithreaded `Renderer` object buffer generation (mostly for matrices)
    - `VkPhysicalDeviceLimits::maxSamplerAllocationCount`
  - Optimize `WorldRenderer` light storage building process
    - Only update modified lights
  - Optimize vertex shader with the gram-shmidt(?) process
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
  - Asynchronous one-time transfer commands
    - Make them use the transfer queue
  - `Renderer::setModel` has a dubious, prematurely optimized way of dealing
     with reassignment; test it
  - Seek a way not to construct a string from a string_view for
    every map access
