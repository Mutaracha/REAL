# `real.settings.json` — справочник по настройкам

Файл лежит рядом с `REAL.exe` (создаётся при первом запуске). Формат — JSON,
поэтому лишняя запятая в конце списка не допускается, зато комментарии `//` и
`/* */` **разрешены** (перед разбором они вырезаются, строки в кавычках не
затрагиваются). Кодировка — UTF-8, BOM допускается.

Приоритет значений:

```
командная строка  >  <каталог exe>\real.settings.json  >  встроенные значения по умолчанию
```

Если файл повреждён, приложение пишет об этом в журнал и работает на значениях
по умолчанию, ничего не перезаписывая. Неизвестные ключи игнорируются с
предупреждением в журнал — опечатка не ломает запуск.

Готовый пример со всеми ключами: [real.settings.example.json](real.settings.example.json).

---

## `application`

| Ключ | Тип | По умолчанию | Описание |
|---|---|---|---|
| `startMinimizedToTray` | bool | `false` | стартовать свёрнутым в трей (аналог `--tray`) |
| `minimizeToTray` | bool | `true` | «Свернуть» прячет окно в трей, а не в панель задач |
| `closeButtonAction` | `"minimize"` \| `"exit"` | `"minimize"` | что делает крестик окна |
| `showConsole` | bool | `false` | дополнительно открывать окно консоли с журналом (аналог `--console`) |
| `singleInstance` | bool | `true` | не запускать вторую копию, а передавать команду уже работающей |
| `startWithWindows` | bool | `false` | автозапуск (запись `REAL` в `HKCU\...\CurrentVersion\Run`) |

## `tray`

| Ключ | Тип | По умолчанию | Описание |
|---|---|---|---|
| `enabled` | bool | `true` | показывать значок в трее |
| `showStatusInTooltip` | bool | `true` | подсказка вида `REAL - 2.67 ms - Speakers` |
| `notifications.onError` | bool | `true` | всплывающее уведомление об ошибках |
| `notifications.onDeviceChange` | bool | `true` | уведомление о смене устройства |
| `notifications.onStateChange` | bool | `false` | уведомление о включении/выключении |
| `menu.showStatus` | bool | `true` | первая строка меню со статусом |
| `menu.toggleEnabled` | bool | `true` | пункт «Latency reduction enabled» |
| `menu.reinitialize` | bool | `true` | пункт «Reinitialize now» |
| `menu.openSettings` | bool | `true` | открыть файл настроек |
| `menu.openLog` | bool | `true` | открыть журнал |
| `menu.checkForUpdates` | bool | `true` | пункт проверки обновлений (виден, только если `updates.mode != "off"`) |
| `menu.startWithWindows` | bool | `true` | переключатель автозапуска |
| `menu.about` | bool | `true` | «About REAL» |
| `menu.exit` | bool | `true` | «Exit» |

## `audio`

| Ключ | Тип | По умолчанию | Описание |
|---|---|---|---|
| `enabledOnStartup` | bool | `true` | включать снижение задержки сразу при запуске |
| `dataFlow` | `"render"` \| `"capture"` \| `"both"` | `"render"` | какие устройства обрабатывать (воспроизведение/запись/оба) |
| `role` | `"console"` \| `"multimedia"` \| `"communications"` | `"console"` | роль устройства по умолчанию |
| `periodSelection` | `"min"` \| `"fundamental"` \| `"fixed"` | `"min"` | какой период запрашивать |
| `requestedPeriodFrames` | int | `0` | период для `"fixed"` (кадров); приводится к кратному `fundamental` и обрезается по `[min, max]` |
| `allowPeriodSnap` | bool | `true` | если период уже зафиксирован другим приложением — принять текущий, а не падать с ошибкой |
| `releaseOnExit` | bool | `true` | освобождать поток при выходе (аудиодвижок возвращается к 10 мс сам) |
| `reinit.defaultDeviceChanged` | bool | `true` | переинициализация при смене устройства по умолчанию |
| `reinit.deviceStateChanged` | bool | `true` | при переходе устройства в активное/неактивное состояние |
| `reinit.deviceAdded` | bool | `false` | при появлении нового устройства |
| `reinit.deviceRemoved` | bool | `false` | при удалении устройства |
| `reinit.resumeFromSleep` | bool | `true` | после выхода из сна/Modern Standby |
| `reinit.sessionUnlock` | bool | `true` | после разблокировки сеанса |
| `reinit.debounceMs` | int | `1000` | антидребезг: Windows присылает пачку событий подряд |

Дополнительно приложение раз в 30 секунд проверяет, что поток жив и что
устройство по умолчанию — то же самое; при расхождении переинициализация
выполняется автоматически.

## `performance`

| Ключ | Тип | По умолчанию | Описание |
|---|---|---|---|
| `processPriority` | `"normal"` \| `"belowNormal"` \| `"idle"` | `"normal"` | приоритет процесса |
| `disablePowerThrottling` | bool | `true` | Windows 11: снять троттлинг скорости исполнения (`PROCESS_POWER_THROTTLING_EXECUTION_SPEED`), чтобы аудиопоток не «занимал» ядро CPU. В Windows 10 параметр игнорируется |

## `updates`

| Ключ | Тип | По умолчанию | Описание |
|---|---|---|---|
| `mode` | `"off"` \| `"manual"` | `"off"` | `off` — ни одного сетевого запроса; `manual` — проверка только по команде |
| `repository` | string | `"Mutaracha/REAL"` | репозиторий GitHub для проверки релизов |
| `timeoutSeconds` | int | `15` | таймаут запроса |
| `checkOnStartup` | bool | `false` | проверять при запуске (только при `mode = "manual"`); результат показывается уведомлением, приложение **не** завершается и **не** обновляется само |

## `hotkeys`

| Ключ | Тип | По умолчанию | Описание |
|---|---|---|---|
| `enabled` | bool | `true` | регистрировать глобальные горячие клавиши |
| `toggleEnabled` | string | `"Ctrl+Alt+L"` | включить/выключить снижение задержки |
| `reinitialize` | string | `"Ctrl+Alt+R"` | переинициализировать потоки |

Поддерживаются модификаторы `Ctrl`, `Alt`, `Shift`, `Win` и клавиши `A`–`Z`,
`0`–`9`, `F1`–`F24`. Если комбинация занята другой программой, в журнал
попадёт предупреждение, приложение продолжит работать.

## `logging`

| Ключ | Тип | По умолчанию | Описание |
|---|---|---|---|
| `level` | string | `"info"` | `trace`, `debug`, `info`, `warn`, `error`, `off` |
| `toConsole` | bool | `false` | дублировать журнал в консоль (нужен `application.showConsole`) |
| `toFile` | bool | `true` | писать файл журнала |
| `filePath` | string | `"REAL.log"` | путь относительно каталога exe или абсолютный |
| `maxFileSizeMb` | int | `1` | размер файла до ротации |
| `maxFiles` | int | `3` | сколько файлов хранить |

## `configVersion`

Служебное поле (текущее значение — `1`). Зарезервировано для будущих миграций
настроек.
