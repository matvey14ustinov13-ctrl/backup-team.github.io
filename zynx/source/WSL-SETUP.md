# Сборка ISO в WSL2

## 1. После перезагрузки Windows

WSL и VirtualMachinePlatform уже включены, но активируются только после reboot.

В **PowerShell от администратора**:

```powershell
wsl --update
wsl --install archlinux --no-launch
```

Если `archlinux` не находится в списке, скачай свежий образ
<https://fastly.mirror.pkgbuild.com/wsl/latest/> и поставь его:

```powershell
wsl --install --from-file C:\путь\archlinux-XXXX.XX.XX.XXXXXX.wsl --no-launch
```

## 2. Запуск Arch

```powershell
wsl -d archlinux
```

Внутри Arch:

```bash
pacman -Syu --noconfirm
cp -a "/mnt/c/MATEYKA/OPENCODE AI" ~/arch-amdgpu
cd ~/arch-amdgpu
bash build-arch-amdgpu-shell.sh
```

## 3. Важно

- Собирай **только в `~/arch-amdgpu`**, не в `/mnt/c/...`. Сборка ядра на 9p-
  файловой системе Windows идёт очень медленно и может ломаться на правах.
- RAM в WSL ограничена через `C:\Users\Андрей\.wslconfig`:
  `memory=24GB`, `processors=32`, `swap=8GB`. После изменения:
  `wsl --shutdown`.
- ISO появится в `~/arch-amdgpu/out/`. Скопировать на Windows:
  `cp ~/arch-amdgpu/out/*.iso "/mnt/c/MATEYKA/OPENCODE AI/out/"`.
- `mkarchiso` официально не поддерживает WSL. Если `arch-chroot`/`mount`
  упрётся в ограничения WSL2, fallback — Arch в VirtualBox/VM
  (ISO `C:\MATEYKA\archlinux-2026.11.01-x86_64.iso` уже есть).
