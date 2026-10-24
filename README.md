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
- **Touchpad:** two fingers, or one finger along the right edge, scroll; tap to click, tap with
  two fingers for a right click (`tpd`). A mouse wheel scrolls too.
- **Terminal:** 1000 lines of history: scroll with the touchpad or wheel, drag the bar on the
  right, or Alt+Page Up / Alt+Page Down.
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

### Installing on the internal disk

Menu > **Install to Disk** (or `s12-install` in a terminal) installs S12 Linux on the laptop's
hard disk. **The whole disk is erased**, Windows included: it shows the disk and asks you to type
`ERASE`. It needs the boot stick plugged in and the network (Wi-Fi or Ethernet), for `mkfs.ext4`
and GRUB from Void's repository. The disk gets one ext4 partition and a 512 MB swap file; files,
settings and packages then stay across reboots, and the RAM is free for programs. Remove the stick
and restart.

### Modern web browser (Badwolf)

On a system installed on the disk, run `install-browser`: it installs
[Badwolf](https://hacktivis.me/projects/badwolf), a minimal browser built on WebKitGTK (the engine
of Safari and GNOME Web, so modern sites work), from Void's repository (about 250 MB), with the
[cage](https://github.com/cage-kiosk/cage) Wayland compositor, which shows it full screen. Menu >
Web Browser then starts it. Ctrl+L: address, Ctrl+T: new tab, Ctrl+PageUp/PageDown: switch tabs,
Alt+D: close the tab, the × at the top right: back to the desktop. JavaScript is on (the JS button
turns it off for a tab). NetSurf, lighter still, stays available as `netsurf`.

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

Most IdeaPad S12s have a **Broadcom BCM4312** card (driver b43). `build.sh` extracts its
firmware from Broadcom's driver into the image (with `b43-fwcutter`, as OpenWrt does), so Wi-Fi
works at the first boot. Broadcom does not allow this firmware to be redistributed: keep images
built this way for your own use, and build with `B43_FIRMWARE=0` for an image to share. Such an
image extracts the firmware on the laptop instead: connect the Ethernet cable and click **Install
firmware** in the Wi-Fi window, or run `wifi-firmware` (or `wifi-firmware FILE` with
`broadcom-wl-5.100.138.tar.bz2` on a USB stick).

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
left out of it. The log is also copied to USB sticks and SD cards (never to the laptop's disk). The log is also copied to any plugged USB stick that can be written.
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
| `desk/` | The desktop (`desk`), the init program (`minit`), the touchpad daemon (`tpd`) and `soundtest` |
| `netsurf/` | Patch for NetSurf (framebuffer surface for a raw console with mouse wheel, Ctrl+L, focus), build settings (`Makefile.config`), browser settings |
| `wifi/` | wpa_supplicant build options |
| `rootfs/` | Files copied into the root file system: startup (`etc/rc`), network daemon (`netd`), DHCP script, power button, `wifi`, `wifi-firmware`, `s12-data`, `s12-install`, `install-browser`, `browser-start`, the XBPS wrapper and settings, `debug-test` |
| `boot/grub.cfg` | Boot menu |

## How it starts

GRUB loads the kernel and the initramfs (xz-compressed, about 6 MB). The kernel unpacks it into
RAM and runs `/sbin/init` (`desk/init.c`), which runs `/etc/rc`: it mounts `/dev`, `/proc`,
`/sys`, `/run`, starts `acpid` (power button: clean shutdown) and `netd` (Ethernet, Wi-Fi,
DHCP, the data stick), then starts the desktop. If the desktop fails three times, a shell starts instead. Ctrl+Alt+Del reboots.

## Not supported yet

WPA3-only Wi-Fi networks, sleep, the webcam.
