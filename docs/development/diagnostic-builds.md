# Diagnostic build gates

The diagnostic foundation is checked with CTest on every supported platform. GUI builds require Qt 6 with the Widgets and Charts components. The macOS GUI builds target macOS 13.0 or later.

## Windows x86-64 (MSYS2 MINGW64)

The Windows CI runner is `windows-latest`; it installs the x86-64 MSYS2 MINGW64 compiler, CMake, Ninja, and Qt 6 packages.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGNO_TESTS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

## Linux x86-64 sanitizer build

The Linux CI runner is `ubuntu-latest`. It installs only the non-Qt build dependencies because `GNO_CONSOLE=ON` prevents Qt discovery and GUI target creation.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DGNO_CONSOLE=ON -DGNO_TESTS=ON -DGNO_SANITIZERS=ON
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

## macOS Apple Silicon

The `macos-15` runner is Apple Silicon. Install Qt 6 with Homebrew, then run:

```sh
brew install qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DGNO_TESTS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

## macOS Intel x86-64

The `macos-15-intel` runner is Intel x86-64. Install Qt 6 with Homebrew and use the same commands as the Apple Silicon build:

```sh
brew install qt
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DGNO_TESTS=ON -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0 -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
```

## Release invariant

The diagnostic build performs no privileged or mutating network operation.
Tests must run through CTest on every supported platform.
