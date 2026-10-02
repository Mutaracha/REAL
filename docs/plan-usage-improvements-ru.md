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

---

## 10. Второй круг доработок (24.09.2026)

По итогам первого запуска на реальной машине:

| Пункт | Что сделано |
|---|---|
| Отключение устройства: без спама | Одно уведомление на всю аварию (раньше — на каждую попытку), журнал не забивается, повтор с паузой 2 → 30 с. Через `audio.reinit.failureTimeoutMs` (60 с) приложение перестаёт опрашивать устройство и выключает снижение задержки; в строке состояния — «отключено: аудиоустройство не отвечает» |
| Подключение/смена устройства | Режим включается автоматически, даже если был выключен (`audio.reinit.enableWhenDisabled`) |
| Короткие уведомления | Заголовок до 48, текст до 120 символов; в тексте только суть, подробности — в журнале |
| Консоль | Только основные операции и ошибки (`Log::Operation`); в окне журнал всегда прокручивается к последней строке |
| Клик по значку в трее | Антидребезг 350 мс: одно нажатие переключает окно ровно один раз. Окончательный разбор уведомлений формата 4 — в §11 |
| Два языка | Модуль `Lang` (английский/русский): окно, меню трея, подсказки, уведомления, журнал, отчёт диагностики и комментарии в файле настроек. Язык — из `application.language` (`auto`/`en`/`ru`), при `auto` — по языку Windows (ru/uk/be → русский). Комментарии пишутся сразу на нужном языке; язык оформления файла хранит служебное поле `commentLanguage` |
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

---

## 11. Третий круг доработок (24.09.2026)

Правки по итогам проверки на живой машине после второго круга:

| Пункт | Что сделано |
|---|---|
| Значок в трее не реагировал ни на левую, ни на правую кнопку | Настоящая причина: идентификатор значка читался из младшего слова `wParam`, а при `NOTIFYICON_VERSION_4` там лежат координаты курсора, идентификатор же — в `HIWORD(lParam)`. Проверка «чужого» значка отбрасывала все сообщения. Теперь разбор по документации: событие — `LOWORD(lParam)`, идентификатор — `HIWORD(lParam)`, координаты — в `wParam`. Левый клик (`NIN_SELECT`, `NIN_KEYSELECT`, он же `WM_LBUTTONUP`) показывает/прячет окно, правый (`WM_CONTEXTMENU`, `WM_RBUTTONUP`) открывает меню; для каждого действия свой антидребезг 350 мс, поэтому одиночный клик срабатывает ровно один раз. `NIM_SETVERSION` вызывается после `NIM_ADD`, результат проверяется |
| Длинное имя кнопки | «Переинициализация»/«Переинициализировать» не помещались: русская надпись кнопки и пункта меню — «Инициализация» (12 знаков, столько же, сколько в «Reinitialize») |
| Комментарии в файле настроек при смене языка | Файл помнит язык оформления в служебном поле `commentLanguage`. Если язык приложения изменился, при следующем запуске (и при перезагрузке настроек) файл перезаписывается: комментарии — на новом языке, **значения сохраняются** — они берутся из уже прочитанных настроек. В журнал пишется одна строка «Файл настроек перезаписан с комментариями на языке … (значения сохранены)» |

Это отменяет прежнее решение «комментарии пишутся один раз и не перезаписываются»:
язык оформления файла теперь следует за `application.language`.

Проверено автоматически (GitHub Actions): сборка Release, `build.bat`, дымовой тест
(старт, передача команды второй копией, чистое завершение, создание и повторное
чтение файла настроек, отчёт `--diagnose`) — запуск
[36004374317](https://github.com/Mutaracha/REAL/actions/runs/36004374317).

**Найдено на этом круге**: первый заход правки значка был собран, но упал на
компиляции — `m_lastMenuTick` использовался в `TrayIcon.cpp`, а в заголовке
объявлен не был. Исправлено, следующий запуск зелёный. Заодно из обработчика
убраны «сырые» `WM_LBUTTONUP`/`WM_RBUTTONUP`: `NOTIFYICON_VERSION_4` присылает их
вместе с `NIN_SELECT` и `WM_CONTEXTMENU`, и обработка обеих пар давала двойное
действие на одно нажатие.

---

## 12. Четвёртый круг доработок (01.10.2026)

Правки по итогам проверки интерфейса, журнала и отчёта диагностики:

| Пункт | Что сделано |
|---|---|
| Файл настроек: меньше служебного | У `configVersion` и `commentLanguage` больше нет комментариев в файле (у них нет значений на выбор); ключ `updates.repository` убран — релизы читаются из репозитория проекта, вшитого в сборку; ключ `logging.toConsole` убран (см. ниже). Ключи из старых файлов принимаются и игнорируются, поэтому файл прежней версии загружается без предупреждений |
| Файл не перезаписывается каждый запуск | Перед запуском читается только служебное поле `commentLanguage`, и файл перезаписывается **лишь** при несовпадении с текущим языком. Если язык тот же, файл не трогается вообще — ни одна строка, включая ваши правки оформления, не меняется |
| Термин «активация» | В русской локали кнопка, пункт меню трея, горячая клавиша и строка журнала говорят «Активировать»/«активация» вместо «Инициализация»/«переинициализация» |
| Кнопки и меню | «Открыть журнал» → «Файл журнала…» (в трее — «Файл журнала») по образцу «Файл настроек…». В окне появилось меню **Файл** с пунктами «Файл настроек…», «Файл журнала…», «Диагностика…», «О программе»; внизу окна остались «Активировать», «Свернуть в трей», «Выход» |
| Журнал и консоль разделены | `logging` описывает **только файл**. Консоль появляется при `showConsole`/`--console` и всегда показывает основные операции и ошибки на уровне `info`; в консоли и в окне у строк появился уровень (`[info]`, `[error]`, `[warning]`) |
| Журнал короче и без дублей | Вместо трёх строк на одно применение («Применяю режим…», «Поток с низкой задержкой запущен…», «Снижение задержки активно: 7.00 мс - …») остаётся одна итоговая: «Снижение задержки активно: Наушники (2- USB HIFI Audio), 48000 Hz, 2 ch, 32 bit, period 336 frames (7.00 ms)» — без `dataFlow`. Подробная строка о потоке пишется только на уровне `debug`. Повторные попытки при недоступном устройстве идут совсем тихо: уже записанных строк о задержке и результате достаточно. Подсказка про `--diagnose` показывается один раз — когда устройство так и не ответило за 60 с, вместо прежней постоянной строки |
| Подсказки отдельно от журнала | Напоминание о горячих клавишах выводится после старта (сразу после строки об отключённом ограничении скорости) и попадает только в окно и консоль; в файле журнала таких информационных строк больше нет. Строки о версии, описании и языке на старте убраны из файла, остаётся `Параметры: dataFlow=…, role=…, period=…, priority=…` |
| Отчёт диагностики | Подписи «Версия:», «Создан:», «Файл:», «Настройки:», «Параметры:» выровнены, литеральные `\n` заменены настоящими переводами строк, регистр и переводы приведены в порядок; в строке «Параметры» остались только значения, влияющие на применение (`dataFlow`, `role`, `period`, `priority`) — состояние трея и параметры запуска убраны; устройства перечисляются по настройке `dataFlow` (при `render` — только вывод); раздел «--- итог ---» переименован в «Примечания» и сокращён (абзац про строку состояния убран) |
| Окно «О программе» | Короткий текст: `REAL v0.3.0`, одна фраза о смысле программы и кликабельная ссылка на проект (открывается в браузере; обычный MessageBox ссылку не умеет) |

---

## 13. Пятый круг доработок (01.10.2026)

Правки по итогам ручного запуска четвёртого круга:

| Пункт | Что сделано |
|---|---|
| Окно «О программе» возвращено к прежнему виду | Своё окно с `SysLink` отменено: ссылка не открывалась, текст выводился сырым (`REAL vv0.3.0`, разметка ссылки), кнопка «Закрыть» была лишней. Вернулся системный `MessageBox` с иконкой информации и прежним текстом (версия, фраза о программе, абзац про клик по значку, путь к настройкам, адрес проекта). Обычная ссылка в MessageBox не кликабельна — это ожидаемо, адрес можно скопировать |
| Отчёт диагностики: разделители и периоды | Вернулись заголовки блоков `--- Устройства вывода (воспроизведение) ---`, `--- Устройства ввода (запись) ---`, `--- Примечания ---`. Строка периодов разбита по одному значению на строку и получила согласование числительных и единицы локали: `стандартный 1920 кадров (10.00 мс)` |
| Номер сборки и коммит в версии | Версия везде (справка, `--version`, окно «О программе», отчёт диагностики, первая строка журнала) — `v0.3.0 RC 39 (47d5109)`: номер запуска CI (`github.run_number`) и коммит сборки. CI передаёт номер через переменную окружения, коммит читается из репозитория (CMake и `build.bat`), поэтому локальная сборка показывает коммит, а сборка из архива — только версию |
| Комментарии в файле настроек — в строке со значением | Файл стал вдвое короче и читается слева направо: `"toggleEnabled": true,  // Пункт включения и выключения режима.` Разделы тоже получили комментарий в строке с `{`. Разбор комментариев при чтении учитывает кавычки, поэтому такой файл читается без предупреждений |
| Пункт трея «Запускать с Windows» привязан к настройке | Галочка в меню и действие пункта берут состояние из `application.startWithWindows`, а не из реестра; при запуске запись в `HKCU\...\Run` приводится в соответствие со значением из файла (правится только при расхождении) |

### Что осталось в бэклоге (не входит в текущую версию)

* ~~**окно настроек** вместо ручного редактирования `real.settings.json`~~ —
  сделано в седьмом круге: пункт «Настройки…» пишет тот же файл;
* **кликабельная ссылка** в справке: своё окно не подошло — ссылка не
  открывалась, разметка выводилась текстом, кнопка «Закрыть» оказалась лишней.
  Справка остаётся системным окном с некликабельным адресом (его можно
  скопировать); если ссылка нужна, вариант — `TaskDialog` с командной ссылкой;
* выбор устройства/периода **на конкретное устройство** (сейчас — «устройство по
  умолчанию» + общие настройки);
* проверка IDE-«списка устройств» в UI: показывать в окне таблицу endpoint'ов
  (данные уже собирает `--diagnose`);
* автовыход, если на текущем устройстве выигрыша нет (`periodSelection = min` и
  `minPeriod == defaultPeriod`) — сейчас приложение просто продолжает работать;
* MMCSS и управление affinity процесса — экспериментально, требует прав;
* проверка на Windows 10 (у пользователя Windows 11; CI проверяет только запуск на
  Windows Server 2022 без звуковых устройств).

---

## 14. Шестой круг доработок (01.10.2026)

| Пункт | Что сделано |
|---|---|
| Справка — прежний вид | Своё окно «О программе» отменено: крупное имя, иконка приложения и ссылки выглядели не так, как ожидалось, ссылка не открывалась, а кнопка «Закрыть» была лишней. Вернулось системное окно с иконкой информации и текстом прежней версии (версия сборки, фраза о программе, напоминание о трее, путь к настройкам, адрес проекта) |
| «О программе» — отдельный пункт меню | Пункт вынесен из выпадающего меню «Файл» в строку меню: в «Файле» остались только файлы и отчёт, а справка вызывается одним нажатием |
| Файл настроек: новый вид применяется сам | Номер версии оформления поднят до `2`: файл, записанный прежней версией (комментарии строкой выше значения), при первом запуске перезаписывается один раз в новом оформлении — комментарий в строке со значением. Значения не теряются, вручную удалять файл не нужно |
| Журнал: меньше шума | Успешная запись файла настроек больше не пишется в журнал (писалась при каждом запуске и смене языка); шаги отчёта диагностики переведены на уровень `debug`, на уровне `info` остаётся одна строка; повторяющиеся строки (недоступное устройство, повторные попытки) не дублируются, а считаются — в журнал попадает `Предыдущее сообщение повторено ещё N раз(а)` |

---

## 15. Седьмой круг доработок (01.10.2026)

Правки по замечаниям после ручной проверки предыдущей сборки.

| Пункт | Что сделано |
|---|---|
| Окно «О программе» | Своё окно: иконка приложения (не системная «информация»), имя крупно и жирным, версия, одна фраза о программе на языке интерфейса и **кликабельные** ссылки «Инструкция» (docs/usage.ru.md), «Параметры» (docs/CONFIG.md) и «Проект» (github.com/Mutaracha/REAL) — клик открывает адрес в браузере. Системное окно с `MessageBoxW` из шестого круга убрано |
| Нажатие «Настройки…» падало | Разбор: окно рисовало фон и цвета само (`WM_ERASEBKGND`/`WM_CTLCOLOR*`) и переживало недокументированную тёмную тему; после падения окно не снимало блокировку с главного, и интерфейс оставался «мёртвым». Тема убрана, окно переведено на системную отрисовку, добавлены проверки (цикл завершается по `WM_QUIT` и возвращает сообщение главному циклу, при закрытии главного окна окно настроек закрывается вместе с приложением) |
| Тёмная тема | **Убрана целиком** (см. пункт 5): меню, полосы прокрутки, флажки и кнопки надёжно не перекрашиваются, а исполнять тёмный вид через недокументированные функции рискованно. Оформление — системное, корректное на любой версии Windows |
| Строка состояния | Статус перенесён из‑под меню вниз окна: последняя строка («утопленная» рамка, как в других программах), кнопки — над ней. Содержимое то же: `2.67 ms - Устройство` |
| Окно настроек | Заодно: страница «Окно» без темы, ссылки на файл и подсказка о перезапуске на месте |
| Файл настроек | Оформление файла поднято до `configVersion: 4`: ключ `application.theme` убран из файла. Файл версии 3 загружается без предупреждений (ключ просто не используется) и при первом запуске перезаписывается один раз — значения сохраняются |

Проверка: все единицы трансляции собираются кросс-компилятором для Windows,
`langtest` (строки языка, чтение файла прежней версии) — без ошибок, пример
`docs/real.settings.example.json` пересобран генератором и разбирается без
предупреждений.

---

## 16. Восьмой круг доработок (01.10.2026)

| Пункт | Что сделано |
|---|---|
| Вкладки окна «Опции» накладывались друг на друга | Ручная отрисовка кнопок-вкладок убрана: вкладки рисует **системный Tab-контрол** (`SysTabControl32`, библиотека `comctl32`). Он сам следит за своей полосой и отдаёт координаты области страниц (`TCM_ADJUSTRECT`), а страницы сдвигаются на эти координаты — наложение невозможно при любом языке и любом системном шрифте |
| В окне консоли всегда пусто | Консоль перестала зависеть от настроек: весь поток уровня `info` и выше идёт в неё всегда, независимо от `logging.level`, `logging.toFile` и содержимого файла настроек (файл по‑прежнему пишется по своим параметрам). Запись идёт через `WriteConsoleW` по дескриптору консоли, а не через `stdout` CRT: у GUI‑приложения поток вывода не инициализирован, из‑за чего прежний способ и давал пустое окно. Окно консоли получило заголовок «REAL — журнал операций» |
| Файл настроек из меню значка | Пункт «Файл настроек» убран из меню значка и из `real.settings.json` (`tray.menu.openSettings`, `configVersion` поднят до `5`). Файл открывается кнопкой **«Открыть файл»** в окне «Опции». Файл версии 4 загружается без предупреждений (ключ в списке допустимых) и один раз перезаписывается в новом оформлении, значения сохраняются |
| «Настройки…» → «Опции» | Переименовано везде: пункт строки меню, заголовок окна, подписи в документации. Команда `OpenSettings` и идентификаторы в коде не менялись |
| Строка меню окна | «Файл» — «Снижение задержки включено» (галочка, переключает состояние), «Запускать с Windows» (галочка, пишет/снимает автозапуск), «Выход»; «Опции»; «Диагностика» → «Диагностика» (отчёт) и «Открыть журнал» (`REAL.log`, только если запись включена и файл существует — иначе понятное объяснение); «О программе». Галочки обновляются при каждой смене состояния, в том числе из значка |
| Кнопки внизу окна | «Свернуть в трей» убрана (действие дублировало системную кнопку). Остались «Активировать» слева и «Выход» справа, ширина ограничена `80…190` px, так что кнопки уменьшаются вместе с окном и всегда прижаты к краям |
| Окно «О программе» | Высота уменьшена на две строки (300 → 264 px), фраза о назначении программы разбита на две строки, строка «Проект» перенесена выше ссылок на инструкцию и параметры |

Проверка: все единицы трансляции собираются кросс-компилятором для Windows,
`langtest` (строки языка, разбор файла версии 4) — без ошибок, пример
`docs/real.settings.example.json` пересобран генератором (`configVersion: 5`) и
разбирается без предупреждений.

---

## 17. Девятый круг доработок (01.10.2026)

Круг закрывает замечания к сборке `v0.3.0 RC 45`.

| Пункт | Что сделано |
|---|---|
| Главное окно и `level` | Окно программы больше не зависит от настроек журнала: поток `info` и выше идёт в него всегда. Приём старый: у одного логгера было два приёмника — буфер окна и файл, — и уровень `off` глушил оба. Теперь уровень файла принадлежит **приёмнику файла**, а буфер окна держит `info`; поэтому `off` не может погасить окно |
| `level` — только файл, `toFile` убран | Файл включается и выключается самим уровнем: `off` — файла нет совсем, любое другое значение — файл ведётся с этой подробностью. Ключ `logging.toFile` убран из файла настроек; старый ключ принимается без предупреждения. Уровень меняется на ходу: при `off` → `info` приёмник файла создаётся заново с текущими путём и ротацией |
| Наложение полей в окне опций | Причина найдена: `AddCombo()` привязывал к вкладке подпись, но **не сам выпадающий список**, поэтому списки всех трёх страниц рисовались в одних и тех же местах. Список теперь привязывается к странице вместе с подписью |
| Консоль убрана совсем | Отдельного окна консоли больше нет: нет `Windows/Console.*`, `LogConsoleRestart`, `ErrConsoleAttach`, ключей `--console`/`--no-console` и `application.showConsole`. Вместе с консолью ушёл и свой приёмник (`ConsoleSink`, `WriteConsoleW`) — он существовал только ради неё |
| Меню значка: разделители | Меню собирается группами, и разделитель рисуется только между двумя непустыми группами: если пункты «Файл журнала», «Диагностика» и «Запускать с Windows» выключены, лишней черты не остаётся |
| Обновления: одна настройка | `updates.mode` убран (был `off`/`manual`), осталась `updates.checkOnStartup`: `true` — одна проверка при запуске, `false` — ни одного сетевого запроса. Старый ключ принимается без предупреждения |
| Окна рядом с главным | «Опции» и «О программе» открываются со смещением от главного окна (40 px вправо и вниз) и прижимаются к рабочей области монитора, а не появляются в центре экрана (`Windows/WindowPlacement.*`) |
| Перезапуск через диалог | Если применённая настройка требует перезапуска (`application.singleInstance` — одна копия), окно опций спрашивает диалогом «Перезапустить программу сейчас?». Согласие запускает новую копию с теми же ключами (`CreateProcessW` в приостановленном состоянии, мьютекс одной копии освобождается до её пробуждения) и корректно закрывает текущую. Остальные настройки применяются на ходу: `startWithWindows` и приоритет процесса теперь тоже применяются сразу, а не только при следующем запуске |
| Окно «О программе» | Ширина уменьшена с 560 до 440 px: две строки описания больше не оставляют пустого места справа |
| Нажатия мимо подписи | Чекбоксы и ссылки теперь по ширине **своего текста** (`Windows/TextMetrics.*`): щелчок далеко справа от подписи больше ничего не переключает, а курсор-рука появляется только над ссылкой |
| Кнопка «Перезапустить» | «Активировать» переименовано в «Перезапустить» (кнопка окна и пункт меню значка): речь о пересоздании аудиопотоков, а не о запуске второй копии |

Проверка: все единицы трансляции собираются кросс-компилятором для Windows,
файлы настроек версий 3, 4 и 5 читаются без предупреждений (значения из них
сохраняются), пример `docs/real.settings.example.json` пересобран генератором
(`configVersion: 6`).

---

## 18. Десятый круг доработок (01.10.2026)

Круг закрывает замечания к сборке `v0.3.0 RC 46`.

| Пункт | Что сделано |
|---|---|
| Пояснение `audio.role` | Роль описана для каждого значения — и в комментарии файла настроек, и в окне опций (под списком появилось пояснение из трёх строк), и в `CONFIG.md`: `console` — игры и системные звуки (так делает большинство программ), `multimedia` — музыка, фильмы, медиаплееры, `communications` — программы связи (мессенджеры, VoIP). Добавлено, что в Windows у каждой роли **своё** устройство по умолчанию |
| Периоды в диагностике | «Основной 1 кадр» оказался не ошибкой чтения: сигнатура `IAudioClient3::GetSharedModeEnginePeriod` — `(default, fundamental, min, max)`, порядок аргументов в коде верный, и драйвер действительно сообщает базовый шаг 1 кадр при минимуме 1344. Ошибкой было **название**: `fundamental` — не период, а ступень сетки периодов. В отчёте строка теперь называется «базовый шаг», в примечаниях объяснено, что шаг меньше минимального использовать нельзя, а `ChoosePeriod()` дополнительно ограничивает результат диапазоном `[минимум, максимум]` и печатает шаг только тогда, когда драйвер его сообщил (без `IAudioClient3` шага нет вовсе — раньше он подменялся минимумом) |
| Ширины полей в окне опций | Все выпадающие списки и все поля ввода приведены к одному размеру — как у поля горячей клавиши (220 px). Раньше списки и путь журнала растягивались почти на всю ширину окна |
| Подсказка про один экземпляр | Строка «Один экземпляр программы включается после перезапуска…» убрана из окна опций (и из таблицы строк). Само поведение осталось: если настройка изменена, окно предложит перезапуск диалогом |
| Перевод приоритетов | `idle` больше не «простой», а «низкий (простой)» — как в диспетчере задач; в файле настроек и документации приоритеты перечислены так же (`normal` — обычный, `belowNormal` — ниже обычного, `idle` — низкий) |
| Разделители меню значка | Меню собирается группами, а перед каждой группой ставится разделитель, который пропускается, если выше ничего нет или выше уже стоит разделитель (`AppendSeparator()` проверяет сам пункт меню через `GetMenuItemInfoW`). При выключенных «Файле журнала», «Диагностике» и «Запускать с Windows» между «Перезапустить» и «Выход» остаётся один разделитель — «О программе» и «Выход» идут одной группой |

Проверка: все единицы трансляции собираются кросс-компилятором для Windows,
пример `docs/real.settings.example.json` пересобран генератором, файлы настроек
версий 3, 4 и 5 читаются без предупреждений.

---

## 19. Одиннадцатый круг доработок (01.10.2026)

Круг закрывает замечания к сборке `v0.3.0 RC 47`.

| Пункт | Что сделано |
|---|---|
| Роль устройства убрана, обрабатываются оба «по умолчанию» | Исходный проект (master, v0.2.0) брал **только** `GetDefaultAudioEndpoint(eRender, eConsole, …)`, то есть обычное устройство по умолчанию для вывода. В Windows же у устройства есть два «по умолчанию» — обычное и **устройство связи**, — поэтому REAL теперь сам собирает устройства по умолчанию **всех** ролей выбранного `dataFlow` (`console`, `multimedia`, `communications`), выкидывает совпадающие (обычно `console` и `multimedia` — это один и тот же endpoint) и держит малый буфер на каждом. Настройка `audio.role` больше не нужна и убрана из файла (`configVersion` = `7`), из окна опций и из строк языка; ключ `role` из старых файлов принимается без предупреждения. Смена устройства связи теперь тоже вызывает повторную активацию, а в отчёте `--diagnose` помечаются оба устройства: `[по умолчанию]` и `[связь]` |
| `IAudioClient3::GetSharedModeEnginePeriod` | Порядок аргументов проверен по заголовку Windows SDK: `(default, fundamental, min, max)` — в коде читается верно, «базовый шаг 1 кадр» при минимуме 1344 кадра приходит именно от драйвера. `MinimumLatencyAudioClient::Start()` теперь принимает готовое устройство (`IMMDevice&`), а не ищет его сам (см. первый пункт), а сам период по-прежнему удерживается в диапазоне `[минимум, максимум]` |
| `ComPtr` | Добавлены перемещающие конструктор и присваивание: без них указатель нельзя было положить в контейнер (список устройств по умолчанию) |
| Описание `"fixed"` | Формулировка «значение ключа ниже (requestedPeriodFrames)» заменена на «fixed — ровно столько кадров, сколько указано в requestedPeriodFrames»; у самого `requestedPeriodFrames` сказано, что он выравнивается по базовому шагу и удерживается в диапазоне устройства, а `0` означает минимальный период устройства |

Проверка: все единицы трансляции собираются кросс-компилятором для Windows,
файлы настроек версий 3, 4, 5, 6 читаются без предупреждений, пример
`docs/real.settings.example.json` пересобран генератором (`configVersion: 7`).


---

## 20. Двенадцатый круг доработок (01.10.2026)

Круг закрывает замечания к сборке `v0.3.0 RC 48`.

| Пункт | Что сделано |
|---|---|
| Термины приоритета процесса | Выпадающий список в окне «Опции» и комментарий в файле настроек используют формулировки диспетчера задач: **Обычный**, **Ниже среднего**, **Низкий** (было «обычный», «ниже обычного», «низкий (простой)»). Значения ключа (`normal`, `belowNormal`, `idle`) не менялись |
| `audio.releaseOnExit` убран | Настройка не читалась ни одной ветвью кода: при выходе процесса потоки закрываются в любом случае, и движок сам возвращается к 10 мс. Ключ удалён из файла (`configVersion` = `8`), из окна «Опции» и из строк языка; файл старой версии с этим ключом читается без предупреждения, а сам ключ исчезает при первой же перезаписи файла |

Проверка: все единицы трансляции собираются кросс-компилятором, таблица строк
сходится по количеству, файлы настроек версий 3–8 читаются без предупреждений,
пример `docs/real.settings.example.json` пересобран (`configVersion: 8`).

---

## 21. Тринадцатый круг доработок (01.10.2026)

Круг закрывает замечания к сборке `v0.3.0 RC 49`.

### 21.1. Интерфейс и мелкие правки

| Пункт | Что сделано |
|---|---|
| Кнопка «Перечитать» | В окне «Опции» рядом с «Сохранить» появилась кнопка **Перечитать**: она заново читает файл в окно, поэтому значение, поправленное в редакторе, попадает в окно без закрытия. Пишет и применяет по-прежнему только «Сохранить» |
| Неактивные поля | Поля, значение которых ничего не значит, пока выключен их главный переключатель, гаснут: горячие клавиши, путь и ротация журнала (при `level: "off"`), период (нужен только для `periodSelection: "fixed"`), пункты меню значка (когда значок выключен) |
| Подписи, которые вводили в заблуждение | `reinit.debounceMs` — пауза **перед активацией** (а не «перед повтором»), `reinit.failureTimeoutMs` — сколько **ждут устройство**, поле числа файлов говорит, что именно считает. Значения списков начинаются с прописной буквы, как и подпись над ними |
| `tray.showStatusInTooltip` убран | Подсказка значка всегда показывает состояние — для этого она и нужна. Ключ удалён (`configVersion` = `9`); файл со старым ключом читается без предупреждения |
| Хвосты убранной консоли | Удалены: запись снимка журнала (`WriteSnapshotToFile`/`LogBuffer::Snapshot`), которую никто не вызывал; `Command::ReloadSettings` и `App::ReloadSettings` (их заменила кнопка); девять строк языка, на которые не осталось ссылок; устаревший пример «RC 39» в трёх местах; строка README о том, что `--diagnose`/`--help`/`--version` передаются работающей копии |
| Проверка таблицы строк | Статическая проверка `static_assert(TABLE_SIZE == Str::Count)` в `Lang.cpp` (в код она попала в 21.3): id без строки или строка без id ломают компиляцию |
| Журналы сборки в дереве | `ci/` больше не хранится в репозитории: файлы убраны из индекса, правило `.gitignore` работает |

### 21.2. Масштабирование интерфейса (per-monitor v2)

Причина: нестандартный масштаб (например, 225 %), выставленный через старый диалог
масштабирования Windows, система отдаёт как масштаб **монитора**, а манифест
объявлял только обычное `dpiAware`. Приложение получало один общий коэффициент,
растягивало им готовую картинку — отсюда мыльные шрифты и обрезанные подписи.

| Пункт | Что сделано |
|---|---|
| Манифест | Объявлен `PerMonitorV2` (`dpiAwareness`), старое `dpiAware=true` оставлено для систем, которые не знают нового ключа |
| Модуль `Windows/Dpi.*` | Точка входа одна: `Dpi::ForWindow`/`ForSystem`, `Dpi::Scale(value, dpi)` (`MulDiv`, как считает сама Windows), `AdjustWindowRect`, шрифты. Функции `GetDpiForWindow`/`GetDpiForSystem`/`AdjustWindowRectExForDpi` ищутся в `user32.dll` во время работы, поэтому на системе без per-monitor DPI приложение по-прежнему запускается |
| Вёрстка | Все размеры остаются расчётными пикселями вёрстки 96 DPI и умножаются на DPI монитора в момент создания окна: один и тот же макет, просто крупнее или мельче. Размер окна считается от DPI, а не подгоняется растягиванием уже созданного |
| Шрифты | Берутся из системного `lfMessageFont` (`SPI_GETNONCLIENTMETRICS`), поэтому размер и начертание из параметров экрана уважаются; заголовки — тот же шрифт на 108 % и полужирный, имя в «О программе» — 200 % жирный, журнал — Consolas |
| Переезд на другой монитор | `WM_DPICHANGED` применяет предложенный системой прямоугольник (окно сохраняет соразмерность) и перестраивает содержимое: главное окно заменяет шрифты, не пересоздавая контролы (уже показанный журнал остаётся), окно опций сначала **читает введённые значения**, затем пересоздаёт контролы — набранное не теряется, «О программе» пересобирает строки, а значок грузится в размере текущего DPI |
| Окна рядом с главным | Смещение 40 px стало расчётным: на 225 % оно умножается, и окно «Опции» больше не ложится на главное |
| Отчёт диагностики | В отчёт добавлена строка «Масштаб: 225 %» — по ней видно, с каким масштабом снят отчёт |

Проверка: все единицы трансляции собираются кросс-компилятором для Windows,
файлы настроек версий 3–9 читаются без предупреждений.

### 21.3. Файл настроек без истории, перевод журнала, окно «Опции»

Замечания к сборке `v0.3.0 RC 51`.

| Пункт | Что сделано |
|---|---|
| Версия файла настроек — `1` | История ключей удалена целиком: из загрузчика ушли «бросаемые» чтения (`role`, `releaseOnExit`, `repository`, `mode`, `toConsole`, `toFile`, `showStatusInTooltip`) и списки «тихих» имён (`theme`, `showConsole`, `openSettings`), из `Settings.h` — перечень версий 2–9. Ключ, которого программа не знает, теперь всегда даёт предупреждение «неизвестный параметр, игнорируется» и исчезает при перезаписи. Файл с номером, отличным от текущего (в том числе `9`), один раз перезаписывается в текущем виде — сравнение `!=` вместо `<`, иначе файлы с номером `9` никогда не обновились бы |
| Окно «Опции»: два ряда кнопок | Путь к файлу перекрывался кнопками. Теперь первый ряд — «Открыть файл», «Перечитать» и путь (занимает весь остаток ряда), второй — «Отмена» и «Сохранить» справа; вкладки заканчиваются над обоими рядами. «Перечитать» при ошибке называет причину (место лишней запятой) |
| «Фрейм» вместо «кадра» | Термин заменён везде: строки языка, комментарии файла настроек, подпись поля периода, отчёт диагностики, документация. Числа согласуются с существительным: `Lang::Frames()` / `Lang::Channels()` дают «1 фрейм», «2 фрейма», «1344 фрейма», «2 канала» |
| Строка журнала о потоке переведена | `2 ch, 32 bit, period 1344 frames (7.00 ms)` → «48000 Гц, 2 канала, 32 бит, период 1344 фрейма (7.00 мс)» (`Str::StreamDetails`); имя устройства не переводится. Строка формата в отчёте диагностики приведена к тому же виду, сообщение о занятом периоде — тоже |
| Стартовые сообщения о файле — в журнал | Предупреждения и ошибки чтения файла, «Файл настроек создан», неизвестный ключ командной строки и сбой мьютекса больше не печатаются в stdout (которого у оконной программы нет), а копятся до создания журнала и выводятся в окно и файл сразу после строки запуска. Язык известен до чтения файла (`Config::PeekLanguage`), поэтому и сами сообщения загрузчика переведены (`Str::CfgWarn*`, `Str::CfgErr*`) |
| Объяснения убранного функционала | Удалены из `CONFIG.md`, `usage.ru.md` и README: раздел о тёмной теме, «ключ `role` / `toConsole` / `repository` из старых файлов принимается», история номеров версий, упоминание Rust, история исправления клика по значку. Журнал кругов (этот файл) остаётся историей и не правится задним числом |
| Ключи командной строки — только на запуск | `--tray`, `--log-level`, `--multi-instance` раньше смешивались с настройками и при любом сохранении (пункт «Запускать с Windows», кнопка «Сохранить») попадали в файл навсегда. Теперь у программы два набора: значения файла (их правит окно и пишет сохранение) и действующие значения (файл + ключи). Ключи применяются и при испорченном файле |
| Автозапуск следует за файлом | Команда записи `HKCU\…\Run` сверяется при запуске и после сохранения: смена «Старт свёрнутым в трей» исправляет `--tray` в записи, переезд программы — путь. Раньше запись менялась только вместе с флажком автозапуска |
| `--enable` / `--disable` = пункт меню | Команды второй копии идут через тот же `SetAudioEnabled()`, что меню, кнопка и горячая клавиша: сбрасывают состояние сбоя и таймер повтора, поэтому всегда дают видимый результат |
| Надёжная запись файла | Запись через `real.settings.json.tmp` + `MoveFileExW(REPLACE_EXISTING \| WRITE_THROUGH)`: прерванная запись не оставляет обрезанного файла. Файл, который не удалось разобрать, перед первой записью сохраняется как `real.settings.json.bad`, о самой ошибке при запуске говорит одно уведомление у значка |
| Горячие клавиши | Перерегистрируются только при изменении своего раздела, а не при каждом сохранении (иначе предупреждение о занятой комбинации повторялось) |
| Мелочи | Ключи командной строки регистронезависимы и для `--tray`/`--no-tray`; справка `--reinit` — «перезапустить: активировать заново»; комментарий раздела `reinit` — «Когда активировать заново автоматически»; параметры звука в журнале — строка языка с ключами файла (`dataFlow=…, periodSelection=…, processPriority=…`); имена параметров в предупреждениях без служебного `root.` и без повтора (`configVersion`, а не `configVersion.configVersion`) |
| Мёртвый код | Удалены `DescribeEndpointError`/`IsTransientEndpointError` и строка `ErrEndpointTransient`, недостижимая ветка таймера проверки с полем `m_lastApplyFailed` и строкой `LogApplyRetry`, объявление `TraceStep` без определения, `IsStartWithWindowsEnabled`, второй анонимный namespace в `App.cpp`, два одинаковых блока вывода в консоль (`WriteToParentConsole`); дыра в номерах пунктов меню значка закрыта; `DeviceNotificationClient` помечен `final` (предупреждение компилятора об удалении через тип без виртуального деструктора) |

Проверка: все единицы трансляции собираются кросс-компилятором для Windows без
предупреждений в коде проекта; пример `docs/real.settings.example.json` пересобран
генератором (`configVersion: 1`) и читается без предупреждений; испорченный файл,
массив вместо объекта, значения неверных типов и вне диапазона дают переведённые
сообщения с правильными именами параметров.

### 21.4. Строки журнала без ключей запуска

Замечания к сборке `v0.3.0 RC 53`.

| Пункт | Что сделано |
|---|---|
| Подсказка после отказа устройства | «Проверьте доступные устройства: REAL.exe --diagnose» → «Устройство не отвечает. Проверьте доступные устройства, запустив диагностику.»: диагностика — пункт меню, ключ запуска в окне программы не нужен |
| Остальные строки окна | Проверены все строки, которые попадают в окно журнала и в уведомления. Ключ запуска больше не встречался; имя ключа файла было в одной — «Файл журнала выключен (logging.level = "off")» → «…: уровень журнала off (окно «Опции», вкладка «Прочее»)». Ключи остались только там, где речь о самой командной строке: справка `--help` и ошибки вида «Для --config нужен путь» |
| Подсказки с `%s` | Три подсказки окна (перезапуск нужен, перезапуск отложен, журнал недоступен) передавались в форматирование `fmt` с шаблоном `"%s"` и выводились буквально как «%s». Шаблон заменён на `"{}"` |
| Причина неудачного перезапуска | Код ошибки `CreateProcessW` передавался строке без места для него и терялся; в журнале теперь «Не удалось запустить новую копию REAL: <причина>», окно с советом закрыть и запустить программу осталось прежним |
| Журнал окна переставал пополняться | Поле журнала в главном окне — системный `EDIT`, который по умолчанию принимает около 32 000 символов, а `EM_REPLACESEL` за этим пределом молча ничего не добавляет: после нескольких сотен строк окно «замерзало» (собственный предел очистки, 200 000 символов, не срабатывал никогда). Предел поля снят (`EM_SETLIMITTEXT`), а при переполнении удаляется старшая половина журнала по границе строки, а не весь текст сразу |

Проверка: число аргументов каждого вызова `fmt::format`/`Log::*` со строкой из
таблицы сверено с подстановками в обеих версиях строки (126 вызовов, расхождений нет).

### 21.5. Окно «Опции» и «О программе»: интерфейс

Замечания к сборке `v0.3.0 RC 55` (снимки главного окна, «О программе» и трёх
вкладок «Опций»), план утверждён до правок.

| Пункт | Что сделано |
|---|---|
| Разрыв правой рамки вкладок | Каждый жирный заголовок раздела был шириной во всё окно и закрашивал своим фоном правую границу вкладок. Теперь заголовок шириной со свой текст (измеряется жирным шрифтом), а любой элемент страницы — заголовок, флажок — ограничен правым краем области вкладок (`pageRight`, считается из `TCM_ADJUSTRECT` с теми же 2 px отступа) |
| Высота окна | Окно подгоняется под самую высокую вкладку: страницы возвращают реальный нижний край (для «Окна» — последний ряд сетки «Пункты меню в трее», раньше функция прибавляла лишние ряд и отступ), вкладки заканчиваются в 8 px под ним, два ряда кнопок идут сразу под вкладками. Подгонка выполняется в реальных пикселях текущего DPI и при открытии, и при перестроении после переноса на монитор с другим масштабом; после подгонки окно ещё раз ставится рядом с главным для своего настоящего размера. При 100 % окно ниже примерно на 90 px, при 225 % — примерно на 200 px |
| Уровни журнала | Список «Уровень» — от «ничего» к «всему»: «Выключен (off)», «Ошибки (error)», «Предупреждения (warn)», «Основное (info)», «Подробно (debug)», «Всё (trace)»; в скобках значение файла. Значение файла читается так же, как его читает журнал: без учёта регистра и с синонимами (`none`, `warning`, `err`). Тот же порядок — в комментарии файла настроек, в справке `--help` и в документации |
| Клавиатура | Порядок Tab — вкладки, поля открытой вкладки, кнопки внизу последними (кнопки раньше создавались первыми и шли первыми). При открытии и после перестроения фокус на вкладках. Ctrl+Tab / Ctrl+Shift+Tab и Ctrl+PgDn / Ctrl+PgUp переключают вкладки из любого поля; поле скрытой вкладки фокус не удерживает. Перестроение после смены DPI больше не перескакивает на вкладку «Прочее», а остаётся на открытой |
| «О программе» | Длинный путь к файлу настроек сокращается посередине (`SS_PATHELLIPSIS`), имя файла видно всегда — так же, как в «Опциях» |

### 21.6. Меню значка, окно «Настройки REAL», одна копия, разбор LowAudioLatency

Замечания к сборке `v0.3.0 RC 56` (снимки трёх вкладок, главного окна и «О программе»)
и разбор репозитория spddl/LowAudioLatency; план утверждён до правок.

| Пункт | Что сделано |
|---|---|
| «Перезапустить» вместо «активации» | Подпись горячей клавиши «Активировать заново» → «Перезапустить», комментарий `hotkeys.reinitialize` → «Перезапустить: заново создать аудиопотоки.», раздел «Повторная активация» → «Автоматический перезапуск», «Пауза перед активацией» → «Пауза перед перезапуском». Так же переписаны строки журнала («…, перезапускаю», «Горячие клавиши: включить/выключить …, перезапустить …»), комментарии раздела `reinit` и пункта меню, справка `--reinit`. Имена ключей не менялись |
| Меню значка | Строка состояния, «REAL запущен», «Перезапустить» — «Файл журнала», «Диагностика» — «Настройки» — «Выход». «О программе» и «Запускать с Windows» остались только в главном окне. Строку состояния, «Настройки» и «Выход» скрыть нельзя (ключи `tray.menu.showStatus`, `startWithWindows`, `about`, `exit` удалены); разделитель группы, у которой скрыты все пункты, не рисуется |
| «REAL запущен» | Так называется переключатель режима в меню значка, в меню «Файл» главного окна и в списке пунктов меню в окне настроек |
| «Опции → Настройки» | «Опции» в строке меню стало выпадающим меню с пунктом «Настройки», заголовок окна — «Настройки REAL». Если окно уже открыто, пункт «Настройки» меню значка выводит его на передний план; открытое из трея при скрытом главном окне, оно получает свою кнопку на панели задач. После закрытия «Настроек» и «О программе» главное окно возвращается в то состояние (доступно или нет), в каком было до открытия |
| Вкладка «Окно» | «Приложение» без «Один экземпляр»; затем «Значок в трее и его меню»: «Показывать значок в трее» и под ним с отступом пункты «REAL запущен», «Перезапустить», «Файл журнала», «Диагностика» (серые без значка); затем «Уведомления» (тоже серые без значка — их показывает значок) |
| Одна копия всегда | Ключ `application.singleInstance` и ключ запуска `--multi-instance` удалены. Мьютекс проверяется до чтения настроек: повторный запуск передаёт команду работающей копии и завершается, не трогая файл настроек и журнал; `--exit` без работающей копии сразу завершается; если копия не отвечает, новая не запускается и сообщает «REAL уже запущен, но не отвечает» (в консоль, если запуск из неё, иначе окном). Диалог перезапуска программы и весь механизм перезапуска (`NeedsRestart`, `AskForRestart`, `RestartApplication`, семь строк языка) удалены: настроек, которым нужен перезапуск, больше нет |
| Вкладка «Звук» | «Включать при запуске» удалено (`audio.enabledOnStartup`): режим включается при каждом старте. «Разрешить Windows подбирать период» удалено (`audio.allowPeriodSnap`): период, закреплённый другим приложением, REAL принимает всегда (при снятом флажке было: ошибка, повторы 60 с и ложное «устройство не отвечает»). Строка журнала — «Период аудиодвижка уже закреплён другим приложением, используется его период: …» (было «выбран ближайший период»), в строке состояния — «(период закреплён другим приложением)». «Включать, если драйвер сбросил режим» → «Включать при смене устройства, если выключено»: подпись приведена к тому, что делает флажок |
| Энергосбережение | Флажок «Отключить энергосбережение» (`performance.disablePowerThrottling`) удалён. Код, как и в LowAudioLatency, на самом деле **включал** EcoQoS (ControlMask = StateMask = EXECUTION_SPEED), а журнал писал «Ограничение скорости выполнения отключено». Поведение оставлено без настройки (процесс REAL только держит поток, звук обрабатывает служба Windows Audio), строк в журнале о нём больше нет. Комментарий раздела `performance`, README и инструкция больше не утверждают, что приоритет и энергосбережение REAL уменьшают треск или занятость ядра |
| Нет устройства по умолчанию | «[error] Ни одно аудиоустройство не удалось проверить.» при старте с Windows → «[warn] Аудиоустройство по умолчанию не подключено.»: не ошибка программы, а препятствие работе. Остальные сбои применения остаются ошибками |
| Ядро для звука в диагностике | В шапке отчёта строка «Процессор: логических ядер: N; зарезервировано для звука: нет» (или «ЦП 0»): `GetSystemCpuSetInformation`, набор ЦП с признаком RealTime, выделенный процессу; функция ищется во время работы. В примечаниях отчёта — абзац о том, что это особый режим низкой задержки самой Windows. Снятие резервирования, как в LowAudioLatency (`NtSetSystemInformation`, права администратора, вся система), не переносилось |
| LowAudioLatency, остальное | Удержание потока, обработка записи, пропуск отсутствующего устройства по умолчанию, отказ держать поток при минимальном периоде, равном стандартному, свой период и низкий приоритет — у нас уже были |
| Ширина окна настроек | Окно по ширине самой широкой строки: столбец подписей — по самой длинной подписи языка окна плюс 16, отступ справа от поля до рамки вкладок равен левому (2 px внутри области вкладок). Ширина не меньше двух кнопок и 160 точек для пути к файлу; пересчитывается и после смены масштаба |
| Кнопки | Все кнопки окна настроек и «Перезапустить» / «Выход» главного окна одного размера: высота 23 (кнопка диалога Windows), ширина — самая длинная из шести подписей плюс по 12 с каждой стороны, но не меньше 75 (`StandardButtonWidth`) |
| «О программе» | Новый текст: «Пока REAL запущен, Windows использует минимальный аудиобуфер, поддерживаемый драйвером устройства.» (две строки). Ширина окна — по самой длинной строке, поля слева и справа по 16; длинный путь к файлу сокращается посередине и окно не расширяет |
| Мелочь | При закреплённом периоде с нулевым значением формат от `GetCurrentSharedModeEnginePeriod` не освобождался — исправлено вместе с переделкой этой ветки |

Файл настроек прошлой сборки читается с восемью предупреждениями «неизвестный параметр,
игнорируется» (удалённые ключи); они пропадают после «Сохранить» в окне настроек. Номер
версии файла остаётся `1`.

Проверка: все единицы трансляции собираются кросс-компилятором для Windows без
предупреждений в коде проекта; пример `docs/real.settings.example.json` пересобран
генератором и читается без предупреждений; число аргументов вызовов форматирования
сверено с подстановками (141 вызов, расхождений нет).
