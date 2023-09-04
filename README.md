# Path Tracing SDK v1.2.0

![Title](./images/r-title.png)


## Overview

Path Tracing SDK is a code sample that strives to embody years of ray tracing and neural graphics research and experience. It is intended as a starting point for a path tracer integration, as a reference for various integrated SDKs, and/or for learning and experimentation.

The base path tracing implementation derives from NVIDIA’s [Falcor Research Path Tracer](https://github.com/NVIDIAGameWorks/Falcor), ported to approachable C++/HLSL [Donut framework](https://github.com/NVIDIAGameWorks/donut).

GTC presentation [How to Build a Real-time Path Tracer](https://www.nvidia.com/gtc/session-catalog/?tab.catalogallsessionstab=16566177511100015Kus&search.industry=option_1559593201839#/session/1666651593475001NN25) provides a high level introduction to most of the features.


## Features

* DirectX 12 and Vulkan back-ends
* Reference and real-time modes
* Simple BSDF model that is easy to extend
* Simple asset pipeline based on glTF 2.0 (support for a subset of glTF extensions including animation)
* NEE/visibility rays and importance sampling for environment maps with MIS
* Basic volumes and nested dielectrics with priority
* RayCone for texture MIP selection
* Basic analytic lights (directional, spot, point)
* [RTXDI](https://github.com/NVIDIAGameWorks/RTXDI) integration for ReSTIR DI (light importance sampling) and and ReSTIR GI (indirect lighting)
* [OMM](https://github.com/NVIDIAGameWorks/Opacity-MicroMap-SDK) integration for fast ray traced alpha testing
* [NRD](https://github.com/NVIDIAGameWorks/RayTracingDenoiser) ReLAX and ReBLUR denoiser integration with up to 3-layer path space decomposition (Stable Planes)
* Reference mode 'photo-mode screenshot' with basic [OptiX denoiser](https://developer.nvidia.com/optix-denoiser) integration
* Basic TAA, tone mapping, etc.
* Streamline + DLSS integration


## Requirements

- Windows 10 20H1 (version 2004-10.0.19041) or newer
- DXR Capable GPU
- GeForce Game Ready Driver 531.18 or newer
- DirectX 12 or Vulkan API
- DirectX Raytracing 1.1 API, or higher
- Visual Studio 2019 or later with Windows 10 SDK version 10.0.20348.0 or later


## Known Issues

* Enabling Vulkan support requires a couple of manual steps, see [below](#building-vulkan)
* SER support on Vulkan is currently work in progress
* Using SER with DLSS3 Frame Generation is known to cause crashes in certain conditions in drivers older than version 536.99
* Running Vulkan on AMD GPUs may trigger a TDR during TLAS building in scenes with null TLAS instances

## Folder Structure

|						| |  
| -						| - |
| /bin					| default folder for binaries and compiled shaders
| /build				| default folder for build files
| /donut				| code for a custom version of the Donut framework  
| /donut/nvrhi    | code for the NVRHI rendering API layer (a git submodule)
| /external			| external libraries and SDKs, including NRD, RTXDI, and OMM
| /media				| models, textures, scene files  
| /tools				| optional command line tools (denoiser, texture compressor, etc)
| /pt_sdk				| **Path Tracing SDK core; Sample.cpp/.h/.hlsl contain entry points**
| /pt_sdk/PathTracer	| **Core path tracing shaders**


## Build

At the moment, only Windows builds are supported. We are going to add Linux support in the future.

1. Clone the repository **with all submodules recursively**:
   
   `git clone --recursive https://github.com/NVIDIAGameWorks/Path-Tracing-SDK.git`

2. Pull the media and other non-git-hosted files:
   
   ```
   cd Path-Tracing-SDK
   update_dependencies.bat
   ```
   
3. Use CMake to configure the build and generate the project files.
   
   ```
   cmake CMakeLists.txt -B ./build
   ```

   Use `-G "some tested VS version"` if specific Visual Studio or other environment version required. Make sure the x64 platform is used. 

4. Build the solution generated by CMake in the `./build/` folder.

   In example, if using Visual Studio, open the generated solution `build/PathTracingSDK.sln` and build it.

5. Select and run the `pt_sdk` project. Binaries get built to the `bin` folder. Media is loaded from `media` folder.

   If making a binary build, the `media` and `tools` folders can be placed into `bin` and packed up together (i.e. the sample app will search for both `media/` and `../media/`).


## Building Vulkan

Due to interaction with various included libraries, Vulkan support is not enabled by default and needs a couple of additional tweaks on the user side; please find the recommended steps below:
 * Install Vulkan SDK (we tested with 1.3.246.1 but others will work)
 * Set DONUT_WITH_VULKAN and NVRHI_WITH_VULKAN CMake variables to ON
 * Disable STREAMLINE_INTEGRATION (set CMake variable to OFF)
 * Clear CMake cache (if applicable) to make sure the correct dxc.exe path (from Vulkan SDK) is set for SPIRV compilation
 

 ## User Interface

Once the application is running, most of the SDK features can be accessed via the UI window on the left hand side and drop-down controls in the top-center. 

![UI](./images/r-ui.png)

Camera can be moved using W/S/A/D keys and rotated by dragging with the left mouse cursor.


## Command Line

- `-scene` loads a specific .scene.json file; example: `-scene programmer-art.scene.json`
- `-width` and `-height` to set the window size; example: `-width 3840 -height 2160 -fullscreen`
- `-fullscreen` to start in full screen mode; example: `-width 3840 -height 2160 -fullscreen`
- `-adapter` to run on a specific GPU in multi GPU environments using a substring match; example: `-adapter A3000` will select an adapter with the full name `NVIDIA RTX A3000 Laptop GPU`
- `-debug` to enable the graphics API debug layer or runtime, and the [NVRHI](https://github.com/NVIDIAGameWorks/nvrhi) validation layer.
 

## Developer Documentation

We are working on more detailed SDK developer documentation - watch this space!


## Contact

Path Tracing SDK is under active development. Please report any issues directly through GitHub issue tracker, and for any information, suggestions or general requests please feel free to contact us at pathtracing-sdk-support@nvidia.com!


## License

See [LICENSE.txt](LICENSE.txt)

This project includes NVAPI software. All uses of NVAPI software are governed by the license terms specified here: https://github.com/NVIDIA/nvapi/blob/main/License.txt.