# Zynx Linux 1.0 — installation guide / руководство по установке

This archive contains:

```
zynx-YYYY.MM.DD-x86_64.iso   the live/install image
zynx-YYYY.MM.DD-x86_64.iso.sha256
INSTALL.txt                  short plain-text guide (English + Russian)
README.md                    this file
```

> **The download is a split ZIP.** GitHub limits one release asset to 2 GiB and
> the image is larger, so the archive is published in volumes:
> `zynx-1.0-release.z01` (first) and `zynx-1.0-release.zip` (last).
> Download **all** volumes into one folder and extract the `.zip` with
> [7-Zip](https://www.7-zip.org/) (it reads `.z01` automatically), WinRAR, or on
> Linux/macOS:
>
> ```bash
> zip -s 0 zynx-1.0-release.zip --out zynx-1.0-full.zip
> unzip zynx-1.0-full.zip
> ```

---

## English

### 1. Check the download

```bash
sha256sum -c zynx-YYYY.MM.DD-x86_64.iso.sha256
```

On Windows (PowerShell):

```powershell
Get-FileHash .\zynx-YYYY.MM.DD-x86_64.iso -Algorithm SHA256
```

Compare the result with the `.sha256` file.

### 2. Write the ISO to a USB drive

The image is a hybrid ISO and boots on both UEFI and BIOS computers.

Linux / macOS:

```bash
sudo dd if=zynx-YYYY.MM.DD-x86_64.iso of=/dev/sdX bs=8M status=progress oflag=sync
```

Replace `/dev/sdX` with the whole USB device (for example `/dev/sdb`), **not** a
partition such as `/dev/sdb1`.

Windows: use [Rufus](https://rufus.ie/) in **DD mode**, or
[balenaEtcher](https://www.balena.io/etcher/).

### 3. Boot the live medium

Open the firmware boot menu (usually F12, F11 or Esc), select the USB drive and
choose **Zynx Linux**. The live desktop logs in automatically as the user
`zynx`; tty1 provides a root shell without a password.

### 4. Run the installer

Start **Install Zynx Linux** from the desktop. The installer is a single GTK3
program written in C — no terminal and no Python are involved.

Steps:

1. **Edition** — XFCE Full, XFCE Minimal or Console; choose the login shell
   (bash, zsh or fish).
2. **Disk** — select the drive. The switch **Format the disk** is **off by
   default**:
   * off — the existing partition table is kept, the largest Linux root
     filesystem is reused, and `/home` is preserved;
   * on — the disk is repartitioned into BIOS boot (1 MiB), ESP (512 MiB),
     root (the size you choose) and `/home` (the rest).
3. **User** — user name, password, optional root password and computer name.
   Leaving the root password blank locks root; use `sudo` instead.
4. **Settings** — time zone, system language (English by default), keyboard,
   root partition size, swap file size (none, 2, 4 or 8 GiB) and whether Zynx
   becomes the default boot entry.
5. **Review** — check the summary and the partition plan, type the confirmation
   word and start the installation.

The machine must have at least 12 GiB of disk space. During installation the
installer copies the live system, installs GRUB for BIOS and UEFI, creates the
user, writes `/etc/fstab`, enables a swap file and turns on Magic SysRq.

### 5. After installation

Remove the USB drive and reboot. Log in with the account you created.
Administrative commands need `sudo` (the root account is locked unless you set
a root password). Update the system with:

```bash
sudo pacman -Syu
```

### Emergency reboot (SysRq)

If the machine freezes, hold **Alt** and press **SysRq**, then type
**R E I S U B** one key at a time. This writes cached data to disk and reboots
safely. SysRq is enabled on the live medium and in the installed system.

---

## Русский

### 1. Проверка загрузки

```bash
sha256sum -c zynx-YYYY.MM.DD-x86_64.iso.sha256
```

В Windows (PowerShell):

```powershell
Get-FileHash .\zynx-YYYY.MM.DD-x86_64.iso -Algorithm SHA256
```

Сравните результат с файлом `.sha256`.

### 2. Запись ISO на USB

Образ гибридный: загружается и на UEFI, и на BIOS.

Linux / macOS:

```bash
sudo dd if=zynx-YYYY.MM.DD-x86_64.iso of=/dev/sdX bs=8M status=progress oflag=sync
```

`/dev/sdX` — это весь USB-накопитель (например `/dev/sdb`), а **не** раздел
`/dev/sdb1`.

Windows: [Rufus](https://rufus.ie/) в режиме **DD** или
[balenaEtcher](https://www.balena.io/etcher/).

### 3. Загрузка

Откройте меню загрузки прошивки (обычно F12, F11 или Esc), выберите USB и
пункт **Zynx Linux**. Живой рабочий стол входит автоматически под пользователем
`zynx`; на tty1 доступна root-консоль без пароля.

### 4. Установка

Запустите **Install Zynx Linux** на рабочем столе. Установщик — одна программа
на C + GTK3, без терминала и без Python.

1. **Edition** — XFCE Full, XFCE Minimal или Console; выберите командную
   оболочку (bash, zsh или fish).
2. **Disk** — выберите диск. Переключатель **Format the disk** по умолчанию
   **выключен**:
   * выключен — таблица разделов сохраняется, используется самый большой
     существующий Linux-раздел, `/home` не трогается;
   * включён — диск размечается заново: BIOS boot (1 МиБ), ESP (512 МиБ),
     root (выбранного размера) и `/home` (остаток).
3. **User** — имя пользователя, пароль, необязательный пароль root и имя
   компьютера. Пустой пароль root блокирует root, используйте `sudo`.
4. **Settings** — часовой пояс, язык системы (по умолчанию английский),
   клавиатура, размер root, размер swap-файла (нет, 2, 4 или 8 ГиБ) и пункт
   «Сделать Zynx системой по умолчанию».
5. **Review** — проверьте сводку и план разметки, введите слово подтверждения
   и запустите установку.

Минимум 12 ГиБ свободного места. Установщик копирует живую систему, ставит
GRUB для BIOS и UEFI, создаёт пользователя, записывает `/etc/fstab`, включает
swap-файл и Magic SysRq.

### 5. После установки

Извлеките USB и перезагрузитесь. Войдите под созданным пользователем.
Административные команды — через `sudo`. Обновление системы:

```bash
sudo pacman -Syu
```

### Аварийная перезагрузка (SysRq)

Если система зависла, зажмите **Alt** и нажмите **SysRq**, затем по одной
клавише **R E I S U B**. Система сбросит данные на диск и безопасно
перезагрузится. SysRq включён и на живом носителе, и в установленной системе.

---

## Notes / Примечания

* Interface language of the installer and the installed system: English
  (default locale `en_US.UTF-8`). You can change it in **Settings**.
* The ISO installs offline: no network is needed during installation.
* Secure Boot must be disabled; the medium is not signed.
* The project source is published at
  <https://github.com/matvey14ustinov13-ctrl/backup-team.github.io>.
