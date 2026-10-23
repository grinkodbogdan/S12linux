# S12 Linux

A small Linux system for the **Lenovo IdeaPad S12, Intel version** (Atom N270, 945GSE chipset,
GMA 950 graphics, Broadcom BCM5906M Ethernet). It boots from a USB stick and runs entirely from
RAM: about 40 MB in use with the desktop running.

- **Kernel:** Linux 7.3, built for the Atom N270, with every driver of the S12 built in (no
  modules): i915 graphics, tg3 Ethernet, HD Audio, USB 1.1/2.0, SATA, SD cards, ACPI battery,
  AC adapter, lid, power button, fan and thermal zones, speed scaling, both processor threads.
- **Desktop:** S12 Desk, a small desktop drawn directly on the framebuffer with
  [microui](https://github.com/rxi/microui): windows, a menu, terminal, file manager, system
  monitor, calculator, minesweeper, settings (brightness, processor speed).
- **Web browser:** NetSurf 3.11 (framebuffer front end), with HTTPS and JavaScript.
- **Userland:** BusyBox 1.37 (hush shell and about 200 commands), curl, all static, built with
  musl through `zig cc`.
- **Network:** plug a cable in and an address comes from DHCP (`netd`, BusyBox `udhcpc`).
- **Diagnostics:** `debug-test` checks every part of the laptop and writes a full log.

## Use

1. Write `s12-linux.iso` to a USB stick (it is a hybrid ISO): with
   [Rufus](https://rufus.ie) in DD mode, [balenaEtcher](https://etcher.balena.io), or
   `sudo dd if=s12-linux.iso of=/dev/sdX bs=4M conv=fsync` (the whole device, not a partition).
2. Plug it into the S12, press **F12** at the Lenovo logo and pick the USB stick.
3. Boot menu:

   | Entry | Use |
   |---|---|
   | S12 Linux | The desktop (default) |
   | S12 Linux (text console) | A shell, no desktop; `desk` starts it |
   | S12 Linux (safe graphics) | Keeps the BIOS framebuffer, without the GMA driver |
   | S12 Linux (debug) | All kernel messages, one processor thread, text console |

Nothing is written to the laptop's disk. Files you create live in RAM and are gone after a
reboot; save them on a USB stick (`mount /dev/sdb1 /mnt`).

### If something does not work

Open the Terminal and run `debug-test`. It lists the S12's chips and their drivers, tests each
part (`[ OK ]`, `[FAIL]`, `[ -- ]` not present), runs both processor threads under load, and
writes everything (PCI devices, kernel log, `/sys` values, sound codec) to
`/root/debug-test.txt`. The log is also copied to any plugged USB stick that can be written.
`debug-test --upload` uploads it to termbin.com and prints a short link (the page is public;
it holds hardware details only).

## Build

On a Linux x86_64 machine with `gcc make flex bison bc perl xz cpio python3 curl` and
`grub-mkrescue` (packages `grub-pc-bin`, `grub-common`, `xorriso`, `mtools`):

```sh
./build.sh            # everything: about 30 minutes on 4 cores
./build.sh kernel image   # only some steps
```

Steps: `fetch toolchain kernel busybox desk web rootfs image`. Downloads are checked against
the SHA-256 sums in `sources.sh`. Everything happens in `./work` (or `$WORK`); the image is
`work/s12-linux.iso`.

## Layout

| Path | Contents |
|---|---|
| `build.sh`, `sources.sh` | Build script, and the sources with their checksums |
| `kernel/s12.config` | Kernel options, applied on top of `i386_defconfig` |
| `busybox/config` | BusyBox configuration |
| `desk/` | The desktop (`desk`), the init program (`minit`) and `soundtest` |
| `netsurf/` | Patch for NetSurf (framebuffer surface for a raw console, Ctrl+L, focus), settings |
| `rootfs/` | Files copied into the root file system: startup (`etc/rc`), network daemon (`netd`), DHCP script, power button, `debug-test` |
| `boot/grub.cfg` | Boot menu |

## How it starts

GRUB loads the kernel and the initramfs (xz-compressed, about 6 MB). The kernel unpacks it into
RAM and runs `/sbin/init` (`desk/init.c`), which runs `/etc/rc`: it mounts `/dev`, `/proc`,
`/sys`, `/run`, starts `acpid` (power button: clean shutdown) and `netd`, then starts the
desktop. If the desktop fails three times, a shell starts instead. Ctrl+Alt+Del reboots.

## Not supported yet

Wi-Fi (Broadcom BCM4312 needs the b43 driver and firmware that cannot be redistributed),
sleep, the webcam.
