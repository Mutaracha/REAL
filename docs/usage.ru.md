# REAL 0.3 — что изменилось и как этим пользоваться

Короткая инструкция по новой версии форка. План работ и разбор исходного кода —
в [plan-usage-improvements-ru.md](plan-usage-improvements-ru.md), справочник по всем
настройкам — в [CONFIG.md](CONFIG.md).

---

## 1. Что сделано по пунктам

### 1.1. Сворачивание в трей
- Крестик окна **сворачивает в трей**, а не завершает программу (`application.closeButtonAction`:
  `minimize` по умолчанию, можно поставить `exit`).
- «Свернуть» тоже уводит окно в трей (`application.minimizeToTray`).
- У значка в трее теперь есть **меню** (правый клик) и подсказка со статусом;
  левый клик — показать/спрятать окно.
- Значок восстанавливается после перезапуска `explorer.exe` (обрабатывается
  `TaskbarCreated`).
- Старт сразу в трее: `--tray` или `application.startMinimizedToTray: true`.

### 1.2. Переинициализация без перезапуска
- Вручную: **меню трея → «Reinitialize now»**, кнопка «Reinitialize» в окне,
  горячая клавиша **Ctrl+Alt+R**, командная строка `REAL.exe --reinit`
  (вторая копия не запускается, а передаёт команду уже работающей).
- Автоматически (настраивается в `audio.reinit`):
  - смена устройства по умолчанию;
  - переход устройства в активное/неактивное состояние;
  - появление/удаление устройства (по умолчанию выключено);
  - выход из сна / Modern Standby;
  - разблокировка сеанса;
  - плюс проверка каждые 30 секунд: жив ли поток и то ли это устройство —
    на случай, если уведомление не пришло.
- Всё это происходит **в том же процессе**, без перезапуска программы.
- Порядок команд: у второй копии всегда приоритет у уже запущенного экземпляра
  (одна копия через мьютекс `Local\REAL.SingleInstance`, отключается
  `application.singleInstance: false`).

### 1.3. Обновления больше не навязываются
- По умолчанию `updates.mode = "off"` — **ни одного сетевого запроса** за всё
  время работы (проверка выключена, curl/WinHTTP не вызывается).
- `updates.mode = "manual"` — проверка только по команде: меню трея →
  «Check for updates» или `REAL.exe --check-updates`. Результат показывается
  уведомлением/диалогом.
- Никогда не закрывает приложение, ничего не скачивает и не устанавливает само.
  Всё, что осталось от старого апдейтера — удаление файла `REAL.exe~DELETE`,
  оставшегося после прежних версий.
- Репозиторий для проверки настраивается: `updates.repository` (по умолчанию
  `Mutaracha/REAL`).

### 1.4. Внешний файл настроек
- Файл `real.settings.json` лежит **рядом с `REAL.exe`** и создаётся при первом
  запуске со всеми значениями по умолчанию.
- Формат JSON, **комментарии `//` и `/* */` разрешены**, кодировка UTF-8 (BOM тоже
  принимается). Неизвестные ключи не ломают запуск — пишется предупреждение в лог.
- Приоритет: командная строка → файл → встроенные значения.
- `--config <путь>` — другой файл, `--no-config` — игнорировать файл.
- Перечитать настройки без перезапуска: меню трея → «Settings file…» открывает файл,
  а пункт `ReloadSettings` (и кнопка «Settings file…» в окне) применяет изменения.
- Полный список опций — [CONFIG.md](CONFIG.md), пример со всеми ключами —
  [real.settings.example.json](real.settings.example.json).

### 1.5. Windows 11
Разбор механизма и рисков — в плане, §3.5. В коде:
- ошибки инициализации выводятся как **HRESULT с расшифровкой**
  (например, `AUDCLNT_E_UNSUPPORTED_FORMAT (0x88890008)`), а не как бессмысленный
  `GetLastError()`;
- обрабатывается `AUDCLNT_E_ENGINE_PERIODICITY_LOCKED`: если период уже зафиксирован
  другим приложением, REAL принимает текущий период (настраивается
  `audio.allowPeriodSnap`);
- отсутствие `IAudioClient3` (Bluetooth, часть виртуальных драйверов) даёт понятное
  сообщение вместо непонятной ошибки;
- `performance.disablePowerThrottling` снимает в Windows 11 троттлинг скорости
  исполнения, из-за которого раньше «залипало» ядро CPU;
- манифест приложения объявляет поддержку Windows 10/11 (иначе система включает
  режим совместимости), включены DPI-awareness и long paths;
- сборка — Release со статическим рантаймом (`/MT`), без зависимости от
  Visual C++ Redistributable.

---

## 2. Как проверить за 5 минут

1. Скачать готовый `REAL.exe`: вкладка **Actions** → последний зелёный запуск
   *build* → блок **Artifacts** → `REAL-x64`. Положить в отдельную папку
   (портативно, ничего не устанавливается).
2. Запустить `REAL.exe`. В трее должен появиться значок, окно — со статусом вида
   `2.67 ms - Speakers (Realtek(R) Audio)`.
3. Проверить главное:
   - **крестик** окна → программа продолжает работать в трее (значок на месте,
     звук не «прыгает»);
   - **левый клик** по значку — окно показывается/прячется;
   - **правый клик** → меню: статус, «Latency reduction enabled», «Reinitialize now»,
     «Settings file…», «Open log», «Check for updates», «Start with Windows», «Exit»;
   - **Ctrl+Alt+R** — переинициализация; в окне и в логе появляется новая запись;
   - в папке с exe появился `real.settings.json` и `REAL.log`.
4. Сценарии, которые стоит проверить отдельно:
   - подключить/отключить наушники или USB-звук → в логе «Audio device change
     detected…», задержка применяется заново (автоматически);
   - переключить устройство по умолчанию в «Параметры → Звук» → то же самое;
   - «Спящий режим» → пробуждение, и блокировка → разблокировка (Win+L);
   - закрыть `explorer.exe` и запустить снова (значок должен вернуться).
5. Если что-то не работает — пришлите `REAL.log` (он рядом с exe). Для подробного
   лога поставьте в настройках `logging.level: "debug"` и перезапустите программу
   (или `--log-level debug`).

---

## 3. Полезные настройки (файл `real.settings.json`)

```jsonc
{
  "application": {
    "minimizeToTray": true,           // «свернуть» → в трей
    "closeButtonAction": "minimize",  // крестик: minimize (в трей) | exit (выход)
    "startMinimizedToTray": false,    // старт сразу в трее
    "startWithWindows": false,        // автозапуск (HKCU\...\Run)
    "singleInstance": true
  },
  "tray": {
    "enabled": true,
    "showStatusInTooltip": true,
    "notifications": { "onError": true, "onDeviceChange": true, "onStateChange": false }
  },
  "audio": {
    "enabledOnStartup": true,
    "dataFlow": "render",             // render | capture | both
    "role": "console",                // console | multimedia | communications
    "periodSelection": "min",         // min | fundamental | fixed
    "allowPeriodSnap": true,
    "reinit": {
      "defaultDeviceChanged": true,
      "deviceStateChanged": true,
      "deviceAdded": false,
      "deviceRemoved": false,
      "resumeFromSleep": true,
      "sessionUnlock": true,
      "debounceMs": 1000
    }
  },
  "performance": {
    "processPriority": "normal",      // normal | belowNormal | idle
    "disablePowerThrottling": true    // Windows 11: не «занимать» ядро CPU
  },
  "updates": {
    "mode": "off",                    // off (по умолчанию) | manual
    "checkOnStartup": false
  },
  "hotkeys": {
    "enabled": true,
    "toggleEnabled": "Ctrl+Alt+L",
    "reinitialize": "Ctrl+Alt+R"
  },
  "logging": {
    "level": "info",                  // trace | debug | info | warn | error | off
    "toFile": true,
    "filePath": "REAL.log",
    "maxFileSizeMb": 1,
    "maxFiles": 3
  }
}
```

---

## 4. Командная строка

| Команда | Что делает |
|---|---|
| `REAL.exe` | запуск с окном (значок в трее тоже создаётся) |
| `--tray` / `--no-tray` | стартовать скрытым в трее / с окном |
| `--reinit` | переинициализировать аудио (передать команду работающей копии) |
| `--enable` / `--disable` | включить/выключить снижение задержки |
| `--check-updates` | проверить обновления (при `updates.mode: "manual"`) |
| `--exit` | закрыть работающую копию |
| `--config <путь>` / `--no-config` | другой файл настроек / без него |
| `--log-level <уровень>` | trace, debug, info, warn, error, off |
| `--console` | дополнительно окно консоли с журналом |
| `--multi-instance` | не переиспользовать запущенную копию |
| `--help`, `--version` | справка / версия |

---

## 5. Сборка

### 5.1. GitHub Actions (ничего устанавливать не нужно)
Пуш в ветку запускает `.github/workflows/build.yml`: сборка Release на
windows-2022, дымовой тест (запуск exe, вторая копия с `--exit`, проверка
`REAL.log`) и публикация `REAL.exe` в Artifacts. Пуш тега `v*` дополнительно
создаёт релиз и прикладывает exe.

### 5.2. Локально, MSVC без CMake
```bat
cd real-app
build.bat
```
`build.bat` сам находит установленный Visual Studio / Build Tools через `vswhere`
(подходит и «Build Tools 18» = VS 2026, toolset v145), компилирует ресурсы и
исходники и кладёт результат в `real-app\build\REAL.exe`. Нужен компонент
**C++ build tools** и Windows 11 SDK. Rust/Git/NASM для этой сборки не нужны
(они понадобятся, если пойдём в сторону Rust-версии из плана, §4.5).

### 5.3. Локально, CMake
```bat
cd real-app
run-cmake.bat
```
Важно: генератор `Visual Studio 18 2026` требует **CMake 4.2+** (VS 2026
отвязывает версию MSVC от версии CMake). Если CMake старше — `run-cmake.bat`
переключится на «Visual Studio 17 2022» или используйте `build.bat`.

---

## 6. Ограничения, о которых стоит знать

- **Период нельзя уменьшить, если его уже зафиксировало другое приложение**:
  ядро отдаёт `AUDCLNT_E_ENGINE_PERIODICITY_LOCKED`. REAL в этом случае принимает
  текущий период (это видно в статусе: `(period locked by another app)`).
- **Bluetooth** — 10 мс по определению профиля; **HDMI/DisplayPort** — зависит от
  ресивера; **виртуальные драйверы** и часть вендорских (Realtek/ACX) малый период
  не поддерживают. Для встроенных кодеков обычно помогает переход на «High
  Definition Audio Device» (см. README).
- Действие низкой задержки **глобальное**: пока REAL работает, все приложения
  работают с малым буфером, а это повышает риск треска при высокой нагрузке на CPU
  (частично лечится `performance.processPriority` + `disablePowerThrottling`).
- Пока поток-«удерживающий» активен, аудиоподсистема резервирует ресурсы CPU —
  это поведение Windows, а не утечка в REAL.
- Интерфейс пока английский; язык интерфейса в планах (см. план, §3.4).
