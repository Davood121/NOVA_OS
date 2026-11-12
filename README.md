# GRID OS (NOVA OS kernel)

A from-scratch operating system: custom bootloader, custom 32-bit kernel,
custom shell. No Linux, no BSD — every byte is ours.
See `BLUEPRINT.md` for the long-term design (the reactive cell-graph OS).

## Current status (v0.2 — interrupts milestone)

- Custom BIOS stage-1 bootloader (512 bytes): captures the e820 memory map,
  loads the kernel via BIOS int 13h (LBA with CHS fallback), enables the A20
  line and switches the CPU to 32-bit protected mode.
- 32-bit kernel: VGA text console, PIT timer, CPUID reporting,
  e820 memory-map reporting.
- **v0.2:** full interrupt support — IDT with exception gates, remapped 8259
  PIC, IRQ-driven keyboard (ring buffer, CPU sleeps via `hlt` while idle),
  IRQ-driven timer ticks, kernel panic handler for CPU exceptions, and an
  RTC (CMOS) driver with BCD/12-hour handling.
- Interactive shell (`grid>`) with commands:
  `help`, `clear`, `echo`, `meminfo`, `uptime`, `date`, `cpuinfo`, `reboot`.

## Build

Requires: `gcc` (with 32-bit codegen), `binutils` (as, ld, objcopy), `make`.

```
make          # produces grid-os.img
make clean    # remove build artifacts
```

CI builds the image on every push — download `grid-os-img` from the
[Actions runs](https://github.com/Davood121/NOVA_OS/actions) page.

## Flash to a USB stick

**WARNING: this overwrites the target drive.** Double-check the device name.

```
sudo dd if=grid-os.img of=/dev/sdX bs=4M status=progress && sync
```

Or use balenaEtcher / Rufus (select the .img file).

## Boot it

1. Plug the USB stick into the laptop.
2. Enter the boot menu (usually F12, F9, Esc, or F2 at power-on).
3. Boot from the USB stick in **legacy/BIOS/CSM mode** (not UEFI-only).
4. You should see the GRID banner and the `grid>` prompt.
5. Try `date` for the real-time clock, `meminfo` for your RAM map,
   `cpuinfo` for your CPU model.

Notes:
- Requires legacy BIOS boot (CSM). UEFI-only machines need a UEFI
  bootloader, which is on the roadmap.
- Keyboards work through BIOS legacy USB emulation (SMI).

## Repository layout

```
BLUEPRINT.md           long-term OS design (reactive cell-graph model)
Makefile               build system
src/boot/boot.S        stage-1 bootloader (real mode -> protected mode)
src/boot/boot.ld       bootloader linker script
src/kernel/entry.S     kernel entry point (stack + bss setup)
src/kernel/isr.S       interrupt stubs (exceptions + IRQs)
src/kernel/idt.c       IDT, PIC remap, IRQ dispatch, panic handler
src/kernel/rtc.c       CMOS real-time clock driver
src/kernel/kernel.ld   kernel linker script (linked at 0x8000)
src/kernel/main.c      kernel entry (kmain)
src/kernel/console.c   VGA text console
src/kernel/kbd.c       IRQ-driven PS/2 keyboard driver
src/kernel/mem.c       e820 memory map
src/kernel/pit.c       IRQ-driven programmable interval timer
src/kernel/cpu.c       CPUID reporting
src/kernel/shell.c     interactive shell
src/kernel/util.c      freestanding libc bits
```

## Roadmap

1. ~~**v0.2** — interrupt-driven input (IDT, PIC, keyboard IRQ), RTC clock~~ ✅
2. **v0.3** — physical memory allocator (`kmalloc`/`kfree`), heap, paging
3. **v0.4** — UEFI boot support
4. **v0.5** — filesystem (transaction log store per BLUEPRINT.md)
5. **v0.6** — userland processes, capability security
6. **v0.7** — graphics compositor, native USB HID
7. **v1.0** — the GRID reactive cell-graph userland
