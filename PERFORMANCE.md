# X3 performance work

This fork keeps the CrossInk source and its accepted X3 performance builds in Git. The upstream project is [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk). Work starts from CrossInk 1.6.1 at `9914146eeae7b46b300f475a16c32426fc02ec1f`.

## Accepted builds

Each saved build has a tag from `x3-perf-v1` through `x3-perf-v15`. Its commit tree matches the source tree saved with that build. The import date records when the snapshots entered this fork. [performance/iterations.json](performance/iterations.json) gives each source tree and firmware hash.

Download the current app from [the v15 release](https://github.com/Klohto/CrossInk/releases/tag/x3-perf-v15). The release contains the exact binary that passed the saved checks and was flashed to an X3. Its SHA-256 is `50dee4643e530f4a443049f10823b9985cdfdf3449e9071a6a6afe32ddb55fd6`.

The app uses 6,314,336 bytes, with 239,264 bytes free in its partition. Static RAM is 64,904 bytes. The pinned FreeInk SDK remains at `699370183fa3a0e33c9cb83a36f701bbb6022095`.

## Daily use

The main workload opens an EPUB, then reads and turns pages. Lock and wake must restore the saved page through repeated use.

The v15 source reads ASCII direction classes from a 128-byte flash table. This classifier uses 90.7% fewer own instructions on the checked Latin EPUB pages. The larger-codepoint search uses a shift for each midpoint. Other routines retain their own cost.

V15 passes 949 selected host checks. The 32 Bidi and Arabic tests pass with address and undefined-behavior checks. All 2,114,116 comparisons with the old classifier match. Each of the 58 native screen states matches v14, including exact page restoration after wake. [performance/v15.json](performance/v15.json) records these results.

The daily rank model covers 42,234 source functions and callbacks. There are 691 complete body reviews. Its central rates assume 120 reading minutes and 240 page turns, with six locks and wakes. These rates and the sleep-mode shares remain assumptions.

## Build and checks

Clone with the SDK submodules:

```sh
git clone --recurse-submodules https://github.com/Klohto/CrossInk.git
cd CrossInk
pio run -e default
```

The X3 app is `.pio/build/default/firmware.bin`. Host checks use CMake:

```sh
cmake -S test -B build/performance-host -DCMAKE_BUILD_TYPE=Release
cmake --build build/performance-host --parallel
ctest --test-dir build/performance-host --output-on-failure -E 'Png|PNG'
```

Further PNG work stays deferred. The test filter skips its 18 checks while preserving the accepted source.

The [X3 emulator](https://github.com/Klohto/xteink-x3-emulator) executes the firmware on an ESP32-C3 model. Native receipts identify the firmware and backend by hash. Instruction counts measure the selected code bodies. Its calibrated-time and full-machine flags remain false.

New accepted builds will keep their source commits and checks with a version tag. The full firmware audit continues with kerning lookup.
