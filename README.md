# Depth of Field

Depth of Field is an open-source native mod for Minecraft Bedrock on Android, made for [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid).

Things near the focus distance stay sharp, things nearer or farther away are blurred.

## Features

- Blurs things near and far, keeps the focus distance sharp
- Auto focus on the middle of the screen
- Edge blur with a sharp circle in the middle
- Mod Menu settings with presets (Performance, Balanced, High)

## Requirements

- Android 9 or newer and a 64-bit ARM device (`arm64-v8a`)
- [LeviLauncher](https://github.com/LiteLDev/LeviLaunchroid) to load the mod and show its Mod Menu page
- Minecraft Bedrock 1.26.x (see `minecraft_versions` in `manifest.json`)

## Installation

1. Install LeviLauncher.
2. Import `DepthOfField.levipack` through the launcher's mod manager and enable it.
3. Launch Minecraft through LeviLauncher and open the Mod Menu to change the settings.

## Build

Requires Android NDK r28c, Python 3, and either xmake or CMake 3.22 or newer.

### xmake

```sh
xmake f -y -p android -a arm64-v8a -m release --ndk=/path/to/android-ndk-r28c --ndk_sdkver=28
xmake -y
```

### CMake

```sh
cmake -S . -B build-cmake -DCMAKE_TOOLCHAIN_FILE=/path/to/android-ndk-r28c/build/cmake/android.toolchain.cmake -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-28 -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
cmake --build build-cmake --parallel
```

Both builds produce `DepthOfField.so` and `DepthOfField.levipack`.
