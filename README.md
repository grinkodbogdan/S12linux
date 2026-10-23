# S12 Linux

A small Linux system for the **Lenovo IdeaPad S12, Intel version** (Atom N270, 945GSE chipset,
GMA 950 graphics, Broadcom BCM5906M Ethernet). It boots from a USB stick and runs entirely from
RAM: about 45 MB in use with the desktop running.

- **Kernel:** Linux 7.3, built for the Atom N270, with every driver of the S12 built in (no
  modules): i915 graphics, tg3 Ethernet, HD Audio, USB 1.1/2.0, SATA, SD cards, ACPI battery,
  AC adapter, lid, power button, fan and thermal zones, speed scaling, both processor threads.
- **Desktop:** S12 Desk, a small desktop drawn directly on the framebuffer with
  [microui](https://github.com/rxi/microui): windows, a menu, terminal, file manager, system
  monitor, calculator, minesweeper, settings (brightness, processor speed).
- **Web browser:** NetSurf 3.11 (framebuffer front end), with HTTPS and JavaScript.
- **Userland:** BusyBox 1.37 (ash shell and about 200 commands), curl, all static, built with
  musl through `zig cc`.
- **Network:** plug a cable in and an address comes from DHCP (`netd`, BusyBox `udhcpc`).
- **Wi-Fi:** Menu > Wi-Fi, or the `wifi` command (wpa_supplicant 2.12, WPA/WPA2). Drivers for the
  S12's cards (Broadcom BCM4312, Intel 5100, Atheros) and for common USB adapters.
- **Packages:** XBPS, Void Linux's package manager, with Void's i686 repository:
  `xbps-install -S nano`.
- **Data stick:** `s12-data setup` keeps Wi-Fi networks, firmware and installed packages on a
  USB stick or SD card, across reboots.
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

### Wi-Fi

Open **Menu > Wi-Fi**: pick a network, type its password, press Enter. From a terminal:

```sh
wifi scan                          # networks in range
wifi connect "My Network" password # joins it, and remembers it
wifi                               # state of the connection
wifi forget "My Network"
wifi off | on                      # the radio (the switch on the front of the S12 works too)
wifi country GB                    # allowed channels and power for your country
```

Most IdeaPad S12s have a **Broadcom BCM4312** card (driver b43). Its firmware cannot be
distributed, so it is extracted from Broadcom's driver on the laptop, once: connect the
Ethernet cable and click **Install firmware** in the Wi-Fi window, or run `wifi-firmware`. Without
a cable, copy `broadcom-wl-5.100.138.tar.bz2` to a USB stick and run
`wifi-firmware /path/to/broadcom-wl-5.100.138.tar.bz2`. With a data stick, this is done once
for good.

Intel 5100/5150/1000 cards, Atheros cards, and USB adapters with Ralink/MediaTek (rt2800),
Realtek (rtl8187, rtl8xxxu) and Atheros (ar9271) chips work without that step. Networks using
only WPA3 are not supported (wpa_supplicant is built without OpenSSL, to save RAM).

### Packages (XBPS)

S12 Linux uses Void Linux's package manager and its i686 repository (glibc), so thousands of
programs can be added:

```sh
xbps-install -S            # updates the list of packages
xbps-query -Rs editor      # searches
xbps-install nano          # installs (with what it needs)
xbps-remove -R nano        # removes
```

The XBPS tools are not in RAM at boot: the first command unpacks them from the boot stick (or
downloads them from Void). Packages are installed in RAM; manual pages and documentation are
removed after each installation to save it. Settings: `/usr/share/xbps.d` (repository,
architecture `i686`, packages the system provides itself), overridden by files of the same name
in `/etc/xbps.d` (for example to use a [mirror](https://docs.voidlinux.org/xbps/repositories/mirrors/)).
The system has Void's layout (`/bin`, `/sbin`, `/lib` link into `/usr`), so packages install
where they expect.

### Data stick

S12 Linux runs from RAM. To keep things across reboots, plug a USB stick or SD card (FAT, exFAT
or ext4) and run `s12-data setup` (or `s12-data setup /dev/sdb1`). A folder `s12linux` on it
then keeps the Wi-Fi networks, the Broadcom firmware, and the list of packages installed with
`xbps-install`, with their downloads. At boot they come back by themselves: Wi-Fi connects, and
the packages are installed again from the cache once the network is up.

### If something does not work

Open the Terminal and run `debug-test`. It lists the S12's chips and their drivers, tests each
part (`[ OK ]`, `[FAIL]`, `[ -- ]` not present), including Wi-Fi (card, driver, firmware, radio
switch, networks in range), runs both processor threads under load, and writes everything (PCI
devices, kernel log, `/sys` values, sound codec) to `/root/debug-test.txt`. Wi-Fi passwords are
left out of it. The log is also copied to any plugged USB stick that can be written.
`debug-test --upload` uploads it to termbin.com and prints a short link (the page is public;
it holds hardware details only).

## Build

On a Linux x86_64 machine with `gcc make flex bison bc perl xz cpio python3 curl` and
`grub-mkrescue` (packages `grub-pc-bin`, `grub-common`, `xorriso`, `mtools`):

```sh
./build.sh            # everything: about 30 minutes on 4 cores
./build.sh kernel image   # only some steps
```

Steps: `fetch toolchain kernel busybox desk web wifi rootfs image`. Downloads are checked against
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
| `wifi/` | wpa_supplicant build options |
| `rootfs/` | Files copied into the root file system: startup (`etc/rc`), network daemon (`netd`), DHCP script, power button, `wifi`, `wifi-firmware`, `s12-data`, the XBPS wrapper and settings, `debug-test` |
| `boot/grub.cfg` | Boot menu |

## How it starts

GRUB loads the kernel and the initramfs (xz-compressed, about 6 MB). The kernel unpacks it into
RAM and runs `/sbin/init` (`desk/init.c`), which runs `/etc/rc`: it mounts `/dev`, `/proc`,
`/sys`, `/run`, starts `acpid` (power button: clean shutdown) and `netd` (Ethernet, Wi-Fi,
DHCP, the data stick), then starts the desktop. If the desktop fails three times, a shell starts instead. Ctrl+Alt+Del reboots.

## Not supported yet

WPA3-only Wi-Fi networks, sleep, the webcam.
