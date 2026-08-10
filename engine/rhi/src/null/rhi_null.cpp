// Null backend: every call is a no-op returning valid handles.
// Compiles on every platform; later upgraded to *record* calls so
// render-graph tests can assert barrier/pass ordering on macOS.
namespace engine::rhi { int null_stub_ = 0; }
