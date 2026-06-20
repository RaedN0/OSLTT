# Lag Test

Vulkan fullscreen lag-test program.

## Build

```bash
cd build
cmake ..
make
```

## Run

```bash
./lagtest
```

## Behavior

- Window opens fullscreen/borderless
- Background is **black** by default
- On **mouse movement** or **mouse button press**, background turns **white**
- When mouse stops and no buttons pressed, reverts to **black**
- Timestamps are printed to stdout on every color change

## Requirements

- Vulkan loader + drivers
- GLFW3
- glslangValidator (for shader compilation)
