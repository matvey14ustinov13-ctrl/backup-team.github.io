# Тест Zynx Linux на реальной RX 9070 XT

## 1. Запись на USB

ISO: `out/zynx-2026.11.15-x86_64.iso`

SHA256: `8842ab3a05333469160faacf5bb86706a9c9975da55cecb21795ce8384d8df25`

Флешка Teclast уже записана и проверена побайтово (2026-11-15); раздел
ниже нужен только для повторной записи.

**Windows (Rufus):**

1. Выбрать ISO.
2. Когда Rufus предложит режим — выбрать **DD Image mode**, не ISO mode.
3. Записать и дождаться завершения.

**Linux:**

```bash
sudo dd if=zynx-2026.11.15-x86_64.iso of=/dev/sdX \
  bs=8M status=progress oflag=sync
```

`/dev/sdX` — именно флешка, не диск с системой.

После DD-записи Windows может не показывать флешку в Проводнике и предлагать
её отформатировать. Это нормально: **отказаться**, букву диска не назначать.

## 2. Загрузка

- В UEFI **отключить Secure Boot**: загрузчик ISO не подписан.
- Выбрать USB в boot menu.
- UEFI: пункт `Zynx Linux live medium (x86_64, UEFI)`.
- BIOS: пункт `Zynx Linux live medium (x86_64, BIOS)` (не speech).
- Ожидаемо: оранжевая ASCII-заставка из `C:\Текстовый документ.txt`,
  строка `Zynx Linux` и приглашение bash `root@zynx:~#`.

tty1 — это настоящий `/usr/bin/bash`, запущенный через getty autologin.
Пароля нет: физический доступ к машине равен root-доступу.

## 3. Проверка bash

```text
uname -r
echo $SHELL
echo $BASH_VERSION
shopt -q login_shell && echo login-shell
type cd
history
```

Ожидаемо: ядро `7.3.4-zynx`, `SHELL=/usr/bin/bash`, `login-shell`,
`cd is a shell builtin`.

Автодополнение и история:

```text
ls /us<Tab>          # должно дополниться до /usr/
# Ctrl+R, набрать uname, Enter
```

Job control и код возврата:

```text
sleep 60 &
jobs
kill %1
false; echo $?
```

Язык bash:

```text
for i in 1 2 3; do echo "i=$i"; done
arr=(one two); echo "${arr[1]}"
f(){ echo "func:$1"; }; f ok
[[ 5 -gt 3 ]] && echo test-ok
```

## 4. Arch-совместимость и base-devel

```text
pacman -Q base-devel bash-completion gcc make vim nano git man-db man-pages curl wget lm_sensors
gcc --version | head -n 1
make --version | head -n 1
printf '#include <stdio.h>\nint main(void){puts("zynx-gcc-ok");return 0;}\n' >/tmp/hello.c
gcc -O2 -o /tmp/hello /tmp/hello.c && /tmp/hello
cat /etc/os-release
cat /etc/hostname
```

Ожидаемо: все пакеты установлены, `gcc` и `make` работают,
программа печатает `zynx-gcc-ok`, `cat /etc/hostname` — `zynx`,
`os-release` содержит `NAME="Zynx Linux"` и `ID=zynx`.

## 5. Проверка GPU (RX 9070 XT)

```text
cat /proc/cmdline
lspci -nnk | grep -A3 -i 'vga\|3d\|display'
ls -l /dev/dri
dmesg | grep -iE 'amdgpu|firmware' | tail -n 60
vulkaninfo --summary | head -n 40
vainfo
sensors
grep -H . /sys/class/drm/card*/device/hwmon/hwmon*/temp*_input
```

Что должно быть:

- в `cmdline` **нет** `nomodeset`;
- `lspci`: `Kernel driver in use: amdgpu`, `Kernel modules: amdgpu`;
- `/dev/dri`: `card0` и `renderD128`;
- `dmesg`: прошивки загружаются, нет `failed to load firmware`,
  `ring ... timeout`, `GPU reset`, `VM_L2_PROTECTION_FAULT`;
- `vulkaninfo`: `AMD Radeon RX 9070 XT (RADV GFX1200)` или похожее;
- `vainfo`: профили H.264 / HEVC / AV1 без ошибок;
- `sensors`: температуры `edge`/`junction`/`mem` (в миллиградусах:
  `45000` = 45 °C).

## 6. Если чёрный экран

Загрузиться с параметром `nomodeset`, чтобы получить консоль:

- UEFI/systemd-boot: на меню нажать **e**, дописать `nomodeset` в строку
  `options`, Enter.
- BIOS/syslinux: на меню нажать **Tab**, дописать `nomodeset`, Enter.

Затем собрать:

```text
dmesg | grep -i amdgpu | tail -n 80
```

и сфотографировать.

## 7. Что прислать после теста

Достаточно фото экрана или текста:

1. фото заставки и приглашения `root@zynx:~#`;
2. `uname -r`;
3. `echo $BASH_VERSION` и `pacman -Q base-devel bash-completion`;
4. `gcc --version | head -n 1` и результат `/tmp/hello`;
5. `lspci -nnk | grep -A3 -i 'vga\|3d\|display'`;
6. `ls -l /dev/dri`;
7. `dmesg | grep -iE 'amdgpu|firmware' | tail -n 60`;
8. `vulkaninfo --summary | head -n 40`;
9. `vainfo`;
10. `sensors`.

## Важно

- tty1 автоматически входит как root без пароля. Это live-ISO.
- ISO не устанавливает систему на диск: изменения живут в RAM и исчезают
  после перезагрузки.
- Windows `.exe` не поддерживаются; нативные Linux ELF-бинарники и пакеты
  Arch (`pacman -S ...`) работают.
- После теста выключить машину, включить Secure Boot обратно и загрузить
  Windows.
