- **cxx/ui/**
  - Remove `Container`, an apparently useless middleman in the `Lot`-`Grid` codependency

- **cxx/engine/**
  - Document how `RayLight::aoa_threshold` works
  - Either recycle `gui::PlaceholderTextCacheView` into a generic VkImage viewer,
    or remove it entirely
  - Engine initialization utilities are UB, find a way to un-UBify them
    without too much boilerplate
  - Gframe selection fences are destroyed/created during every swapchain recreation
  - Integrate cleanup queues
    - An alternative is using `std::shared_ptr` and `std::weak_ptr` where classes
      use dependency injection
  - Implement `WorldRenderer::rotate` and `WorldRenderer::rotateTowards`
    - Optimize `WorldRenderer` rotation functions by doing old fashioned,
      matrix-on-paper math.
  - Add missing model logic (as opposed to asserting that they *must* exist)
  - Asynchronous one-time transfer commands
    - Make them use the transfer queue

- **cxx/engine-util/**
  - Fragment shader point light computations are highly unoptimized
  - The NVidia best practices validation layer suggests using memory
    priority hints along with `VK_EXT_pageable_device_local_memory`,
    so do that I guess
