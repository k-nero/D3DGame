# Link chain (PUBLIC deps propagate, so game only links eng_app):
#
#   core  <-  rhi  <-  rendergraph  <-  renderer  <-  scene? (scene uses renderer proxies)
#   core  <-  assets
#   everything  <-  app
#
add_subdirectory(core)
add_subdirectory(rhi)
add_subdirectory(rendergraph)
add_subdirectory(renderer)
add_subdirectory(scene)
add_subdirectory(assets)
add_subdirectory(app)
