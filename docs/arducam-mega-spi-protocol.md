# Arducam MEGA SPI Camera Protocol

Source: https://www.arducam.com/docs/arducam-mega/arducam-mega-getting-started/packs/HostCommunicationProtocol.html

## Hardware Overview

- **Interface:** Four-wire SPI (SCLK, MOSI, MISO, CS)
- **SPI Mode:** Mode 0 (CPOL=0, CPHA=0)
- **Max Clock:** 8 MHz
- **Sensor variants:** 3MP (`0x08`) and 5MP (`0x09`)
- **Output formats:** JPEG, RGB565, YUYV
- **Architecture:** FPGA-based Arduchip controller buffers complete frames into off-chip SRAM; the MCU only issues a capture command and reads the FIFO — no real-time pixel streaming required.

## SPI Command Byte Layout

| Bit 7 | Bits 6–0 |
|-------|----------|
| R/W (0=read, 1=write) | 7-bit register address |

- **Register read:** send `0x00–0x7F`, then clock in 1+ bytes
- **Register write:** send `0x80–0xFF`, then clock out data bytes
- **FIFO burst read:** assert burst mode, then read consecutive bytes in a single CS-low transaction

## Key Registers

| Register | Address | Description |
|----------|---------|-------------|
| Number of frames | `0x00` | Frames ready in buffer (255 = full) |
| Camera power control | `0x01` | `cam_power_en`, `cam_pwdn`, `cam_rst_n` bits |
| Memory / capture control | `0x02` | `start_capture`, `clear_fifo_write` flags |

Full register map: https://blog.arducam.com/arduchip-registers/

## Capture Sequence

1. Assert `cam_power_en` in register `0x01`.
2. Write desired resolution and format via sensor registers.
3. Write `start_capture` to register `0x02`.
4. Poll capture-complete flag (or wait for interrupt).
5. Reset FIFO read pointer (`clear_fifo_write`).
6. Assert burst-read mode (`set_fifo_burst`).
7. Read frame bytes over SPI until `bytesused` satisfied.

## References

- Arducam MEGA Wiki: https://docs.arducam.com/Arduino-SPI-camera/MEGA-SPI/MEGA-SPI-Camera/
- Quick Start Guide: https://docs.arducam.com/Arduino-SPI-camera/MEGA-SPI/Quick-Start-Guide/
- Application Note (PDF): https://blog.arducam.com/downloads/datasheet/Arducam_MEGA_SPI_Camera_Application_Note.pdf
- C API Reference: https://www.arducam.com/docs/arducam-mega/arducam-mega-getting-started/packs/C_ApiDoc.html
- Arduino library reference: https://github.com/ArduCAM/Arduino
