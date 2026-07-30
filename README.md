# TMC5160 SPI+GPIO Linux Device Driver for Stepper Motor Control

> CDAC PG-Certificate in Embedded Systems Design — Final Project

## Overview

A Linux kernel character device driver runs on the BeagleBone Black and controls a stepper motor through the Trinamic TMC5160 IC. The driver uses two interfaces to the IC. SPI writes configuration registers and reads status. GPIO lines carry the STEP and DIR signals that produce motor motion, and the DIAG lines that signal fault and stall events. The driver exposes `/dev/tmc5160` so a userspace application can command rotation angle, speed, and acceleration without knowing the IC internals.

## Hardware

| Component | Details |
|---|---|
| Single board computer | BeagleBone Black (AM335x, ARM Cortex-A8) |
| Motion controller IC | Trinamic TMC5160 |
| Validation MCU | STM32F407G-DISC1 |
| Stepper motor | FL42STH38-1684A, NEMA17, 1.68 A/phase |
| Power supply | 24 V, 108 W DC |

## Tech Stack

- Embedded Linux — kernel module development on BeagleBone Black
- Linux SPI subsystem — `spi_driver`, `spi_sync()`, 40-bit SPI datagrams
- Linux GPIO subsystem — STEP and DIR output lines via the `gpio_desc` descriptor API
- Linux IRQ subsystem — DIAG0 and DIAG1 interrupt handling via `request_irq()`
- hrtimer — high-resolution kernel timer for STEP pulse generation and velocity ramp
- Device Tree overlay — hardware description for TMC5160 on McSPI1 with GPIO pin assignments
- STM32 HAL — bare-metal SPI and UART for Stage 1 hardware validation
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

This stage validates the hardware before Linux driver work starts. The STM32F407G-DISC1 acts as SPI master. It configures the TMC5160 over SPI and drives motion using GPIO STEP and DIR pulses. A UART interface sends status to a PC.

Goals:
- Verify SPI communication with the TMC5160 at register level
- Drive motor motion using GPIO STEP and DIR signals
- Detect stall events via the DIAG GPIO interrupt pin
- Confirm that the full hardware chain works correctly

**Expected outcome:** the motor moves on command and the UART log shows status and fault events. The hardware is confirmed good before driver development starts.

### Stage 2 — Embedded Linux Device Driver (BeagleBone Black)

This stage is the core of the project. A Linux kernel module (`tmc5160_drv.ko`) runs on the BeagleBone Black and replaces the STM32 from Stage 1. The driver registers as an SPI and GPIO device through the Device Tree. It creates a character device at `/dev/tmc5160`. An hrtimer in the kernel generates STEP pulses and controls the velocity ramp. A GPIO interrupt handler on the DIAG line reports stall and fault events to userspace. Sysfs attributes expose motor state.

Goals:
- SPI protocol driver using the Linux `spi_driver` framework
- GPIO output driver using the Linux descriptor API for STEP and DIR lines
- hrtimer-based step pulse generation with trapezoidal velocity ramp
- Character device with an interface to command rotation, current, and speed
- Device Tree overlay declaring the TMC5160 on McSPI1 with all GPIO pin assignments
- GPIO interrupt handler for TMC5160 DIAG0 (driver fault) and DIAG1 (stall detection)
- Sysfs attributes for motor position, speed, and fault status

**Expected outcome:** a userspace application commands a target angle and the motor moves there. The driver reports position and fault state through sysfs.

### Stage 3 — Userspace Interface and Polish

With the driver stable, the userspace layer becomes a structured control application. The application takes a physical displacement in centimeters, converts it to a rotation angle using the lead screw pitch, and sends the angle to the driver through `/dev/tmc5160`. Documentation and the final project report are also completed in this stage.

**Final deliverable:** `tmc5160_drv.ko` — a working Linux kernel SPI and GPIO driver that controls a stepper motor on embedded hardware, with a userspace linear actuator application and full project documentation.

## Repository Structure

```
.
├── stage1_stm32         # STM32 bare metal firmware (CubeIDE project)
├── stage2_driver        # Linux kernel module source
│   ├── tmc5160_hw.c     # SPI register access and GPIO hardware primitives
│   ├── tmc5160_motion.c # hrtimer step engine and velocity ramp
│   ├── tmc5160_cdev.c   # character device interface and sysfs attributes
│   ├── tmc5160_main.c   # probe/remove, spi_driver registration, module init/exit
│   ├── tmc5160.h        # shared structs, ioctl numbers, function prototypes
│   ├── Makefile
│   └── tmc5160.dtbo     # Device Tree overlay
├── stage3_userspace     # Userspace linear actuator control application
├── docs                 # Schematics, wiring diagrams, project report
├── refdocs              # Reference documents and datasheets
└── README.md
```

## Current Status

- [x] Stage 1 — Completed
- [ ] Stage 2 — In progress
- [ ] Stage 3 — Pending
