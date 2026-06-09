# TMC5160 SPI Linux Device Driver for Stepper Motor Control

> CDAC PG-Certificate in Embedded Systems Design — Final Project

## Overview

A Linux kernel character device driver running on the BeagleBone Black
that controls a stepper motor through the Trinamic TMC5160 motion
controller IC over the SPI bus. The driver exposes a clean interface at
`/dev/tmc5160` so any userspace application can command position,
velocity, and acceleration without knowing anything about the IC
internals. The TMC5160 handles ramp generation and coil current control
autonomously — the driver's job is to send the right SPI register writes
and surface motor status back to userspace.

## Hardware

| Component | Details |
|---|---|
| Single board computer | BeagleBone Black (AM335x, ARM Cortex-A8) |
| Motion controller IC | Trinamic TMC5160 |
| Validation MCU | STM32F407G-DISC1 |
| Stepper motor | FL42STH38-1684A, NEMA17, 1.68A/phase |
| Power supply | 24V 108W DC |

## Tech Stack

- Embedded Linux — kernel module development on BeagleBone Black
- Linux SPI subsystem — `spi_driver`, `spi_sync()`, 40-bit SPI datagrams
- Linux GPIO / IRQ subsystem — DIAG interrupt handling via `request_irq()`
- Device Tree overlay — hardware description for TMC5160 on McSPI1
- STM32 HAL — bare metal SPI and UART for Stage 1 hardware validation
- SPI protocol — full-duplex, Mode 3, 40-bit register datagrams

## Team

| Name | Email |
|---|---|
| Sanyam Choraria | sanyam1802@gmail.com |
| Joshi Avadhoot Kiran | avadhoot.joshi2402@gmail.com |
| Mande Swanand Suhas | mandeswanand7@gmail.com |
| Ganesh Ashok Patil | patilganesh413517@gmail.com |

## Project Stages

### Stage 1 — Bare Metal Validation (STM32F407)

Hardware-level validation of the TMC5160 and stepper motor before any
Linux driver work begins. The STM32F407G-DISC1 acts as SPI master,
communicates with the TMC5160 over SPI, and exposes a UART interface
to a PC so the user can send motion commands interactively.

Goals:
- Verify SPI communication with the TMC5160 at register level
- Demonstrate position mode and velocity mode motion
- Demonstrate sensorless stall detection via the DIAG interrupt pin
- Establish that the hardware chain is fully functional

**Expected outcome:** stepper motor moves correctly on user command,
stall events are reported over UART. Hardware is confirmed good before
driver development begins.

### Stage 2 — Embedded Linux Device Driver (BeagleBone Black)

The core of the project. A Linux kernel module (`tmc5160_drv.ko`) is
developed on the BeagleBone Black, replacing the STM32 from Stage 1.
The driver registers as an SPI device via Device Tree, creates a
character device at `/dev/tmc5160`, and exposes an ioctl interface for
motion control. A DIAG GPIO interrupt handler surfaces stall events to
userspace. Motor status (position, velocity, stall flag) is readable
via sysfs attributes.

Goals:
- SPI protocol driver using the Linux `spi_driver` framework
- Character device with ioctl API: `GOTO`, `SET_VMAX`, `SET_AMAX`, `READ_POS`
- Device Tree overlay declaring TMC5160 on McSPI1
- GPIO interrupt handler for TMC5160 DIAG pin (StallGuard2)
- sysfs attributes for motor state

**Expected outcome:** a userspace application commands a target position,
the motor moves there, and the current position is read back from the IC.

### Stage 3 — Userspace Interface and Polish

With the driver stable, the userspace layer is cleaned up into a
well-structured control application with a readable interface for
interactive use and scripting. Documentation and the final project
report are completed here.

**Final deliverable:** `tmc5160_drv.ko` — a working Linux kernel SPI
driver controlling a stepper motor on embedded hardware, with a clean
userspace interface and full project documentation.

## Repository Structure
```
.
├── stage1_stm32         # STM32 bare metal firmware (CubeIDE project)
├── stage2_driver        # Linux kernel module source
│   ├── tmc5160_drv.c
│   ├── Makefile
│   └── tmc5160.dtbo     # Device Tree overlay
├── stage3_userspace     # Userspace control application
├── docs                 # Schematics, wiring diagrams, project report
└── README.md
```
## Current Status

- [x] Stage 1 — In progress
- [ ] Stage 2 — Pending
- [ ] Stage 3 — Pending

##
