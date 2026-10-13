# Axion-Tool

Esp32S3 Marauder Tool with Camera Support, New Hardware Compatibility, Media Player, Notepad, Retro Minigames, and Tactical Offensive/Defensive Tooling.

Engineered for the **ESP32-S3 (N16R8 Octal PSRAM)**.

## Key Features
- **Camera Viewfinder**: Live 20+ FPS preview & 3MP hardware JPEG capture with real-time ISP controls, filters, and digital zoom.
- **2.4GHz RF Analyzer**: nRF24L01+ 126-channel spectrum & waterfall analyzer.
- **Wi-Fi 802.11 Monitor**: Promiscuous monitor, targeted deauthentication, and automated WPA2 4-way handshake capture committed directly to SDMMC (`/HANDSHAKES/*.pcap`).
- **BLE Proximity Scanner**: Hardware detection badges (`[SKIMMER!]`, `[AIRTAG]`, `[FLIPPER]`) and Tactical Fox-Hunt proximity tracker with Geiger audio feedback.
- **Sub-GHz 433MHz Engine**: Raw pulse train capture, oscilloscope waveform monitor, and EV1527/PT2262 synthesis.
- **Infrared Transceiver**: TSOP receiver & 940nm blaster.
- **Notepad & Media Gallery**: Fast SD text editor with on-screen keyboard & full-color JPEG image browser.
- **Retro Minigames**: 4-in-1 space arcade engine (Space Invaders, Flappy, Pong, Snake).
- **Interactive Web Simulator**: Standalone `simulator.html` zero-dependency hardware simulator.
- **Smart Configuration**: `/axion_config.ini` human-editable profile on SD root.

## Hardware Target
- **SoC**: ESP32-S3 (16MB Flash, 8MB Octal PSRAM)
- **Display**: ST7789 SPI LCD (240x320) via LovyanGFX
- **Sensors**: OV3660 Camera, nRF24L01+, CC1101 / 433MHz Superheterodyne, TSOP IR

## Build & Flash
```bash
pio run
pio run --target upload
```
