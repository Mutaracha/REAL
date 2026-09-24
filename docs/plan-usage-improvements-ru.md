# REAL v0.2.0 → доработки по использованию: оценка и план

Документ подготовлен по состоянию ветки `master` (коммит `077e6fe`, релиз v0.2.0).
Форк: `Mutaracha/REAL`, апстрим: `miniant-git/REAL` (последний коммит апстрима — апрель 2019, последний релиз — `v0.2.0`).

---

## 0. Резюме (TL;DR)

| № | Требование | Как сейчас | Объём правок | Оценка |
|---|---|---|---|---|
| 1 | Сворачивание в трей | Частично: флаг `--tray` прячет консоль при старте, в трее только ЛКМ = «показать консоль и выйти» | Средний: переработка GUI-слоя, контекстное меню, корректный цикл сообщений | 2–3 дня |
| 2 | Переинициализация без перезапуска | Невозможна: цикл сообщений запускается **только** в `--tray`, ввод с консоли блокирующий, аудиопоток создаётся один раз при старте | Крупный: `IMMNotificationClient`, health-check, ручной сброс, отвязка от блокирующего `_getch` | 2–4 дня |
| 3 | Отключить принудительную проверку обновлений | Проверка выполняется всегда; при наличии новой версии приложение **завершается (return 2) независимо от ответа** пользователя | Малый: конфиг + переработка сценария запуска, апдейтер опционален на этапе сборки | 0,5–1 день |
| 4 | Внешний файл настроек | Нет вовсе, всё зашито в код | Средний: загрузчик настроек, приоритеты, горячая перезагрузка, документация | 1–1,5 дня |
| 5 | Совместимость с Windows 11 | Работает (подтверждено пользователями, см. §3.5), но диагностика ошибок сломана, а часть Win11-специфики (Modern Standby, «залипание» ядра, драйверы) не учтена | Малый–средний: корректные HRESULT-сообщения, обработка `AUDCLNT_E_ENGINE_*_LOCKED`, сон/разблокировка, митигации Win11 | 1,5–2 дня |

**Итого:** ~8–12 человеко-дней на всё, из них «быстрые победы» (пункты 3 и 4 + гигиена сборки) — 2–3 дня.

**Ответы на вопросы про сборку (кратко, подробно в §4):**

* **Переписать на C#?** Можно, и это реально проще для пунктов 1, 3, 4. `NAudio` уже содержит `IAudioClient3` (`AudioClient.GetSharedModeEnginePeriod`, `InitializeSharedAudioStream`, `WasapiPlayerBuilder.WithLowLatency`) — то есть аудиоядро переписывается в ~150 строк. Нужен .NET SDK 8/9, публикация `win-x64`. Минусы: exe self-contained ~60–80 МБ (WinForms не поддерживает trim/AOT), либо ~1 МБ + .NET Desktop Runtime у пользователя.
* **PortableBuildTools?** Можно, но не нужно: он даёт MSVC + Windows SDK, но **не содержит CMake**, репозиторий архивирован (март 2025, v2.10), качает ~1–2 ГБ. MSVC BuildTools 18 у вас уже есть — этого достаточно.
* **MSVC BuildTools 18 + Rust + Git + NASM — хватит?** Да. BuildTools 18 = Visual Studio 2026 (toolset v145). Для C++ пути нужен CMake ≥ 4.2 (генератор `"Visual Studio 18 2026"`) либо сборка вообще без CMake (см. §4.2 — после отказа от libcurl внешних зависимостей не остаётся). Rust/NASM для REAL не нужны (Rust — только если пойти по пути `spddl/LowAudioLatency`, см. §4.5).
* **Как компилировать, если среды нет вообще?** GitHub Actions на вашем форке (Actions доступны, запусков пока 0): workflow на `windows-latest` собирает exe и отдаёт артефакт/релиз. Это также единственный способ, которым я могу проверять сборку — в этой песочнице (Linux) нет Windows-кросс-компилятора и нет доступа к apt.

---

## 1. Карта текущего кода

| Файл | Роль |
|---|---|
| `src/main.cpp` | весь сценарий запуска: лог, консоль/трей, старт аудио, апдейтер, цикл сообщений |
| `src/Windows/MinimumLatencyAudioClient.*` | WASAPI: enumerator → default endpoint (`eRender`, `eConsole`) → `IAudioClient3` → `GetSharedModeEnginePeriod` → `InitializeSharedAudioStream(min)` → `Start()` |
| `src/Windows/MessagingWindow.*` | message-only окно (`HWND_MESSAGE`) + карта обработчиков сообщений |
| `src/Windows/GlobalWindowProcedure.*` | единый `WndProc`, `RegisterWindowClass`, генератор ID событий от `WM_USER` |
| `src/Windows/TrayIcon.*` | `NOTIFYICONDATA` v4, только `WM_LBUTTONUP` |
| `src/Windows/Console.*` | `AllocConsole`/`FreeConsole`, перенаправление `stdout`/`stdin` |
| `src/Windows/Filesystem.*` | `ShellExecuteEx` команд `cmd`/`powershell` (обновление, UAC), пути, проверка прав |
| `src/AutoUpdater.*` | GitHub API через libcurl, скачивание `update`, распаковка `Expand-Archive`, `~DELETE` |
| `src/Version.*` | разбор `vX.Y.Z` (regex) |
| `src/OStreamSink.*` | sink spdlog в `ostringstream` (буфер для консоли) |
| `real-app/CMakeLists.txt`, `run-cmake.bat`, `deps/*` | сборка VS2017 + libcurl из исходников через `ExternalProject` |

Поток выполнения `wWinMain` (сейчас):

```
OStreamSink → Console → (--tray ? MessagingWindow+TrayIcon : окно консоли)
  → MinimumLatencyAudioClient::Start()           // один раз, без возможности повтора
  → AutoUpdater::CleanupPreviousSetup()
  → AutoUpdater::IsAppSuperseded()               // сетевой запрос всегда
  → AutoUpdater::GetUpdateInfo()                 // сетевой запрос всегда
      → если версия новее: показать консоль, спросить y/N, применить, ... return 2
  → if (--tray) GetMessage-цикл; else _getch()   // ожидание клавиши
```

---

## 2. Дефекты, влияющие на все 5 пунктов

1. **Цикл сообщений только в режиме `--tray`.** В обычном режиме вместо цикла — блокирующий `_getch()` ([main.cpp](../real-app/src/main.cpp)). Без цикла сообщений невозможны ни таймеры, ни обработка смены устройства, ни меню трея.
2. **`--tray` сравнивается со всей командной строкой целиком**: `if (commandLine == COMMAND_LINE_OPTION_TRAY)`. Любой лишний аргумент/кавычка → режим трея не включается. Нужен нормальный парсер (`CommandLineToArgvW`).
3. **`Console::Close()` завершает процесс.** Внутри — `SendMessage(::GetConsoleWindow(), WM_CLOSE, 0, 0)`, то есть conhost рассылает `CTRL_CLOSE_EVENT`, и процесс умирает. Пока приложение выходит сразу после этого, баг незаметен; при «показать/скрыть окно» он станет критичным — прятать надо через `ShowWindow(SW_HIDE)` + `SetConsoleCtrlHandler`.
4. **Иконка в трее без меню и подсказки** (`uFlags = NIF_ICON | NIF_MESSAGE`, ни `NIF_TIP`, ни `NIF_INFO`), только обработчик ЛКМ; нет перерегистрации иконки после перезапуска `explorer.exe` (сообщение `TaskbarCreated`).
5. **Обновление принудительное не только «по факту проверки», но и по логике**: найдя версию новее, приложение показывает `y/N`, но **в любом случае** завершается (`DisplayExitMessage(...); return 2;`). Пользователь не может отказаться и продолжить работу. Плюс URL-ы жёстко указывают на `miniant-git/REAL` — для форка это бессмысленно.
6. **Апдейтер не защищён от исключений**: `json::parse(...)` без `try/catch`, `response["name"]` на неожиданном ответе (rate limit GitHub, прокси, captive portal) → `json::type_error`/`parse_error` → `std::terminate`. Проверка обновлений в принципе опасна, что усиливает пункт 3.
7. **`WindowsError` всегда печатает `GetLastError()`**, даже когда сбой пришёл как `HRESULT` из COM: пользователь видит `Last error: 0` вместо, например, `AUDCLNT_E_UNSUPPORTED_FORMAT (0x88890008)`. На Win11 это главный барьер диагностики.
8. **`MinimumLatencyAudioClient` — «одноразовый» объект**: нет обработки `AUDCLNT_E_ENGINE_PERIODICITY_LOCKED` / `AUDCLNT_E_ENGINE_FORMAT_LOCKED` (движок уже залочен другим клиентом — это нормальная ситуация), нет проверки «а не малый ли период уже сейчас», нет ролей (`eConsole` жёстко), нет выбора устройства, нет `Stop()`/`CoUninitialize()`, указатели хранятся как `void*`.
9. **Сборка**: `set(CMAKE_BUILD_TYPE Debug)` + `/JMC` — сборка Debug (debug-CRT, нераспространяемая), `run-cmake.bat` жёстко задаёт генератор `"Visual Studio 15 2017 Win64"`, `deps/curl/curl-config.cmake` жёстко прописывает путь `libcurl-vc15-x64-debug-static-...`, а `build.bat` пересобирает curl из git через `nmake ... VC=15`. С MSVC 18 этот путь сломается.
10. **Нет CI, нет манифеста приложения** (нет `supportedOS` для Win10/11, нет `dpiAware`, нет `asInvoker`), версия дублируется в `main.cpp` и в README.

---

## 3. Разбор по пунктам

### 3.1 Пункт 1 — сворачивание в трей

**Как сейчас.** `--tray` создаёт message-only окно и иконку, консоль не показывается. ЛКМ по иконке: скрыть иконку → открыть консоль → «Press any key to disable and exit» → выход. Всё.

**Что нужно.**

1. Полноценный интерактивный слой. Варианты:
   * **A (рекомендуемый):** одно основное окно класса `RealWindow` (`RegisterClassEx` + `WndProc`) как «лицо» приложения: показывает статус (устройство, текущий период, драйвер), в него же рендерится лог. Сворачивание перехватывается `WM_SYSCOMMAND`/`SC_MINIMIZE`, крестик — `WM_CLOSE` → скрыть (`ShowWindow(SW_HIDE)`), иконка в трее остаётся. Это чистый Win32-путь, без «костылей» с консолью, и он же закрывает пункт 4 (меню настроек).
   * **B (минимальный дифф):** оставить консоль, но сабклассить её окно (`GetConsoleWindow` + `SetWindowLongPtr(GWLP_WNDPROC)`) и перехватывать `WM_SYSCOMMAND`/`SC_MINIMIZE`, `WM_CLOSE`, плюс `SetConsoleCtrlHandler` для «крестика». Дёшево, но консоль остаётся уродливым UI, а сабклассинг чужого окна — хрупкий приём.
   * **C:** окна нет вообще, только трей (как сейчас `--tray`), но с меню и корректным «показать лог» (можно открыть файл лога в блокноте). Самый простой вариант «свернуть в трей» по сути, но тогда непонятно, что именно «сворачивается».
2. **Меню трея** (`WM_RBUTTONUP` → `CreatePopupMenu` + `TrackPopupMenu` с `TPM_RIGHTBUTTON`, обязательно `SetForegroundWindow` перед этим и `PostMessage(hwnd, WM_NULL, ...)` после — классические грабли):
   ```
   Статус: 2.67 мс · Speakers (Realtek)      [disabled item]
   ─────────────
   Включено                          (галка)
   Переинициализировать сейчас       ← ручной сброс (пункт 2)
   Целевое устройство ▸ (default / выбрать)  ← если решим поддержать выбор
   ─────────────
   Открыть настройки (real.settings.json)
   Открыть журнал
   Проверить обновления              ← только если updates.mode != off
   ─────────────
   Выход
   ```
3. **Поведение окна** (по настройкам, §3.4): `minimizeToTray`, `closeButtonAction = minimize|exit`, `startMinimizedToTray` (замена `--tray`), `showConsoleWindow`.
4. **Иконка**: `NIF_ICON|NIF_MESSAGE|NIF_TIP|NIF_SHOWTIP`, тултип со статусом, `Shell_NotifyIcon(NIM_SETVERSION, NOTIFYICON_VERSION_4)`, обработка `NIN_BALLOONUSERCLICK`/`NIN_SELECT`, пузырьки `NIF_INFO` на ошибки/смену устройства. Обязательна перерегистрация по `RegisterWindowMessage(TEXT("TaskbarCreated"))` (перезапуск explorer; в Win11 случается чаще, чем хотелось бы).
5. **Один экземпляр** (`CreateMutex` + именованный объект) — сейчас можно запустить несколько копий, каждая держит свой поток на том же endpoint.
6. **Особенность Win11**: иконки приложений по умолчанию попадают в «скрытую область» трея; приложение не может это переопределить — стоит один раз показать balloon «перетащите иконку в видимую область». Это надо написать в README.

**Критерии приёмки.** Кнопка «свернуть» → окно скрывается, иконка в трее, процесс жив; ЛКМ по иконке — переключение окна; ПКМ — меню; «Выход» завершает процесс и снимает иконку; перезапуск `explorer.exe` не теряет иконку; повторный запуск exe не создаёт вторую копию; в режиме «только трей» окно не мелькает при старте.

---

### 3.2 Пункт 2 — переинициализация без перезапуска

**Почему сейчас нельзя.** `MinimumLatencyAudioClient` создаётся один раз; цикла сообщений в обычном режиме нет; `_getch()` блокирует поток; в трее доступен только «выход». После смены устройства по умолчанию (подключили USB-наушники, переключили вывод в микшере, устройство усыпили) «магический» поток остаётся на **старом** endpoint, и эффекта на новом устройстве нет — приложение при этом считает, что всё в порядке.

**Что нужно (автоматический режим).**

1. **`IMMNotificationClient`**: получить `IMMDeviceEnumerator`, вызвать `RegisterEndpointNotificationCallback`. Обработчики:
   * `OnDefaultDeviceChanged(flow, role, deviceId)` — основной триггер (учтите: приходит и с `role = eConsole/eMultimedia/eCommunications`; для нас значимы `flow == eRender` и наша роль);
   * `OnDeviceStateChanged`, `OnDeviceAdded`, `OnDeviceRemoved` — «обнаружено новое устройство» (тут же решается вопрос «применять ли к новому устройству» — только если оно стало целевым/попало под фильтр имён);
   * `OnPropertyValueChanged` — не нужен, но пусть будет пустой заглушкой.
2. **Потокобезопасность COM — ключевая деталь дизайна.** Колбэки приходят на системном потоке, `IAudioClient3` создан в апартаменте главного потока. Поэтому: все операции с аудиооъектами — **только в главном потоке** (в обработчике окна), а колбэк лишь сигнализирует: `PostMessage(hMessagingWindow, WM_APP_REINIT, ...)`. Это же снимает вопрос маршалинга и «классических» `RPC_E_*` ошибок.
3. **Health-check по таймеру** (например, раз в 30 с, опция `healthCheckSeconds`): вызвать `GetCurrentSharedModeEnginePeriod` и сравнить с ожидаемым; ошибки `AUDCLNT_E_DEVICE_INVALIDATED`, `AUDCLNT_E_SERVICE_NOT_RUNNING`, `AUDCLNT_E_RESOURCES_INVALIDATED` → переинициализация. Это ловит случаи, которые колбэки не покрывают: перезапуск `Audiosrv`, выход из спящего режима, «драйвер перезагрузился».
4. **События сна/сессии**: `WM_POWERBROADCAST` (`PBT_APMRESUMEAUTOMATIC`) и `WTSRegisterSessionNotification` (`WM_WTSSESSION_CHANGE`, `WTS_SESSION_UNLOCK`) → переинициализация. Особенно актуально для Win11 с Modern Standby: после resume аудиоустройства часто пересоздаются.
5. **Антидребезг**: `reinitDebounceMs` (по умолчанию 1000 мс) — при переключении устройства Windows может сгенерировать 3–5 событий подряд.
6. **Корректный цикл переинициализации**:
   ```
   Stop()/Release старый клиент → CoTaskMemFree(format)
   → GetDefaultAudioEndpoint(flow, role)
   → Activate(IAudioClient3)
   → GetSharedModeEnginePeriod → выбрать период
   → InitializeSharedAudioStream
       ├─ AUDCLNT_E_ENGINE_PERIODICITY_LOCKED → взять текущий период
       │   (GetCurrentSharedModeEnginePeriod) и принять его как рабочий
       ├─ AUDCLNT_E_ENGINE_FORMAT_LOCKED    → работать в формате движка
       └─ прочие ошибки → понятное сообщение + статус в трее/логе
   → Start() → записать в статус фактический период (мс)
   ```
7. **Ручной режим** (на случай, когда автоматика не сработала):
   * пункт меню трея `Переинициализировать сейчас`;
   * глобальная горячая клавиша (`RegisterHotKey`, по умолчанию `Ctrl+Alt+R`);
   * CLI: `REAL.exe --reinit` — второй экземпляр не запускает второй аудиопоток, а находит уже работающий (именованный мутекс + `RegisterWindowMessage` / `FindWindow`) и просит его переинициализироваться. Удобно для скриптов и ярлыков. Аналогично `--enable`, `--disable`, `--exit`.
8. **Дополнительно**: опция «применять ко **всем** активным render-endpoint» (`deviceSelection = allActive`) закрывает старый запрос из issues апстрима (#16 — Voicemeeter, виртуальные устройства) и делает смену устройства безболезненной «по определению».

**Проверяемость.** После переинициализации в лог/тултип пишется фактический период движка (например `Сейчас: 128 кадров = 2.67 мс @48 кГц (устройство: USB DAC)`). Это же значение видно в `mmsys.cpl` → «Дополнительно» → размер буфера — расхождение сразу заметно.

**Критерии приёмки.** Переключение устройства по умолчанию в микшере → в течение ~1–2 с в логе новый endpoint и малый период; подключение/отключение USB-наушников — то же; перезапуск службы `Audiosrv` — восстановление; сон/пробуждение — восстановление; `--reinit` из второго процесса — работает; всё то же самое при ПКМ → «Переинициализировать».

---

### 3.3 Пункт 3 — отключение принудительной проверки обновлений

**Как сейчас.** Проверка на каждом старте, всегда, без опций; при найденной новой версии приложение в любом случае завершается с кодом 2. URL-ы жёстко на апстрим; в форке это ещё и бессмысленно (последний релиз апстрима — 2019 год, а тег `superseded` вообще не существует).

**Целевое поведение.**

```jsonc
"updates": {
  "mode": "off",                          // off | notify | check   (по умолчанию off)
  "repository": "Mutaracha/REAL",         // если форк будет публиковать свои релизы
  "checkOnStartup": false,
  "manualOnly": true,                     // «Проверить обновления» только из меню трея
  "showSupersededNotice": false,
  "assetName": "update",
  "timeoutSeconds": 15
}
```

* `off` — **ни одного сетевого запроса**; пункт меню скрыт; `AutoUpdater` не создаётся, `CurlHandle::InitialiseCurl()` не вызывается.
* `notify` — проверка (по таймеру/вручную), но **никогда** не завершает приложение: максимум balloon «доступна версия X» и ссылка на страницу релиза.
* `check` — прежнее поведение (скачивание и самообновление), но с подтверждением и **без** принудительного выхода.

**Дополнительно к пункту:**

1. **Убрать зависимость от libcurl** (она же — самая тяжёлая часть сборки). Варианты:
   * `updates.mode = off` в релизной сборке → апдейтер вообще не компилируется (`REAL_ENABLE_UPDATER=OFF` в CMake). Плюс: сборка без внешних библиотек (см. §4.2).
   * Если апдейтер нужен — заменить libcurl на **WinHTTP** (`WinHttpOpen`/`WinHttpSendRequest`, ~40 строк, часть Windows). Тогда `deps/curl` и `deps/curl/build.bat` удаляются, а сборка перестаёт требовать пересборки curl из git.
2. **Защита от исключений**: `json::parse` в `try/catch`, проверка наличия полей через `find()` вместо `operator[]`, лимиты на размер ответа, таймаут, корректная работа при rate-limit (403 + другой JSON → сейчас это падение).
3. **Обновление каталога `Program Files`** сейчас требует UAC (`ShellExecuteEx` с `runas`, см. `CanWriteTo`) — если апдейтер оставляем, лучше честно предупредить и не пытаться самообновляться оттуда.
4. **Обновить формат релизов форка** (если самообновление сохраняем): в апдейтере зашит ассет с именем `update` (см. `FindUpdateAssetUrl`) и логика переименования в `update.zip`. Проще указать в конфиге `assetName` и не переизобретать.

**Критерии приёмки.** При `mode = off` в Process Monitor/`netsh trace` нет обращений к `api.github.com`; приложение никогда не завершается из-за обновлений; при `mode = notify` с сетевой ошибкой/подменённым ответом — запись в лог, но работа продолжается.

---

### 3.4 Пункт 4 — внешний файл настроек и «что ещё можно вынести»

**Формат и расположение.** `real.settings.json` **в каталоге exe** (как просили), кодировка UTF-8 (принимать и с BOM — блокнот Windows его пишет), приоритеты:

```
CLI-аргументы  >  <каталог exe>\real.settings.json  >  %LOCALAPPDATA%\REAL\settings.json  >  значения по умолчанию
```

* Если файла нет — приложение пишет шаблон с комментариями-подсказками и продолжает с дефолтами.
* Неизвестные ключи — предупреждение в лог (не ошибка), «битый» JSON — сообщение и работа на дефолтах.
* Перечитать можно из меню трея («Перечитать настройки») и/или автоматически по `ReadDirectoryChangesW` (проще — по таймеру раз в 2 с сверять `mtime`).
* `--config <путь>` — для нескольких профилей; `--no-config` — «заводские» настройки.
* Бонус: `docs/real.settings.schema.json` (JSON Schema) → автодополнение и валидация в VS Code.
* Встроенный JSON в репозитории — `nlohmann/json` 3.1.2 (2018). Для комментариев в конфиге нужен ≥ 3.9 (`ignore_comments`), поэтому либо обновляем header до 3.11, либо делаем конфиг «чистым» JSON без комментариев (в примере ниже комментарии есть — это документ, а не рантайм-файл; шаблон, который пишет приложение, будет без них).

**Полная таблица опций** (это и есть ответ на «проверить, возможно ли вынести дополнительные опции» — вынести можно почти всё):

| Секция | Ключ | Тип | Дефолт | Смысл |
|---|---|---|---|---|
| application | `startMinimizedToTray` | bool | false | замена ключа `--tray` |
| | `minimizeToTray` | bool | true | сворачивать в трей, а не в панель задач |
| | `closeButtonAction` | enum | `minimize` | `minimize` \| `exit` |
| | `showConsoleWindow` | bool | true | false = работаем без окна консоли |
| | `singleInstance` | bool | true | один экземпляр |
| | `startWithWindows` | bool | false | `HKCU\...\Run` |
| tray | `enabled` | bool | true | иконка в трее |
| | `showStatusInTooltip` | bool | true | «2.67 мс · Speakers» в подсказке |
| | `notifications.onError` / `.onDeviceChange` / `.onStateChange` | bool | true/false/false | balloon-уведомления |
| | `menu.*` | bool | — | какие пункты показывать |
| audio | `enabledOnStartup` | bool | true | применять сразу |
| | `dataFlow` | enum | `render` | `render` \| `capture` \| `both` (микрофоны — как в LAL) |
| | `role` | enum | `console` | `console` \| `multimedia` \| `communications` |
| | `deviceSelection` | enum | `default` | `default` \| `allActive` \| `list` |
| | `deviceIds`, `deviceNameFilter` | array | [] | явный выбор устройств |
| | `periodSelection` | enum | `min` | `min` \| `fundamental` \| `fixed` |
| | `requestedPeriodFrames` | int | 0 | 0 = минимум, поддерживаемый драйвером |
| | `allowPeriodSnap` | bool | true | смириться с уже залоченным периодом движка |
| | `reinitOn.*` | bool | см. §3.2 | какие события вызывают переинициализацию |
| | `reinitDebounceMs` | int | 1000 | антидребезг |
| | `healthCheckSeconds` | int | 30 | 0 = выключить health-check |
| | `releaseStreamIfNoChangeNeeded` | bool | false | не держать поток, если период уже минимальный |
| performance | `processPriority` | enum | `normal` | `normal` \| `belowNormal` \| `idle` |
| | `disablePowerThrottling` | bool | true | Win11: `PROCESS_POWER_THROTTLING_EXECUTION_SPEED` (гасит «залипание» ядра, см. §3.5) |
| | `mmcssProAudioThread` | bool | false | если понадобится свой поток рендера |
| updates | `mode` | enum | `off` | см. §3.3 |
| | `repository` | string | fork | источник релизов |
| hotkeys | `toggleEnabled`, `reinitialize` | string | `Ctrl+Alt+L` / `Ctrl+Alt+R` | `RegisterHotKey` |
| logging | `level`, `toConsole`, `toFile`, `filePath`, `maxFileSizeMb`, `maxFiles` | — | `info`, true, false, `REAL.log`, 1, 3 | файловый лог критичен для Win11-диагностики |

Что **не** стоит выносить в конфиг: параметры, которые ломают сам механизм (например, `AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM`, попытки задать период меньше `minPeriodInFrames` и т.п.) — вместо этого дать понятное сообщение об ошибке. Также вне конфига — «тихие» эксперименты (affinity/CpuSet), их лучше держать в отдельной секции `experimental.*` с явным «на свой риск».

**Артефакт-пример:** `docs/real.settings.example.json` (создан вместе с этим планом).

---

### 3.5 Пункт 5 — совместимость с Windows 11

**Что уже работает.** `IAudioClient3` присутствует и в Win10 (1607+), и в Win11; Win11 наследует тот же аудиостек и тот же принцип: приложение, запросившее малый период, «переключает» движок для всех клиентов endpoint. Пользователи REAL на Win11 подтверждают работу (issues #21, #26: «Win 11 is not a problem», «буфер падает до 2.67 мс») — при условии, что стоит драйвер «High Definition Audio Device», а не вендорский.

**Что требует правок:**

1. **Диагностика (главное).** Сейчас любая ошибка — `ERROR: Could not enable low-latency mode` + `Last error: 0`. Нужно печатать HRESULT в hex + `FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM|IGNORE_INSERTS)` и трактовать:
   | Код | Что значит | Поведение |
   |---|---|---|
   | `E_NOINTERFACE` из `Activate(IAudioClient3)` | драйвер/endpoint не поддерживает v3 (часто Bluetooth, некоторые USB-DAC) | понятное сообщение «устройство не поддерживает малый буфер» |
   | `AUDCLNT_E_UNSUPPORTED_FORMAT` | формат не тот, что у движка (например, попытка задать свою частоту) | работать в `GetMixFormat()` |
   | `AUDCLNT_E_ENGINE_PERIODICITY_LOCKED` | другой клиент уже зафиксировал период | взять текущий период (`GetCurrentSharedModeEnginePeriod`) и принять его |
   | `AUDCLNT_E_ENGINE_FORMAT_LOCKED` | движок залочен по формату | не требовать `MATCH_FORMAT` |
   | `AUDCLNT_E_DEVICE_INVALIDATED`, `AUDCLNT_E_SERVICE_NOT_RUNNING`, `AUDCLNT_E_RESOURCES_INVALIDATED` | устройство/служба пересозданы | переинициализация (§3.2) |
   | `AUDCLNT_E_CPUUSAGE_EXCEEDED` | превышен бюджет CPU процесса | предупреждение, снижение частоты повторов |
2. **Сон/разблокировка и Modern Standby** — Win11 гораздо агрессивнее выгружает аудиоустройства; без обработки `WM_POWERBROADCAST`/`WM_WTSSESSION_CHANGE` эффект «отваливается» до перезапуска приложения (это и есть жалоба №2 пользователя).
3. **Побочка низкой латентности — «занятое» ядро** (issues #9, #22: у части пользователей один поток CPU выглядит занятым, падает результат бенчмарков, система хуже уходит в сон). Это поведение аудиостека, а не баг REAL, но его можно существенно ослабить:
   * `SetPriorityClass(..., IDLE_PRIORITY_CLASS)`;
   * `SetProcessInformation(..., ProcessPowerThrottling, {EXECUTION_SPEED})` — Win11 (build 22000+), в Win10 вызов просто вернёт ошибку, которую надо игнорировать;
   * **не держать поток вообще, если изменений не требуется** (`minPeriodInFrames >= defaultPeriodInFrames` → сообщить «драйвер и так отдаёт минимальный буфер» и остановить стрим).
   Приём взят из `spddl/LowAudioLatency` (MIT) — там он описан как «removes the real-time connection to the first CPU thread».
4. **Драйверная сторона** (не код, но без неё «исправление звука» на Win11 не работает):
   * встроенный «High Definition Audio Device» в Win11 присутствует (Device Manager → Update driver → Browse → Let me pick…), путь в README надо описать в терминах Win11, с предупреждением про скачок громкости;
   * вендорские драйверы (Realtek `RTKVHD64.sys`, новые ACX-драйверы `AcxHdAudio.sys`) обычно **не** отдают малый период — важно, чтобы приложение сообщало это явно, а не «Could not enable»;
   * Bluetooth-endpoint'ы всегда 10 мс, HDMI/DP — по возможностям AVR/дисплея: это ограничения, о которых пользователь должен прочитать в логе.
5. **Манифест приложения**: добавить `<compatibility><supportedOS Id="{8e0f7a12-...}"/></compatibility>` (Windows 10/11), `dpiAware` (для нового окна), `requestedExecutionLevel level="asInvoker"` (UAC не нужен; сейчас единственный повод к элевации — обновление в `Program Files`).
6. **Сборка под Win11**: нынешний exe — Debug-сборка VS2017; корректнее Release + статический CRT (`/MT`) — тогда не нужен ни распространяемый пакет VC++, ни debug-CRT. Плюс линковка более новым SDK (у вас — MSVC 18 / Windows 11 SDK).
7. **Проверка «а не мешает ли кто-то»**: если период уже минимальный или залочен другим приложением (DAW, игры с экспериментальным аудиорежимом, Teams/Discord), приложение должно сказать об этом в статусе, а не выдать «ошибку».
8. **Регрессии, которые надо проверить руками** (из issues): повышенный расход CPU, помеха уходу в сон, «звук стал тише» (это следствие смены драйвера), поведение при нескольких звуковых приложениях.

**Что не изменилось в Win11 и не требует правок:** сам механизм `GetSharedModeEnginePeriod`/`InitializeSharedAudioStream`, API `IMMDeviceEnumerator`, схема `NOTIFYICONDATA` (кроме дизайна трея), `ShellExecuteEx`, работа с консолью.

---

## 4. Сборка: варианты и рекомендации

### 4.1 Что уже есть на вашей машине

* **MSVC BuildTools 18 = Visual Studio 2026**, toolset v145. Нужны компоненты: «MSVC v145 toolset», «Windows 11 SDK», при желании «C++ CMake tools for Windows».
* **CMake**: для генератора `"Visual Studio 18 2026"` требуется **CMake ≥ 4.2** (генератор добавлен в CMake 4.2). Если CMake установлен отдельно и он старее — либо обновить, либо собирать через `-G Ninja` после `vcvars64.bat`.
  * CMake может уже лежать внутри BuildTools: `C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe` — проверьте `cmake --version`.
* **Git** — нужен (для `ExternalProject_Add` с curl; если curl убрать — только для клонирования).
* **Rust** и **NASM** — для этого проекта не требуются.

### 4.2 Путь «C++ с минимумом зависимостей» (рекомендуемый, если остаёмся на C++)

Ключевая идея: **убрать libcurl**. Тогда из внешних зависимостей остаются только header-only библиотеки, уже лежащие в `real-app/deps` (`spdlog`, `nlohmann/json`, `tl/expected`), и сборка сводится к одной команде компилятора:

```bat
:: real-app\build.bat  (Release, статический CRT, без DLL-зависимостей)
call "%ProgramFiles(x86)%\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist build mkdir build
rc /nologo /fo build\real-app.res res\real-app.rc
cl /nologo /O2 /MT /EHsc /std:c++17 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN ^
   /Fo:build\ /Fe:build\REAL.exe src\*.cpp src\Windows\*.cpp ^
   /link /SUBSYSTEM:WINDOWS build\real-app.res shell32.lib ole32.lib user32.lib
:: src\CurlWrapper\*.cpp добавляются только если REAL_ENABLE_UPDATER=ON
:: и апдейтер оставлен на libcurl; при переходе на WinHTTP — winhttp.lib вместо curl
```

Что для этого надо сделать в проекте: `REAL_ENABLE_UPDATER=OFF` (или замена curl на WinHTTP), правки `CMakeLists.txt` (Release, `/MT`, без `/JMC`), актуализация `run-cmake.bat` (или отказ от CMake в пользу `build.bat`). Итог: exe ~1–2 МБ, без внешних DLL, собирается где угодно, где есть BuildTools (2017…2026).

### 4.3 PortableBuildTools — оценка

* Что даёт: портативная установка MSVC-компилятора/линкера + заголовки и библиотеки Windows SDK в отдельную папку, без установки Visual Studio (можно положить на флешку/в облако).
* Ограничения: **CMake в него не входит** (нужен отдельно: `winget install Kitware.CMake` или bundled из BuildTools); репозиторий **архивирован 01.03.2025** (последний релиз v2.10, январь 2025) — обновлений и поддержки новых SDK не будет; скачивание ~1–2 ГБ.
* **Вывод:** у вас уже есть BuildTools 18 → инструмент избыточен. Имеет смысл только если хочется «переносимый» тулчейн на другой машине без установки VS — и тогда CMake придётся положить рядом вручную.

### 4.4 Путь «C# / .NET» — да, можно (и это самый быстрый способ закрыть пункты 1, 3, 4)

Проверено по исходникам NAudio (актуальная `main`):

* `src/NAudio.Wasapi/CoreAudioApi/Interfaces/IAudioClient3.cs` — интерфейс объявлен;
* `AudioClient.InitializeSharedAudioStream(AudioClientStreamFlags, uint periodInFrames, WaveFormat, Guid)` — есть;
* `AudioClient.GetSharedModeEnginePeriod(WaveFormat)` → `AudioClientPeriodInfo` — есть;
* `WasapiPlayerBuilder.WithLowLatency()` — высокоуровневая обёртка.

То есть ядро REAL на C# — это ~150 строк: `MMDeviceEnumerator` → `GetDefaultAudioEndpoint` → `IAudioClient3` → период → `InitializeSharedAudioStream` → `Start()`, плюс `NotifyIcon` для трея и `System.Text.Json` для настроек.

Плюсы: быстрее всего писать пункты 1/3/4; трей и меню — из коробки; никаких C++-тулчейнов; простая сборка `dotnet publish`.
Минусы: нужен **.NET SDK 8/9** (~200 МБ, ставится `winget install Microsoft.DotNet.SDK.8`); публикация `--self-contained -p:PublishSingleFile=true` даёт exe ~60–80 МБ (WinForms нельзя тримить, NativeAOT не поддерживается), либо framework-dependent ~1 МБ + требование .NET Desktop Runtime у пользователя; появляется вторая кодовая база вместо развития существующей.

Компромисс «C++ ядро + C# оболочка» не рекомендую — межпроцессное взаимодействие дороже, чем выигрыш.

### 4.5 Бонус: Rust

У вас уже установлен Rust, а самая близкая по духу реализация — `spddl/LowAudioLatency` (Rust, MIT, 210★, живой):
применяет минимум к render **и** capture-endpoint'ам, умеет задавать роль/период через аргументы, гасит «залипание» ядра (`IDLE_PRIORITY_CLASS` + `PROCESS_POWER_THROTTLING_EXECUTION_SPEED`), не держит поток, если изменений не требуется.
Можно либо взять его код как референс для C++-правок, либо пойти по Rust-пути, дописав трей (`tray-icon`) и конфиг (`serde` + `toml`) — но тогда проект теряет связь с REAL и придётся переносить лицензионные атрибуции (обе MIT — совместимо).

### 4.6 GitHub Actions как «сборщик в облаке» (рекомендую в любом случае)

В вашем форке Actions доступны (API отвечает, запусков пока 0). Минимальный workflow решает проблему «нет локальной среды» и, кроме того, даёт мне возможность **проверять компиляцию** правок:

```yaml
name: build
on: [push, workflow_dispatch]
jobs:
  windows:
    runs-on: windows-2022          # MSVC v143 из коробки
    steps:
      - uses: actions/checkout@v4
      - run: cmake -S real-app -B build -G "Visual Studio 17 2022" -A x64
      - run: cmake --build build --config Release
      - uses: actions/upload-artifact@v4
        with: { name: REAL-x64, path: build/Release/real-app.exe }
```

При желании — публикация релиза по тегу и «вечерние» сборки. Если в настройках форка Actions запрещены (`Settings → Actions → Allow all actions`) — нужно один раз разрешить.

**Важная оговорка о проверке из моей среды:** в этой песочнице — Linux, доступ в сеть ограничен, нет ни `mingw-w64`, ни `dotnet`, ни установщика apt; поэтому Windows-бинарник я здесь собрать не могу. Схема такая: сборка и «компиляция прошла» — через GitHub Actions (или на вашей машине), а проверка поведения (звук, смена устройства, сон) — на вашем Win10/Win11 по чек-листу из §6.

---

## 5. План работ по фазам

Зависимости: `0 → 1 → 2 → 3 → 4 → 5`. Фазы 1 и 2 можно частично вести параллельно; фаза 2 — быстрая победа.

### Фаза 0. Гигиена сборки (0,5–1 день)
* `CMakeLists.txt`: убрать `set(CMAKE_BUILD_TYPE Debug)` и `/JMC` из Release, добавить `CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded..."`, опцию `REAL_ENABLE_UPDATER` (по умолчанию OFF), цель `real-app.rc` без изменений.
* `run-cmake.bat`: генератор под ваш VS (или Ninja), `-A x64`, конфигурация Release.
* Новый `real-app/build.bat` — сборка вообще без CMake (вариант из §4.2).
* `real-app/res/real-app.manifest` (+ подключение в `.rc`): `supportedOS` Win10/11, `dpiAware`, `asInvoker`.
* Единый источник версии (например, генерация `Version.h` из CMake или один `#define` в одном файле) + печать версии форка в «шапке» лога.
* `.github/workflows/build.yml`.
* DoD: exe собирается с нуля одной командой и в CI, запускается без установленного VC++ Redistributable, версия совпадает везде.

### Фаза 1. Настройки (1–1,5 дня)
* Новые файлы: `src/Settings.h/.cpp` (структура + загрузка/валидация/дефолты/шаблон), `src/CommandLine.h/.cpp` (`CommandLineToArgvW`, приоритеты, `--config/--tray/--no-tray/--reinit/--enable/--disable/--check-updates/--log-level/--diagnose`).
* Обновить `nlohmann/json` до 3.11 (drop-in) — чтобы поддержать комментарии и `contains()`.
* `docs/CONFIG.md` + `docs/real.settings.schema.json` + шаблон, который пишется при первом запуске.
* Логирование в файл (`logging.*`), потому что без него Win11-диагностика невозможна.
* DoD: при отсутствии файла настройки создаются; правки в файле применяются после «Перечитать настройки» и (опционально) автоматически; приоритет CLI > файл > дефолты; битый файл не валит приложение.

### Фаза 2. Обновления (0,5–1 день)
* `AutoUpdater` вызывается только при `updates.mode != off`; «superseded»-проверка отключаема; ссылки — из конфига.
* Убрать `return 2` и любой принудительный выход; режим `notify` — только уведомление.
* `try/catch` вокруг JSON, проверка наличия полей, таймауты, лимит размера.
* Опционально: замена libcurl на WinHTTP и удаление `deps/curl`.
* DoD: в режиме `off` нет сетевых обращений; при наличии новой версии приложение продолжает работать; при мусорном ответе API — запись в лог, без падения.

### Фаза 3. Трей и цикл сообщений (2–3 дня)
* Всегда работающий цикл сообщений; ввод с консоли — либо в отдельном потоке, либо через `MsgWaitForMultipleObjects` на `stdin` (это снимает блокирующий `_getch`).
* `Console` — исправить закрытие: `SetConsoleCtrlHandler` + `ShowWindow(SW_HIDE)` вместо `SendMessage(WM_CLOSE)`; методы `Show/Hide/Toggle`.
* `TrayIcon`: `NIF_TIP/NIF_SHOWTIP`, `WM_RBUTTONUP` + `TrackPopupMenu`, двойной клик, `NIN_*`, balloon (`NIF_INFO`), перерегистрация по `TaskbarCreated`.
* Основное окно (вариант A из §3.1) или режим «только трей» (вариант C) — по вашему выбору.
* Один экземпляр + сигнализация через `RegisterWindowMessage` (`--reinit`/`--enable`/`--disable`/`--exit` во второй копии).
* «Автозапуск с Windows» (опция) через `HKCU\Software\Microsoft\Windows\CurrentVersion\Run`.
* DoD: см. критерии §3.1.

### Фаза 4. Переинициализация (2–4 дня)
* Новые файлы: `src/Windows/AudioEndpointWatcher.h/.cpp` (`IMMNotificationClient` → `PostMessage`), `src/Windows/HotkeyManager.*`, `src/AudioSessionController.h/.cpp` (владение клиентом, состояние, backoff, статус для UI).
* Рефакторинг `MinimumLatencyAudioClient`: роли, выбор устройства, `Stop()`+release, `CoInitializeEx/CoUninitialize`, разбор HRESULT, обработка `*_LOCKED`, проверка «период уже минимальный», возврат фактического периода.
* Таймер health-check, обработчики `WM_POWERBROADCAST`, `WM_WTSSESSION_CHANGE`.
* Меню/хоткей/CLI для ручного сброса.
* DoD: см. критерии §3.2.

### Фаза 5. Windows 11 и диагностика (1,5–2 дня)
* Человеческие сообщения об ошибках (HRESULT + расшифровка) и «диагностический отчёт» (`--diagnose`: список endpoint'ов, friendly name, драйвер, mix-format, min/default период, версия ОС, версия приложения) в файл — чтобы пользователи присылали один лог вместо скриншотов.
* `processPriority` / `disablePowerThrottling` / «не держать поток, если ничего не меняется».
* Обновление README: установка Win11-драйвера, ограничения Bluetooth/HDMI, что делать при вендорском Realtek, предупреждение про громкость, раздел «Диагностика».
* DoD: на Win11 + HD Audio — 2.67 мс и понятный лог; на Win11 + Realtek — сообщение «драйвер не поддерживает малый период», а не «ERROR: Could not enable».

### Фаза 6. Релиз (0,5 дня)
* Тег `v0.3.0`, сборка в CI, артефакты, обновлённый README/CHANGELOG, при необходимости — свой `update.zip` (если самообновление сохраняем).

**Итого:** ~8–12 человеко-дней. Если приоритет — «чтобы просто работало удобно»: фазы 1+2 (настройки и обновления) и 3 (трей) дают 80 % ощутимой пользы за ~3 дня.

---

## 6. Тест-матрица (ручной прогон)

| Сценарий | Win10 21H2/22H2 | Win11 23H2/24H2/25H2 |
|---|---|---|
| MS HD Audio Device | период падает до 2.67 мс | то же (ожидаемо) |
| Вендорский драйвер (Realtek/ACX) | понятное сообщение | понятное сообщение |
| USB DAC / аудиоинтерфейс | 2.67–5 мс | то же |
| Bluetooth-наушники | «10 мс, драйвер не поддерживает» | то же |
| Смена устройства по умолчанию на ходу | автопереинициализация | автопереинициализация |
| Подключение/отключение USB-наушников | автопереинициализация | автопереинициализация |
| Sleep/Modern Standby → resume | восстановление | восстановление (обязательно) |
| Перезапуск службы `Windows Audio` | восстановление | восстановление |
| Разблокировка сессии | восстановление | восстановление |
| Перезапуск `explorer.exe` | иконка возвращается | иконка возвращается |
| Свернуть/развернуть, крестик, ПКМ-меню | ок | ок |
| `--tray`, `--reinit`, `--diagnose`, второй экземпляр | ок | ок |
| Нагрузка на CPU / уход в сон | без регресса к v0.2.0 | без регресса (проверить CpuSet/affinity) |
| Запуск с сетью и без, битый конфиг | без падений | без падений |

---

## 7. Риски и оговорки

1. **Драйверозависимость.** Малый период поддерживается не всеми драйверами; гарантировать эффект нельзя — можно лишь корректно его применить и сообщить ограничения.
2. **Период движка глобален для endpoint'а.** Если период уже залочен другим клиентом — форсировать нельзя, только «принять» текущий (это и есть обрабатываемый `AUDCLNT_E_ENGINE_PERIODICITY_LOCKED`).
3. **Побочные эффекты** (занятый поток CPU, помеха сну) — свойство аудиостека; полностью устранить нельзя, можно ослабить (приоритет/троттлинг/не держать поток впустую).
4. **Форк и апдейтер.** Апдейтер указывает на апстрим; нужно решить: выключить (рекомендуется), направить на свой форк или снять самообновление вовсе.
5. **Проверка из моей среды.** Скомпилировать и запустить Windows-бинарник здесь невозможно — сборка проверяется в GitHub Actions либо у вас; поведение проверяется по чек-листу §6.
6. **Объём диффа vs совместимость с апстримом.** Чем больше «правильная» архитектура (своё окно, контроллер сессии), тем сильнее расходятся ветки. Если хочется сохранить возможность `git merge` с апстримом (он мёртв с 2019) — надо придерживаться минимального диффа (вариант B в §3.1).
7. **Лицензии.** REAL — MIT (`LICENCE`), `LowAudioLatency` — MIT: заимствование кода допустимо с сохранением атрибуции (файл-уведомление в исходниках).
8. **Изменение настроек «на лету»** требует аккуратной остановки/пересоздания аудиопотока — обязательна синхронизация с единственным «владельцем» состояния (главный поток).

---

## 8. Приложения

* `docs/real.settings.example.json` — предлагаемый внешний файл настроек (с комментариями как документация; рантайм-шаблон будет без комментариев).
* Далее в рамках фазы 1: `docs/CONFIG.md`, `docs/real.settings.schema.json`.

Новые модули (предлагаемая структура, чтобы дифф читался):

```
real-app/src/
  App.h/.cpp                     — жизненный цикл приложения, состояние, меню, горячие клавиши
  Settings.h/.cpp                — конфиг (загрузка/валидация/шаблон/перезагрузка)
  CommandLine.h/.cpp             — разбор аргументов, приоритеты над конфигом
  AudioSessionController.h/.cpp  — владение аудиопотоком, health-check, переинициализация
  Windows/AudioEndpointWatcher.* — IMMNotificationClient → уведомление главного потока
  Windows/MainWindow.*           — окно приложения (если выбран вариант A)
  Windows/PowerSessionWatcher.*  — WM_POWERBROADCAST / WM_WTSSESSION_CHANGE
  Windows/HttpClient.*           — WinHTTP (если апдейтер сохраняем)
```

---

## 9. Статус реализации (ветка `arena/01a0cfaa-real`, 24.09.2026)

Реализовано в коде (собирается в GitHub Actions: компиляция Release на windows-2022
плюс дымовой тест — запуск exe, передача команды второй копией, чистый выход):

| Фаза | Состояние | Что именно |
|---|---|---|
| 0. Гигиена сборки | сделано | Release и `/MT` по умолчанию, версия 0.3.0, манифест (Win10/11, DPI, longPath, asInvoker), `CMakeLists.txt` переписан и больше не тянет внешние зависимости, `real-app\build.bat` (MSVC без CMake), `.github/workflows/build.yml` (сборка + дымовой тест + артефакт/релиз по тегу) |
| 1. Настройки | сделано | `Settings.h/.cpp`, файл `real.settings.json` рядом с exe (создаётся при первом запуске), комментарии `//` и `/* */`, устойчивый разбор с предупреждениями, `--config`/`--no-config`, перезагрузка без перезапуска, `docs/CONFIG.md` и пример со всеми ключами |
| 2. Обновления | сделано | Режимы `updates.mode`: `off` (по умолчанию — ни одного сетевого запроса) и `manual` (одна проверка при запуске, `checkOnStartup`), приложение никогда не закрывается и не обновляется само; HTTP переведён на WinHTTP, libcurl удалён из дерева |
| 3. Трей и цикл сообщений | сделано | Полноценное окно (статус + прокручиваемый лог + кнопки), значок в трее с меню, подсказкой, всплывающими уведомлениями и восстановлением после перезапуска `explorer.exe`; крестик/«свернуть» → трей; одиночный экземпляр и передача команд работающей копии; консольный режим больше не блокирует процесс через `_getch` |
| 4. Переинициализация | сделано | Вручную: меню трея, кнопка в окне, `Ctrl+Alt+R`, `REAL.exe --reinit` (включает режим заново, если он был выключен). Автоматически: смена устройства по умолчанию, изменение состояния, появление устройства (по умолчанию включено) и удаление (выключено), выход из сна, разблокировка сеанса, плюс периодическая проверка раз в 30 секунд (жив ли поток, то ли устройство). При сбое — один всплывающий отчёт на всю аварию, повтор с растущей паузой и пределом `reinit.failureTimeoutMs` (60 с), после чего режим выключается и опрос прекращается |
| 5. Windows 11 и диагностика | сделано | Расшифровка HRESULT (`AUDCLNT_E_*`), обход `AUDCLNT_E_ENGINE_PERIODICITY_LOCKED`/`_FORMAT_LOCKED` через принятие текущего периода, снятие power throttling, управление приоритетом процесса, понятное сообщение при отсутствии `IAudioClient3` (Bluetooth/виртуальные драйверы), подробный лог. Диагностический отчёт: `REAL.exe --diagnose` и кнопка **Diagnostics** пишут `REAL-diagnostics.txt` (версия Windows, устройства, драйвер + версия, формат, периоды default/min/fundamental/max). Поток не создаётся, если драйвер не умеет период меньше стандартного (нет побочки «занятого ядра»). Повтор с нарастающей паузой, если endpoint временно недоступен (`ERROR_NOT_FOUND`, `AUDCLNT_E_DEVICE_INVALIDATED`, перезапуск службы аудио) |
| 6. Релиз | сделано в CI | Пуш тега `v*` создаёт релиз и прикладывает `REAL.exe`; обычные пуши публикуют артефакт `REAL-x64` |

Что **нельзя** проверить в этой среде (Linux-песочница без звукового устройства) и
что нужно прогнать на реальной Windows 11 — см. чек-лист в
[usage.ru.md](usage.ru.md) §2: фактический размер буфера, смена устройства «на лету»,
сон/разблокировка, перезапуск explorer, поведение на Bluetooth/HDMI/Realtek.

**Что уже проверено автоматически** (GitHub Actions, каждый push): сборка Release
(MSVC + windows-2022) и `build.bat` без CMake; дымовой тест — запуск exe, передача
команды второй копией (`--exit`), чистый выход, создание `real.settings.json`,
повторная загрузка этого файла без предупреждений (файл с комментариями читается
приложением) и генерация отчёта `--diagnose`.

**Найдено и исправлено по ходу отладки**: последний `Release()` COM-интерфейса
`IMMDeviceEnumerator` выполнялся уже после `CoUninitialize()`, когда сервер COM
выгружался — падение с `0xC0000005` (адрес и функция определены через журнал
событий Windows + карту компоновки, это же добавлено в CI). Тот же порядок в
основном приложении был корректен.

## 10. Второй круг доработок (24.09.2026)

По итогам первого запуска на реальной машине:

| Пункт | Что сделано |
|---|---|
| Отключение устройства: без спама | Одно уведомление на всю аварию (раньше — на каждую попытку), журнал не забивается, повтор с паузой 2 → 30 с. Через `audio.reinit.failureTimeoutMs` (60 с) приложение перестаёт опрашивать устройство и выключает снижение задержки; в строке состояния — «отключено: аудиоустройство не отвечает» |
| Подключение/смена устройства | Режим включается автоматически, даже если был выключен (`audio.reinit.enableWhenDisabled`) |
| Короткие уведомления | Заголовок до 48, текст до 120 символов; в тексте только суть, подробности — в журнале |
| Консоль | Только основные операции и ошибки (`Log::Operation`); в окне журнал всегда прокручивается к последней строке |
| Клик по значку в трее | Исправлено чтение уведомления формата 4: раньше идентификатор значка брался из слова с координатой курсора, поэтому клик срабатывал через раз («мигающее» окно). Плюс антидребезг 350 мс от дублирующихся сообщений |
| Два языка | Модуль `Lang` (английский/русский): окно, меню трея, подсказки, уведомления, журнал, отчёт диагностики и комментарии в файле настроек. Язык — из `application.language` (`auto`/`en`/`ru`), при `auto` — по языку Windows (ru/uk/be → русский). Комментарии пишутся один раз при создании файла и после смены языка не перезаписываются |
| Локализация журнала | Все сообщения журнала, ошибки WinHTTP/WinHTTP-апдейтера и текст отчёта `--diagnose` берутся из той же таблицы |

Новые ключи настроек: `application.language`, `audio.reinit.enableWhenDisabled`,
`audio.reinit.failureTimeoutMs`, `tray.menu.diagnostics`; `audio.reinit.deviceAdded`
теперь по умолчанию `true`.

Проверено автоматически (GitHub Actions): сборка Release, `build.bat`, дымовой тест
(старт, передача команды второй копией, чистое завершение, создание и повторное
чтение файла настроек, отчёт `--diagnose`) — запуск
[36000486823](https://github.com/Mutaracha/REAL/actions/runs/36000486823) и последующие.

**Найдено на этом круге**: отчёт `--diagnose` падал с `0xC0000409` — строка
`OpDiagnostics` («Отчёт диагностики: {}») выводилась в журнал без аргумента, `fmt`
бросал исключение, и процесс завершался через `abort()` без следов в журнале. Своё
сообщение для начала сбора отчёта добавлено, а `main()` теперь ловит непойманные
исключения и пишет их в журнал, чтобы такое не оставалось незамеченным.

### Что осталось в бэклоге (не входит в текущую версию)

* выбор устройства/периода **на конкретное устройство** (сейчас — «устройство по
  умолчанию» + общие настройки);
* проверка IDE-«списка устройств» в UI: показывать в окне таблицу endpoint'ов
  (данные уже собирает `--diagnose`);
* автовыход, если на текущем устройстве выигрыша нет (`periodSelection = min` и
  `minPeriod == defaultPeriod`) — сейчас приложение просто продолжает работать;
* MMCSS и управление affinity процесса — экспериментально, требует прав;
* проверка на Windows 10 (у пользователя Windows 11; CI проверяет только запуск на
  Windows Server 2022 без звуковых устройств).
