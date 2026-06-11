
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

