# TinyEngine

A minimal runnable game-engine skeleton implemented from `design.md`.

## Included modules

- Runtime
- Core (`Logger`, `EventBus`)
- Memory (`LinearAllocator`, `PoolAllocator`)
- Task (`JobSystem`, `WorkerThread`)
- Resource (`ResourceManager`, resource states, async load)
- ECS (`Entity`, `Component`, `System`, `World`)
- Render (`SDL2 + OpenGL`, render queue)
- Script (`ScriptEngine`, simple script instance)

## Build

```powershell
cmake -S . -B build
cmake --build build --config Release
```

## Run

```powershell
.\build\Release\tinyengine_demo.exe
```

Run with a frame limit:

```powershell
.\build\Release\tinyengine_demo.exe 300
```

## Controls

- Move: `W/S` (front/back), `A/D` (left/right), `R/F` (up/down)
- Rotate: `Arrow Up/Down` (X), `Arrow Left/Right` (Y), `Q/E` (Z)
- Scale: `Z` enlarge, `X` shrink
- Speed up: hold `Shift`
- Exit: `Esc` or close window
