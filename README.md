# FO4VR Community Shaders

An experimental rendering mod for **Fallout 4 VR**, bringing Community Shaders lighting, materials, shadows, and image-quality features to the game's native Direct3D 11 renderer.

This is a Fallout 4 VR implementation with stereo rendering support. Feature coverage and visual results vary; it does not include every feature from Skyrim Community Shaders or Open Shaders.

[Downloads](https://github.com/brunocatani/fo4vr-community-shaders/releases) · [Report an issue](https://github.com/brunocatani/fo4vr-community-shaders/issues) · [GPL-3.0 license](LICENSE)

## Included systems

| System | Features |
| --- | --- |
| Lighting | Linear lighting with native darkness preservation, diffuse image-based lighting, dynamic cubemaps, skylighting, and sun/moon lighting synchronization. |
| Materials | Physically based material response, authored RMAOS materials, and complex-material parallax. |
| Shadows and atmosphere | Contact shadows, fixed native-shadow controls, and stereo world-space volumetric lighting / god rays. |
| Image quality | NVIDIA DLAA and DLSS, including selectable quality modes and a center-DLAA mode with TAA in the periphery. |
| Post-processing | Filmic tonemapping, enhanced bloom, and physical glare. |
| Renderer corrections | Integrated Vanilla Fixes for native rendering paths. |

The mod also exposes experimental controls for basic wetness, wrapped grass lighting, hair specular, subsurface scattering, and cloud shadows. Some of these effects remain incomplete or lack a confirmed visible result. A feature appearing in the menu is not a guarantee of complete visual coverage.

## Requirements

- Fallout 4 VR **1.2.72.0** on Windows, launched through **F4SEVR 0.6.21**.
- A supported NVIDIA RTX GPU for the DLAA/DLSS subsystem. Its availability is checked through NVIDIA Streamline at runtime.
- **DevMenu**, if you want the in-game settings interface. You can also edit the INI directly.
- Compatible authored textures for complex parallax or authored PBR materials. Ordinary texture replacements do not automatically provide those material channels.

## Installation

1. Download a mod archive from [Releases](https://github.com/brunocatani/fo4vr-community-shaders/releases). GitHub's automatically generated **Source code** archives do not contain a built plugin.
2. Install the mod archive through Mod Organizer 2, keeping its directory structure. For a manual installation, place the archive's runtime folders under the game's `Data` directory.
3. Confirm that the installed payload includes:

   ```text
   Data/
     F4SE/Plugins/fo4vr-community-shaders.dll
     F4SE/Plugins/Streamline/
     DevMenu/Mods/fo4vr-community-shaders/menu.json
   ```

   Keep the bundled Streamline DLLs and their `Licenses` directory together. Install any other runtime folders included in the mod archive as well.
4. Launch Fallout 4 VR through F4SEVR. In DevMenu, open **FO4VR Community Shaders** to configure the mod.

## Configuration

The active settings file is in your Windows Documents folder:

```text
Documents/My Games/Fallout4VR/Mods_Config/FO4VRCommunityShaders/FO4VRCommunityShaders.ini
```

DevMenu and the plugin use this same file. Missing settings use compiled defaults. Most feature changes reload during play; **Native Shadows settings and the Vanilla Fixes master switch require a game restart**.

The **Community Shaders Visual Suite** switch controls the lighting, material, and output effects. **DLAA/DLSS, Vanilla Fixes, and Native Shadows have independent switches** and keep their own state when the visual suite is disabled.

Linear Lighting defaults to **off**. Enable it explicitly to use the PBR pipeline, which depends on Linear Lighting. Quality settings are fixed choices: the mod does not automatically lower visual quality to meet an FPS target.

If upgrading from a build that used a different INI location, move your existing settings file to the path above before launching. The plugin resolves only the current location.

## Authored PBR materials

Texture mods can register base-colour and RMAOS texture pairs through their own manifest:

```text
Data/F4SE/Plugins/FO4VRCommunityShaders/PBRMaterials/<mod-name>.json
```

```json
{
  "materials": [
    {
      "base": "textures/example/metal_d.dds",
      "rmaos": "textures/example/metal_rmaos.dds"
    }
  ]
}
```

RMAOS stores **roughness, metalness, ambient occlusion, and dielectric specular** in its R, G, B, and A channels. The base-colour texture is sRGB; RMAOS is linear data. The manifest and textures belong to the content mod. Restart the game after adding or changing a manifest. Authored transport requires both Linear Lighting and PBR to be enabled; materials without a matching entry retain the existing material path.

## Limitations and troubleshooting

This mod is experimental. Lighting and reflection coverage, stereo artifacts, and compatibility with other rendering modifications still need testing across scenes and hardware. Skylighting, diffuse IBL, contact shadows, and glare can be expensive; choose settings for your headset resolution and GPU. Complex-material effects depend on the supplied assets, and authored PBR content still needs visual qualification.

When reporting a problem, include the mod version, GPU, headset, render resolution, enabled features, relevant settings, reproduction steps, and a screenshot or short video where useful. Attach the log from the affected run:

```text
Documents/My Games/Fallout4VR/F4SE/FO4VRCommunityShaders.log
```

For an F4SE loading problem, also include `f4sevr.log` from the same directory and game session.

## Building from source

The build requires **Visual Studio 2022 with the v143 C++ toolset**, a Windows SDK containing `fxc.exe`, **CMake 4.2 or newer**, Python 3, vcpkg, CommonLibF4VR, and the **NVIDIA Streamline 2.12.0 SDK** with its runtime DLLs and license files. Dependencies from vcpkg use the `x64-windows-static` triplet.

Clone this repository with its CommonLibF4VR submodule:

```powershell
git clone --recurse-submodules https://github.com/brunocatani/fo4vr-community-shaders.git
cd fo4vr-community-shaders
```

The checked-in presets expect vcpkg at `C:/vcpkg`. Adjust the toolchain and `VCPKG_ROOT` through a local preset if your installation differs. Point CMake at the initialized CommonLibF4VR checkout and your Streamline SDK:

```powershell
cmake --preset fast -DCOMMON_LIB_F4VR_PATH="$PWD/extern/CommonLibF4VR" -DSTREAMLINE_SDK_ROOT="C:/SDKs/streamline-sdk-v2.12.0"
cmake --build build-fast --config Release -- /m:1 /p:CL_MPCount=2
```

The build stages the plugin, Streamline runtime and notices, and DevMenu manifest under `package/`. The `fast` preset does not deploy to an installed game. Local deployment can be configured in the ignored `CMakeUserPresets.json` using `POST_BUILD_COPY_PLUGIN` and `COPY_PLUGIN_BASE_PATH`.

To build and run the existing tests:

```powershell
cmake --preset tests -DCOMMON_LIB_F4VR_PATH="$PWD/extern/CommonLibF4VR" -DSTREAMLINE_SDK_ROOT="C:/SDKs/streamline-sdk-v2.12.0"
cmake --build build-tests --config Release -- /m:1 /p:CL_MPCount=2
ctest --test-dir build-tests -C Release -j 4 --output-on-failure
```

## Credits and license

Thanks to the Community Shaders and Open Shaders contributors, the F4SEVR and CommonLibF4VR maintainers, and the Fallout 4 VR modding community. NVIDIA Streamline supplies the DLAA/DLSS integration.

The project's original source and modifications are licensed under the **GNU General Public License, version 3 only (GPL-3.0-only)**. See [LICENSE](LICENSE) for the complete terms. Third-party code, assets, and NVIDIA runtime components retain their respective licenses and notices; the project license does not replace those terms.
