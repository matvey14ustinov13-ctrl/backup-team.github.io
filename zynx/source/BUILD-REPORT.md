# Zynx Linux — отчёт о сборке

Дата сборки: 2026-11-15

## Артефакт

| Параметр | Значение |
|---|---|
| ISO | `out/zynx-2026.11.15-x86_64.iso` |
| Размер | 1 205 796 864 байт (1149.93 MiB / 1.123 GiB) |
| SHA256 | `8842ab3a05333469160faacf5bb86706a9c9975da55cecb21795ce8384d8df25` |
| Метка тома | `ZYNX` |
| Загрузка | BIOS: syslinux; UEFI: systemd-boot |
| Ядро | `7.3.4-zynx` |
| Shell на tty1 | `/usr/bin/bash` (getty autologin root) |

Контрольная сумма продублирована в файле `*.iso.sha256`.

## Что изменилось по сравнению с arch-shell

- C++ shell полностью удалён: на tty1 работает настоящий bash из пакета
  `bash`, без самописных built-in'ов.
- `/etc/passwd`: shell пользователя root — `/usr/bin/bash` (в releng был
  `/usr/bin/zsh`, которого в ISO нет).
- `getty@tty1` больше не маскируется; используется releng-овский
  `autologin.conf` (`agetty --autologin root`).
- Добавлены `base-devel`, `bash-completion`, `nano`, `vim`, `git`,
  `man-db`, `man-pages`, `curl`, `wget`, `lm_sensors`.
- Zynx-брендинг: `hostname=zynx`, `/etc/os-release`, `/etc/motd`,
  оранжевый prompt, метка ISO `ZYNX`, заголовки меню загрузки.
- Заставка из `C:\Текстовый документ.txt` встроена в `/etc/issue`
  оранжевым (`ESC[33m`); syslinux-фон тоже заменён на неё.
- Ядро: `CONFIG_LOCALVERSION="-zynx"`, полная пересборка `make -j32`.
- Windows `.exe`/Wine не поддерживаются; работают нативные ELF-бинарники
  и пакеты Arch.

## Ядро

- Версия: `7.3.4-zynx`
- Исходники: kernel.org `linux-7.3.4.tar.xz`
- Конфигурация: официальная Arch `linux 7.3.4.arch1-1`, извлечённая из
  `vmlinuz` через `scripts/extract-ikconfig`.
- Сборка: `make -j32`.
- `CONFIG_DRM_AMDGPU=m`, `CONFIG_DRM_AMD_DC=y`,
  `CONFIG_DRM_AMD_DC_FP=y`, `CONFIG_LOCALVERSION="-zynx"`.
- В ISO входит `amdgpu.ko.zst` и прошивки AMD, включая GC 12.0 / DCN 4.2
  (RDNA4 / Navi48, RX 9070 XT).
- initramfs: zstd, хуки `archiso`, `archiso_loop_mnt`, `microcode`;
  микрокод `AuthenticAMD.bin` встроен.

## Проверки

### QEMU/KVM

Проверено на ISO `zynx-2026.11.15-x86_64.iso` (QEMU 11.1.3, KVM, 8 vCPU,
8 GiB RAM, OVMF):

- BIOS/syslinux: оранжевая заставка, заголовок `Zynx Linux`, пункт
  `Zynx Linux live medium (x86_64, BIOS)`; загрузка до tty1 с
  автологином и приглашением `root@zynx:~#` (`zynx-bios-*.png`).
- UEFI/systemd-boot: пункты `Zynx Linux live medium (x86_64, UEFI)` и
  `... with speech`; загрузка до того же tty1 (`zynx-uefi-*.png`).
- ISO дополнительно проверен как **raw-диск** (модель USB-флешки):
  BIOS/syslinux и UEFI/systemd-boot доходят до tty1
  (`zynx-raw-bios-*.png`, `zynx-raw-uefi-*.png`).
- Serial (ttyS0): вход root, `uname -r` → `7.3.4-zynx`;
  `SHELL=/usr/bin/bash`, `BASH_VERSION=5.3.20(1)-release`, login shell;
  история (`history 3`), Tab-дополнение (`ls /us<Tab>` → `/usr/`),
  job control (`sleep &`, `jobs`, `kill %1`), циклы, массивы, функции,
  `[[ ]]`, коды возврата — работают.
- Пакеты: `base-devel`, `bash-completion`, `gcc 16.2.1`, `make 4.4.1`,
  `vim`, `nano`, `git`, `man-db`, `curl`, `wget` установлены; `gcc`
  компилирует и запускает `ZYNC_GCC_OK`.
- `/etc/issue` начинается с `1b 5b 32 4a 1b 5b 48 1b 5b 33 33 6d`.
- `getty@tty1` и `NetworkManager` — active; PID 1 — systemd.
- Полные логи: `zynx-qemu-test.log`, `zynx-qemu-serial.log`,
  `zynx-qemu-raw-bios.log`, `zynx-qemu-raw-uefi.log`;
  расшифровка serial: `zynx-serial-transcript.txt` (очищенная версия —
  `zynx-serial-transcript-clean.txt`).
- Замечание: отдельной утилиты `hostname` в наборе нет; имя хоста задано
  в `/etc/hostname` и отображается в prompt. В чек-листе используется
  `cat /etc/hostname`.

### Реальная RX 9070 XT

На предыдущей сборке (`arch-shell`, ядро `7.3.4-archshell`) пользователь
сообщил, что тест прошёл успешно («все было отлично»); логи `lspci`/
`dmesg`/`vulkaninfo` не присылались. Конфигурация ядра для AMDGPU в Zynx
та же, но **новый ISO `zynx-2026.11.15` на реальной RX 9070 XT ещё не
проверялся**: флешка записана, тест нужно пройти по `HARDWARE-TEST.md`.

## Ограничения

- tty1 автоматически входит как root без пароля. Физический доступ к
  машине = root-доступ. Это осознанное решение для live-ISO.
- ISO работает из RAM: установленные пакеты и изменения исчезают после
  перезагрузки.
- Это консольная сборка; KDE Plasma / SDDM не добавлены.
- Windows `.exe` не запускаются; Wine не включён.

## Запись на USB (выполнено)

Флешка **Teclast CoolFlash** (Disk 5, 8 069 890 048 байт) прошита
2026-11-15 в raw/DD-режиме. После записи все 1 205 796 864 байт
перечитаны с устройства: SHA256 совпал с ISO
(`8842ab3a…d8df25`). Автомонтирование Windows на время записи было
отключено (`mountvol /N`), затем включено обратно.

Повторная запись:

Linux:

```bash
sudo dd if=zynx-2026.11.15-x86_64.iso of=/dev/sdX \
  bs=8M status=progress oflag=sync
```

Windows: Rufus, режим **DD Image mode**, не ISO mode.

Перед записью убедиться, что `/dev/sdX` (или диск в Rufus) — именно флешка.
