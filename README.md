# GRID OS

A from-scratch operating system: custom bootloader, custom 32-bit kernel,
custom shell. No Linux, no BSD — every byte of this OS is ours.
See `BLUEPRINT.md` for the full long-term design (the reactive cell-graph OS).

## Current status (v0.4 — storage milestone)

- Custom BIOS stage-1 bootloader (512 bytes): captures the e820 memory map,
  loads the kernel via BIOS int 13h (LBA with CHS fallback), enables the A20
  line and switches the CPU to 32-bit protected mode.
- 32-bit kernel: VGA text console with hardware cursor and scrolling,
  interrupt-driven PS/2 keyboard (IRQ1 ring buffer), PIT timer (100 Hz),
  RTC clock, CPUID reporting, e820 memory-map reporting.
- Interrupt subsystem: full IDT, remapped 8259 PIC, exception registry
  (`exc_register`) with a live page-fault handler.
- Memory subsystems: physical frame allocator (e820-driven bitmap, contiguous
  multi-frame runs via `pmm_alloc_frames`), kernel heap (`kmalloc`/`kfree`/
  `krealloc`, contiguous-backed growth), 2-level paging (identity-mapped
  first 16 MB, `vmap`/`vunmap`, page-fault reporting).
- **ATA storage driver (new in v0.4):** polling PIO LBA28 driver for the
  primary master (ports 0x1F0/0x3F6), IDENTIFY with ATAPI skip, sector
  read/write with busy/DRQ waits and cache flush.
- Interactive shell (`grid>`) with commands:
  `help`, `clear`, `echo`, `meminfo`, `uptime`, `cpuinfo`, `reboot`,
  `date`, `memstat`, `hexdump`, `sleep`, `rand`,
  `atainfo`, `ataread <lba>`.

## Build

Requires: `gcc` (with 32-bit codegen), `binutils` (as, ld, objcopy), `make`.

```
make          # produces grid-os.img
make clean    # remove build artifacts
```

Or download a ready-made image from the GitHub Actions artifacts
(`grid-os-img`, built on every push).

## Flash to a USB stick

**WARNING: this overwrites the target drive.** Double-check the device name.

Linux / macOS:

```
sudo dd if=grid-os.img of=/dev/sdX bs=4M status=progress && sync
```

(replace `/dev/sdX` with your USB stick — check with `lsblk`)

Or use [balenaEtcher](https://etcher.balena.io/) / Rufus (select the .img file).

## Boot it

1. Plug the USB stick into the laptop.
2. Enter the boot menu (usually F12, F9, Esc, or F2 at power-on).
3. Boot from the USB stick in **legacy/BIOS/CSM mode** (not UEFI-only).
4. You should see the GRID banner and the `grid>` prompt.

Notes:
- Requires legacy BIOS boot (CSM). UEFI-only machines need a UEFI
  bootloader, which is on the roadmap.
- Keyboards work through BIOS legacy USB emulation (SMI), which covers most
  laptops; native USB and PS/2 interrupt-driven input are next steps.
- **ATA disk access** requires the SATA controller in IDE/legacy/compatibility
  mode (most BIOSes default to this; QEMU's default `-hda` works too).
  Try `atainfo` at the prompt, then `ataread 0` to dump the first sector.

## Repository layout

```
BLUEPRINT.md           long-term OS design (reactive cell-graph model)
Makefile               build system
src/boot/boot.S        stage-1 bootloader (real mode -> protected mode)
src/boot/boot.ld       bootloader linker script
src/kernel/entry.S     kernel entry point (stack + bss setup)
src/kernel/kernel.ld   kernel linker script (linked at 0x8000)
src/kernel/main.c      kernel entry (kmain)
src/kernel/console.c   VGA text console
src/kernel/kbd.c       PS/2 keyboard driver (IRQ ring buffer)
src/kernel/idt.c       IDT, ISR dispatch, exception registry
src/kernel/isr.S       interrupt stubs (0-47)
src/kernel/rtc.c       CMOS real-time clock
src/kernel/pit.c       programmable interval timer (uptime)
src/kernel/mem.c       e820 memory map
src/kernel/pmm.c       physical frame allocator (bitmap, contiguous runs)
src/kernel/heap.c      kernel heap (kmalloc/kfree/krealloc)
src/kernel/paging.c    2-level paging, vmap, page-fault handler
src/kernel/ata.c       ATA PIO LBA28 disk driver
src/kernel/cpu.c       CPUID reporting
src/kernel/shell.c     interactive shell
src/kernel/util.c      freestanding libc bits
```

## Roadmap

1. **v0.2** ✅ — interrupt-driven input (IDT, PIC, keyboard IRQ), RTC clock
2. **v0.3** ✅ — physical memory allocator, kernel heap, paging
3. **v0.4** ✅ — ATA storage driver (PIO LBA28); UEFI boot pending
4. **v0.5** — filesystem (transaction log store per BLUEPRINT.md)
5. **v0.6** — userland processes, capability security
6. **v0.7** — graphics compositor, native USB HID
7. **v1.0** — the GRID reactive cell-graph userland (Phase 0 semantics in-kernel)
