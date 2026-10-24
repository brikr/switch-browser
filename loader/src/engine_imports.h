#pragma once

class Linker;

// Points every host -> engine trampoline (generated at build time) at the engine's export.
// Fatal if the engine doesn't export one of them.
void bind_engine_imports(const Linker& linker);
