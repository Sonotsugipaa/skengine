- **cxx/**
  - Refactor everything to follow the style guideline  
    (I can't see how this might possibly go wrong)

- **cxx/engine/**
  - Figure out stencil buffer usage, which will probably help with consistent
    UI element transparency
  - Engine initialization utilities are UB, find a way to un-UBify them
    without too much boilerplate
  - Make `Renderer` objects not get fully recomputed every time an object
    is added
  - Break down big functions in `renderer.cpp`
  - Fragment shader point light computations are highly unoptimized
  - The NVidia best practices validation layer suggests using memory
    priority hints along with `VK_EXT_pageable_device_local_memory`,
    so do that I guess
  - Gframe selection fences are destroyed/created during every swapchain recreation
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
  - Investigate what's up with view directions and signs and stuff
  - Try and simplify `AssetSupplier::msi_requestMaterial`
  - `WorldRenderer::getViewTransf` inverts the Z axis, because the
    glm clip-space is weird and Y is also inverted; find a way to do that
    with the projection matrix instead
  - Add missing model logic (as opposed to asserting that they *must* exist)
  - Reconsider how `Renderer` map members are initialized
  - Make `vkQueuePresentKHR` wait for swapchain images instead of gframes
    - It is probably not needed to have an acquired image in order to
      draw a gframe
  - Every gframe waits for the draw command to complete, because modified
    objects may or may lag behind; this should be fixable, with a lot of work
  - Asynchronous one-time transfer commands
    - Make them use the transfer queue
  - `Renderer::setModel` has a dubious, prematurely optimized way of dealing
     with reassignment; test it
  - Seek a way not to construct a string from a string_view for
    every map access
