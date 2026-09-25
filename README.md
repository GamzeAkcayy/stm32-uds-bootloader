# stm32-can-bootloader

Secure CAN bootloader for STM32 with UDS/ISO-TP firmware update, CRC32 verification and power-loss recovery.

> **Status:** work in progress. See the [roadmap](#roadmap) for what is done and what is planned. Numbers in [Results](#results) are filled in only after they are measured.

## Why this project
Field firmware updates over CAN are standard in automotive ECUs, and a bootloader that can be interrupted at any point without bricking the device is a real engineering problem, not a simple demo. 

This project implements an automotive-grade update pipeline compliant with:
* **ISO 14229-1 (UDS):** Application-layer diagnostic and flashing services (`0x10`, `0x34`, `0x36`, `0x37`, `0x31`).
* **ISO 15765-2 (ISO-TP):** Transport layer network protocol for packet segmentation, flow control, and reassembly over standard 8-byte CAN frames.

It covers the full path: flash partitioning, integrity verification (CRC32), power-loss recovery, and builds on my earlier [vehicle CAN bus simulator](https://github.com/GamzeAkcayy/stm32-can-node) (STM32 + Raspberry Pi, FreeRTOS, SocketCAN).

## Architecture

```
 Raspberry Pi (update client, Python)          STM32 target
 ┌───────────────────────────────┐            ┌──────────────────────────────┐
 │ firmware.bin + CRC32/signature│            │ Bootloader (fixed sectors)   │
 │ ISO-TP segmentation           │  CAN bus   │  UDS server, ISO-TP, flash   │
 │ UDS client                    │◄──────────►│  driver, CRC check, boot     │
 │ (python-can / SocketCAN)      │            │  decision                    │
 └───────────────────────────────┘            ├──────────────────────────────┤
                                              │ Application (updatable)      │
                                              └──────────────────────────────┘
```

> TODO: replace the ASCII diagram with a proper diagram (draw.io / Mermaid) once the design is final.

### Flash layout

| Region      | Purpose                                     | Notes                                    |
|-------------|---------------------------------------------|------------------------------------------|
| Bootloader  | UDS server, flash driver, boot logic        | Never overwritten by updates             |
| Metadata    | Application size, CRC32, valid flag         | Written last, after verification         |
| Application | Updatable firmware, own vector table        | Entered only if metadata is valid        |

> TODO: fill in the actual sector addresses and sizes for your chip after the linker scripts are written.

### Boot decision

1. Bootloader starts after reset.
2. If update is requested (or the application is invalid), stay in bootloader and wait for a UDS session.
3. Otherwise verify metadata and CRC32, then jump to the application (set vector table offset, load MSP, jump to reset handler).

## Supported UDS services

| SID  | Service                  | Purpose                              |
|------|--------------------------|--------------------------------------|
| 0x10 | DiagnosticSessionControl | Enter programming session            |
| 0x31 | RoutineControl           | Erase flash, verify integrity        |
| 0x34 | RequestDownload          | Announce address and size            |
| 0x36 | TransferData             | Send firmware blocks                 |
| 0x37 | RequestTransferExit      | Finish transfer                      |

> Adjust this table to match what you actually implement.

## Hardware

- STM32 development board (TODO: exact model)
- CAN transceiver (TODO: e.g. TJA1050 / SN65HVD230)
- Raspberry Pi with a CAN interface (TODO: e.g. MCP2515 HAT)
- USB logic analyzer for timing verification

## Build and run

```bash
# Bootloader
cd bootloader
make            # TODO: replace with your actual build command

# Application (example firmware to be updated)
cd ../app
make

# Update client (Raspberry Pi)
cd ../tools
pip install -r requirements.txt
python update_client.py --interface can0 --firmware ../app/build/app.bin
```

> TODO: replace commands with the real ones and document toolchain versions (arm-none-eabi-gcc, OpenOCD/STM32CubeProgrammer).

## Testing

- **Unit tests:** ISO-TP, CRC32 and UDS parsing (Unity/Ceedling), run on host.
- **Hardware tests:** power-loss during transfer, corrupted image, wrong CRC, oversized image.
- **CI:** build and unit tests on every push (GitHub Actions).

## Results

Filled in after measurement. Do not publish estimates as results.

| Metric                          | Value | Test Setup                                |
|---------------------------------|-------|-------------------------------------------|
| Update time for 64 KB image     | TBD   | 500 kbps CAN, 64-byte block size          |
| Recovery success after cut      | TBD   | N interruptions at random transfer points |
| Bootloader flash footprint      | TBD   | arm-none-eabi-gcc, -Os                    |
| Corrupted image rejection       | TBD   | Bit-flip fault injection                  |

## Roadmap

- [x] Linker scripts for bootloader and application partitions
- [x] Safe jump from bootloader to application
- [ ] ISO-TP receive/transmit
- [ ] UDS services (0x10, 0x31, 0x34, 0x36, 0x37)
- [ ] Flash erase/write driver
- [ ] CRC32 verification and metadata handling
- [ ] Power-loss recovery
- [ ] Python update client
- [ ] Unit tests and CI
- [ ] Measurements and demo video
- [ ] Optional: signature verification (e.g. ECDSA) instead of CRC only

## Repository layout

```
bootloader/   bootloader firmware
app/          example application
tools/        Python update client
docs/         diagrams, measurements, notes
```

## License

MIT (TODO: add LICENSE file)
