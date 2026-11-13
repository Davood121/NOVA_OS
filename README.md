# GRID OS

A from-scratch operating system: custom bootloader, custom 32-bit kernel,
custom shell. No Linux, no BSD — every byte of this OS is ours.
See `BLUEPRINT.md` for the full long-term design (the reactive cell-graph OS).

## Current status (v0.6 — UEFI boot milestone)

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
- **ATA storage driver (v0.4):** polling PIO LBA28 driver for the
  primary master (ports 0x1F0/0x3F6), IDENTIFY with ATAPI skip, sector
  read/write with busy/DRQ waits and cache flush.
- **Block cache (v0.5):** 512-byte sector cache over the ATA driver with
  hits/misses/dirty tracking and write-back flush.
- **GRIDFS transaction-log filesystem (v0.5):** superblock at LBA 0,
  A/B file-table copies with generation bit for crash-consistent commits,
  append-only data log. Auto-formats a blank disk at boot. 32 files max,
  24-char names, files up to 4 KB readable via shell.
- **UEFI boot (new in v0.6):** hand-rolled PE32+ EFI loader (`uefi/`), no
  gnu-efi, no EDK2 — the PE headers are written by hand in assembly. The
  loader kills the watchdog, captures the UEFI memory map and converts it
  to e820 for the kernel, copies the kernel to 0x8000, forces VGA text
  mode, scrubs the firmware CPU state (paging, PAE, EFER long-mode bits),
  rebuilds the kernel's GDT and drops to 32-bit protected mode. Both boot
  paths (legacy BIOS and UEFI) are boot-tested on every push in CI.
- Interactive shell (`grid>`) with commands:
  `help`, `clear`, `echo`, `meminfo`, `memstat`, `hexdump`, `sleep`, `rand`,
  `blkstat`, `atainfo`, `ataread <lba>`, `ls`, `write <name> <text>`,
  `cat <name>`, `rm <name>`, `fsinfo`, `mkfs yes`, `uptime`, `date`,
  `cpuinfo`, `reboot`.

## Build

Requires: `gcc` (with 32-bit codegen), `binutils` (as, ld, objcopy), `make`.

```
make          # produces grid-os.img
make clean    # remove build artifacts
```

Or download ready-made binaries from the GitHub Actions artifacts
(`grid-os-img`, built on every push): `grid-os.img` for legacy BIOS boot
and `build/BOOTX64.EFI` for UEFI boot.

To build the UEFI loader locally:

```
make uefi     # produces build/BOOTX64.EFI
```

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
3. Boot from the USB stick — **either legacy/BIOS/CSM mode or UEFI mode
   both work now**.
4. You should see the GRID banner and the `grid>` prompt.

Notes:
- **UEFI boot:** use the `BOOTX64.EFI` from the CI artifact instead of
  dd-ing the whole image (see the UEFI section below for the USB setup).
- Keyboards work through BIOS legacy USB emulation (SMI), which covers most
  laptops; native USB and PS/2 interrupt-driven input are next steps.
- **ATA disk access** requires the SATA controller in IDE/legacy/compatibility
  mode (most BIOSes default to this; QEMU's default `-hda` works too).
  Try `atainfo` at the prompt, then `ataread 0` to dump the first sector.
- **Files persist on the USB stick**: `write note hello`, `cat note`,
  `ls`, `fsinfo`. A blank disk is auto-formatted at boot; `mkfs yes`
  reformats (erases everything).

## Booting via UEFI (USB stick)

1. Partition the USB stick with a **GPT** table and one FAT32 partition
   (most OS tools do this in two clicks).
2. Copy `BOOTX64.EFI` from the CI artifact to `EFI/BOOT/BOOTX64.EFI` on
   the FAT32 partition (create the folders if needed).
3. Boot the stick in **UEFI mode**.

The loader converts the UEFI memory map to the e820 format the kernel
expects, so the kernel itself is identical on both boot paths.

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
src/kernel/blkcache.c  512-byte block cache (write-back)
src/kernel/fs.c        gridfs transaction-log filesystem
src/kernel/cpu.c       CPUID reporting
src/kernel/shell.c     interactive shell
src/kernel/util.c      freestanding libc bits
uefi/uefi.S            hand-rolled PE32+ UEFI loader (embeds kernel.bin)
uefi/uefi.ld           UEFI loader linker script
```

## Roadmap

1. **v0.2** ✅ — interrupt-driven input (IDT, PIC, keyboard IRQ), RTC clock
2. **v0.3** ✅ — physical memory allocator, kernel heap, paging
3. **v0.4** ✅ — ATA storage driver (PIO LBA28)
4. **v0.5** ✅ — block cache + gridfs transaction-log filesystem
5. **v0.6** ✅ — UEFI boot (hand-rolled PE32+ loader, CI-tested on OVMF)
6. **v0.7** — userland processes, capability security, graphics compositor
7. **v1.0** — the GRID reactive cell-graph userland (Phase 0 semantics in-kernel)
