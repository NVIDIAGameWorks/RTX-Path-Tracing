# Donut

Donut is a real-time rendering framework built by NVIDIA DevTech for use in various prototype renderers and code samples. It provides a collection of reusable and somewhat extensible rendering passes that can be put together in an application, and a system for loading a scene and maintaining its component graph.

Donut is **not** a game engine. It does not provide any means for creating interactive experiences, such as actors or navigation.

Donut has originated from the VRWorks Multi-Projection SDK and has been improved and evolved since. Different versions of Donut have been used to build the Asteroids demo, the DLSS SDK, and the RTXDI SDK.

## Requirements

* Windows or Linux (x64 or ARM64)
* CMake 3.10
* A C++ 17 compiler (Visual Studio 2019, GCC 8 or Clang 6)
* A shader compiler (FXC for DX11, DXC for DX12, DXC-SPIR-V for Vulkan - the newer the better)

## Dependencies

Required (all included as git submodules):

* **cgltf** to load glTF scenes
* **JsonCpp** to read and write JSON scene and configuration files
* **stb** to read and write textures and other images

Optional (also included as git submodules but can be disabled through CMake variables):

* **NVRHI**, **ImGUI**, and **glfw** for rendering (`DONUT_WITH_NVRHI`)
* **TaskFlow** for multi-threading (`DONUT_WITH_TASKFLOW`)
* **tinyexr** to read EXR images (`DONUT_WITH_TINYEXR`)
* **LZ4** to extract packaged media (`DONUT_WITH_LZ4`)
* **miniz** to mount zip archives (`DONUT_WITH_MINIZ`)

## Examples

Example projects that use Donut can be found in a separate repository: [donut_examples](https://github.com/NVIDIAGameWorks/donut_examples).

## Build

Donut is not set up to be built as a separate project. It should always be included as a submodule into a larger CMake-based project. Follow the instructions for each project to get it built.

## Structure

Donut consists of 4 major subsystems, represented as separate static libraries:

* `donut_core` provides the basic functionality, including math, virtual file system (VFS), logging, JSON and other utilities. The core module does not include any graphics functions.
* `donut_engine` implements scene import and maintenance, animations, materials, texture cache, descriptor table management, console variables, and basic audio. Depends on `donut_core`.
* `donut_render` provides a collection of rendering passes, such as forward and deferred shading, temporal AA, SSAO, shadow maps, procedural sky, tone mapping, and bloom. Depends on `donut_core` and `donut_engine`.
* `donut_app` is a framework for creating interactive applications and it includes the graphics device managers, UI bindings and utilities, camera, and media file systems. Depends on `donut_core` and `donut_engine`.

The engine and render modules require some shaders, which can be found in the `shaders` folder and built with the `donut_shaders` target.

## Features

### Graphics API support

Most interaction with graphics APIs is done through the NVRHI abstraction layer. Donut and NVRHI support the following GAPIs:

* Vulkan 1.2, requires Vulkan headers version 1.2.162 or later; included as a submodule of NVRHI (`DONUT_WITH_VULKAN`)
* Direct3D 12, requires Windows SDK version 19041 or later (`DONUT_WITH_DX12`)
* Direct3D 11, requires some compatible version of Windows SDK (`DONUT_WITH_DX11`)

Note that NVRHI does not provide any means to create the GAPI devices or windows, that functionality is handled by the `DeviceManager` class and its descendants in `donut_app`.

### Scene formats

In this version, Donut can only import [glTF 2.0](https://github.com/KhronosGroup/glTF) models with some limitations:

* No morph targets
* No KTX2 textures (but DDS textures are supported)

Supported glTF extensions:
* `KHR_materials_pbrSpecularGlossiness`
* `KHR_materials_transmission`
* `KHR_lights_punctual`
* `MSFT_texture_dds`.

In addition to glTF, Donut supports its own JSON-based scene layout files. Those files can load multiple glTF models and combine them into a larger scene graph, also add lights, cameras, animations, and apply animations to scene nodes imported from the models using their paths.

### Render passes

For a full list of render passes, refer to the headers in the [include/donut/render](include/donut/render) folder.

The most useful passes are:

* Forward shading
* G-buffer fill
* Deferred shading
* Temporal anti-aliasing
* Adaptive tone mapping
* SSAO
* Procedural sky

### Ray tracing

Donut does not provide any ray tracing passes or even maintain acceleration structures (AS'es). That is mostly because the requirements for ray tracing AS'es are different in each application, and those affect the passes that use the AS'es. But it does provide a generic bindless resource table that is populated with textures and geometry data buffers when the scene is loaded. Building acceleration structures for a scene can be easily handled on the application side using the data structures provided by Donut's scene representation and the NVRHI abstractions.

## License

Donut is licensed under the [MIT License](LICENSE.txt).