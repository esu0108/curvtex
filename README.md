[English](README.md) | [日本語](README.ja.md)

# curvtex

**curvtex** is a tool for baking mesh curvature into a texture using the UV coordinates of a 3D model.

It supports `.obj`, `.gltf`, `.glb`, and `.fbx` formats, and exports the result as a 24-bit BMP image.

## Features

- GPU-accelerated curvature calculation using DirectX 12
- Signed and absolute curvature visualization
- Adjustable texture resolution (16–4096)
- Real-time preview of the baked curvature
- Automatic UV mapping from the input model

## Supported Formats

| Format | Extension |
|---|---|
| Wavefront OBJ | `.obj` |
| glTF | `.gltf`, `.glb` |
| Autodesk FBX | `.fbx` |

## Build

**Requirements**
- Windows 10 / 11
- Visual Studio 2022 or later
- CMake 3.24 or later
- Windows SDK (including `fxc.exe`)

Build using CMake:

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Run the executable:

```bash
build\Release\curvtex.exe
```

Dependencies are automatically downloaded through CMake FetchContent during configuration.

![screenshot of curvtex](docs/screenshot.png)

## Usage

1. **Load Model** — Select an OBJ, glTF, GLB, or FBX file.
2. **Texture Resolution** — Set the output texture width and height.
3. **Adjust Curvature** — Choose signed or absolute curvature and adjust the maximum curvature value.
4. **Export** — Save the resulting texture as a 24-bit BMP image.

Changes to curvature settings are reflected in the preview immediately.

## Limitations

- Models must have UV coordinates.
- UV coordinates outside the `[0, 1]` range are not rendered.
- Draco and meshopt compressed glTF files are not supported.
- glTF and FBX scenes are merged into a single mesh using their node transforms.
- Skinning and morph targets are not evaluated; meshes are processed in their bind pose.
- FBX uses its first UV set. glTF uses `TEXCOORD_0`.
- OBJ files without normals are processed using automatically generated smooth normals.
- Curvature values depend on the coordinate units of the input model. FBX unit conversion is not performed.

## Dependencies

- [Dear ImGui](https://github.com/ocornut/imgui)
- [rapidobj](https://github.com/guybrush77/rapidobj)
- [fastgltf](https://github.com/spnda/fastgltf)
- [ufbx](https://github.com/ufbx/ufbx)