# ExtIO_PlutoSDR

A from-scratch ExtIO DLL for the **ADALM-Pluto / PlutoPlus**, built for **HDSDR**
(and any other Winrad-derived SDR program that loads ExtIO_*.dll).

The DLL talks to the device through `libiio`. RX-only, 32-bit (HDSDR is x86).

## How to get a DLL without setting up Visual Studio

Push this repo to GitHub. The workflow at `.github/workflows/build.yml`
runs on every push and uploads a zipped artifact named **`ExtIO_PlutoSDR-win32`**
that contains:

- `ExtIO_PlutoSDR.dll`
- `libiio.dll` and its runtime dependencies (`libusb-1.0.dll`, `libxml2.dll`, …)

### Steps

1. Create a new empty repo on GitHub.
2. From this directory:
   ```
   git init
   git add .
   git commit -m "initial scaffold"
   git branch -M main
   git remote add origin git@github.com:<you>/<repo>.git
   git push -u origin main
   ```
3. Open the **Actions** tab on GitHub. The `build` workflow will start.
4. When it finishes, click the run → **Artifacts** → download `ExtIO_PlutoSDR-win32.zip`.
5. Unzip everything into your HDSDR install directory (e.g.
   `C:\Program Files (x86)\HDSDR\`). Existing files of the same name can be
   overwritten.

## Using it with HDSDR + PlutoPlus

1. Flash PlutoPlus firmware to the device (copy `pluto.frm` to the Pluto
   mass-storage drive, eject, wait for the LED to settle).
2. Plug Pluto in. Make sure libusb / WinUSB drivers bind to the IIO USB
   interface — Zadig is the easy path if libiio can't open the device.
3. Launch HDSDR. On first run it'll ask which ExtIO to use → pick
   **PlutoSDR**.
4. Click the **ExtIO** button in HDSDR's toolbar. The settings dialog opens.
   - **URI**: `usb:` for USB-attached, or `ip:192.168.2.1` for the default
     RNDIS-over-USB Ethernet (often more stable).
   - Press **Connect**, set sample rate / bandwidth / gain mode, press **Apply**.
5. Press **Start** in HDSDR.

## Building locally (optional)

You only need this if you don't want to go through GitHub Actions.

Prerequisites:

- Visual Studio 2019 or 2022 with the *Desktop development with C++* workload.
- CMake ≥ 3.20.
- The official **libiio Windows installer** from
  <https://github.com/analogdevicesinc/libiio/releases> (e.g.
  `libiio-0.25.gb8f3dde-Windows-setup.exe`). Install it and note the install path.

Then, from a Developer Command Prompt:

```
cmake -S . -B build -A Win32 ^
      -DLIBIIO_ROOT="C:\Program Files\Analog Devices\libiio"
cmake --build build --config Release
```

Output: `build\Release\ExtIO_PlutoSDR.dll`. Copy it plus `libiio.dll`,
`libusb-1.0.dll`, `libxml2.dll`, and friends from
`C:\Program Files\Analog Devices\libiio\<MS32 dir>` into your HDSDR folder.

## Source layout

```
src/
  ExtIO_PlutoSDR.cpp   main source: ExtIO entry points, streaming thread, dialog
  extio_api.h          ExtIO ABI types/constants (callback signature, hwtypes, status codes)
  ExtIO_PlutoSDR.def   forces undecorated stdcall exports (HDSDR needs `StartHW`, not `_StartHW@4`)
  ExtIO_PlutoSDR.rc    Win32 resource for the settings dialog
  resource.h           dialog control IDs
CMakeLists.txt          build script — drives MSVC via `cmake -A Win32`
.github/workflows/
  build.yml             CI: download libiio, build for x86, upload zipped artifact
```

## Notes / caveats

- The DLL sends IQ as `float32` (`exthwUSBfloat32`). Pluto's 12-bit samples
  are scaled by `1/2048`, giving roughly ±1.0 full-scale. Adjust `kBufSampleSets`
  in `ExtIO_PlutoSDR.cpp` if you want a different latency tradeoff.
- Sample-rate / bandwidth lists are conservative defaults that match what
  HDSDR can keep up with over USB 2.0. PlutoPlus over Ethernet can sustain
  higher rates if you add them to `kSampleRates[]`.
- This is a starting point — TX, FIR filter loading, and decimation are not
  implemented. PRs welcome.
