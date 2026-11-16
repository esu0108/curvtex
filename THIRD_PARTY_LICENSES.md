# Third-Party Licenses

curvtex uses the following third-party libraries.

Each library is distributed under its own license. These licenses apply independently of the license of curvtex itself.

## Dependencies

| Library | Version | License | Description |
|---|---|---|---|
| [Dear ImGui](https://github.com/ocornut/imgui) | v1.93.0 | MIT | User interface |
| [rapidobj](https://github.com/guybrush77/rapidobj) | v1.1 | MIT | OBJ model loading |
| [fastgltf](https://github.com/spnda/fastgltf) | v1.0.1 | MIT | glTF model loading |
| [ufbx](https://github.com/ufbx/ufbx) | v0.23.1 | MIT / Unlicense | FBX model loading |

## Transitive Dependencies

The following libraries are downloaded automatically by fastgltf through CMake FetchContent.

| Library | License | Description |
|---|---|---|
| [simdjson](https://github.com/simdjson/simdjson) | Apache-2.0 / MIT | JSON parsing |
| [simdutf](https://github.com/simdutf/simdutf) | Apache-2.0 / MIT | Unicode and UTF-8 processing |

## License Notices

The licenses of these third-party libraries remain applicable to their respective components.

When redistributing curvtex together with these libraries, their original copyright notices and license terms must be preserved in accordance with their respective licenses.

For the complete license texts, refer to the `LICENSE` files in each upstream repository.

## Disclaimer

The inclusion of these libraries does not imply endorsement by their respective authors or copyright holders.