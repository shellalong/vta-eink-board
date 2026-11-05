# Elecrow display driver

Source: https://github.com/Elecrow-RD/CrowPanel-ESP32-4.2-E-paper-HMI-Display-with-400-300

Pinned commit: `cb6d6b41249051890456d3a311d22925c8f26ebe`

Source directory: `example/arduino/Examples/4.2_partial_refresh/`.
The repository readme identifies this hardware/software as V1.0 and the panel as
SSD1683, 400×300. Seven files copied unchanged into `firmware/VtaBoard`:
`EPD.cpp`, `EPD.h`, `EPD_GUI.cpp`, `EPD_GUI.h`, `EPD_SPI.cpp`, `EPD_SPI.h`, `EPD_font.h`.
No standalone license for these seven example files was found in that source tree;
upstream attribution is retained here. Other libraries' licenses do not establish
a license for this example driver.

Signatures checked in both the downloaded header and implementation:

```cpp
void EPD_GPIOInit(void);
void Paint_NewImage(uint8_t *image, uint16_t Width, uint16_t Height,
                    uint16_t Rotate, uint16_t Color);
void EPD_Init(void);
void EPD_Display(const uint8_t *Image);
void EPD_Display_Part(uint16_t x, uint16_t y, uint16_t sizex,
                      uint16_t sizey, const uint8_t *Image);
```

`EPD_Init_Part` is declared in the vendor header but **has no implementation**
in this example. The firmware does not call it.
`EPD_Display_Part` expects tightly packed rows of `(sizex + 7) / 8` bytes;
the firmware passes a full-width 400×300 buffer, so its 50-byte stride matches.
The vendor's `EPD_SetCursor` writes its x argument directly; using x=0 avoids
the pixel-versus-byte ambiguity for nonzero x offsets.

Pins: display power=7, SCK=12, MOSI=11, reset=47, DC=46, CS=45, BUSY=48.
The vendor uses software SPI. GPIO 41 is not enabled by this V1.0 example.
`ROTATE_0` is already 400×300 landscape; 90 degrees would be portrait.

Full updates initialize the controller and call `EPD_Display`. The firmware then
writes the same frame to previous-image RAM 0x26. Optional partial updates keep
the controller awake and call `EPD_Display_Part(0, 0, 400, 300, frame)`.
They remain disabled by default until physically verified. Full refresh is
forced at startup, on success/error transitions, and at least every 20 minutes;
with partial mode disabled every 120-second poll uses a full refresh.

The unchanged vendor BUSY loop has no timeout. A disconnected panel, a different
controller revision, or a stuck BUSY pin may block initialization. Physical panel
behavior, waveform suitability, and ghosting cannot be confirmed by compilation.
