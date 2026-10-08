# ZH Mobile — Command & Conquer™ Generals: Zero Hour на Android

**RU** · Неофициальный порт Zero Hour (2003) на Android. Это настоящий движок игры,
собранный под ARM64, без эмулятора. Графика переводится с DirectX 8 через
[DXVK](https://github.com/doitsujin/dxvk) в Vulkan. Есть сенсорное управление для RTS,
лаунчер на русском и английском, а в игре можно переключать язык на русский, если у вас
есть русская версия игры.

**EN** · Unofficial Android port of Zero Hour: the real 2003 engine compiled for ARM64
(no emulation). DirectX 8 → DXVK → Vulkan, RTS touch controls, a Russian/English launcher,
and a switchable Russian in-game language pack (from your own Russian copy).

> **Игровых файлов здесь нет / No game assets included.** Нужна своя лицензионная копия
> игры (диск или [Steam](https://store.steampowered.com/app/2732960/)). ZH Mobile не связан
> с Electronic Arts и не одобрен ею. Not affiliated with or endorsed by Electronic Arts.

## Установка / Install

1. Скачайте последний APK со страницы **[Releases](../../releases)** и установите его.
   Нужно разрешить установку из неизвестных источников.
2. Скопируйте на телефон папки **Command and Conquer Generals** и
   **Command and Conquer Generals Zero Hour** (или папку Steam-версии Zero Hour).
3. В лаунчере нажмите **«Импортировать файлы игры»** и выберите папку, где лежат обе игры.
   Файл `00RussianZH.big`, если он есть, станет переключаемым русским пакетом.
4. Выберите язык и нажмите **«Играть»**.

Требования: Android 10+, arm64, Vulkan 1.1. Основная отладка идёт на Samsung Galaxy S23
(Adreno 740).

**Управление / Controls:** тап — выбор и приказ; протянуть пальцем — рамка выделения; долгое
нажатие — снять выделение; два пальца — камера; щипок — зум; «Назад» — меню.

## Сборка / Building

APK собирает GitHub Actions ([`.github/workflows/android.yml`](.github/workflows/android.yml)):

```sh
git clone --recursive https://github.com/Explozerman/zh-mobile && cd zh-mobile
export ANDROID_NDK_HOME=/path/to/ndk-r27c VCPKG_ROOT=/path/to/vcpkg
cmake --preset android-arm64
cmake --build build/android-arm64 --target z_generals dxvk_d3d8_install
./scripts/build/android/stage-native-libs.sh && ./scripts/build/android/stage-assets.sh
gradle -p android assembleRelease
```

Устройство Android-части:

| Path | What |
|---|---|
| `CMakePresets.json` → `android-arm64` | NDK + vcpkg (`cmake/triplets/arm64-android-zh.cmake`), game as `libmain.so` |
| `cmake/dx8.cmake`, `Patches/dxvk-android.patch` | DXVK d3d8/d3d9 cross-built with meson for Android |
| `GeneralsMD/Code/Main/SDL3Main.cpp` | Android paths, logcat/file log, language pack, resolution |
| `GeneralsMD/Code/GameEngineDevice/Source/SDL3GameEngine.cpp` | touch gestures + lifecycle (shared with iOS), Back → Esc |
| `Core/.../StdBIGFileSystem.cpp` | `GX_LANGPACK_DIR`: switchable language pack with override priority |
| `android/` | Gradle app: launcher (RU/EN, import, settings, logs) + SDL `GameActivity` |

## Лицензия и авторы / License & credits

GPL v3 ([LICENSE.md](LICENSE.md)), как и исходный код, открытый EA. Порт основан на работе
[TheSuperHackers](https://github.com/TheSuperHackers/GeneralsGameCode),
[Fighter19](https://github.com/Fighter19/CnC_Generals_Zero_Hour),
[fbraz3/GeneralsX](https://github.com/fbraz3/GeneralsX) и
[iOS-порта](https://github.com/ammaarreshi/Generals-Mac-iOS-iPad) (ammaarreshi). Подробности про
iOS-порт: [docs/README-ios-port.md](docs/README-ios-port.md) и
[docs/port/PORTING_PLAYBOOK.md](docs/port/PORTING_PLAYBOOK.md).
Также используются SDL3, DXVK, OpenAL Soft, FFmpeg, FreeType и шрифты Liberation.

Command & Conquer and Generals are trademarks of Electronic Arts Inc.
