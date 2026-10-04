#include "Lang.h"

#include "Text.h"

#include <Windows.h>

#include <spdlog/fmt/fmt.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstddef>
#include <iterator>

using namespace miniant::Lang;

namespace {

struct Entry {
    Str id;
    const char* english;
    const char* russian;
};

// Every string of the application. English first, Russian second; both texts
// must be present, the count is validated at run time in Initialize().
const Entry TABLE[] = {
    // Main window: the menu bar and the restart button of the status line
    { Str::WindowTitle, "REAL - REduce Audio Latency", "REAL - REduce Audio Latency" },
    { Str::StatusStarting, "Starting...", "Запуск..." },
    { Str::MenuProgram, "REAL", "REAL" },
    { Str::MenuRestart, "Restart", "Перезапустить" },
    { Str::MenuExit, "Exit", "Выход" },
    { Str::MenuOptions, "Options", "Опции" },
    { Str::MenuStartWithWindows, "Start with Windows", "Запускать с Windows" },
    { Str::MenuDiagnostics, "Diagnostics", "Диагностика" },
    { Str::MenuOpenLog, "Open the log", "Открыть журнал" },
    { Str::MenuAbout, "About", "О программе" },

    // Tray menu
    { Str::TrayToggleEnabled, "REAL is running", "REAL запущен" },
    { Str::TrayReinitialize, "Restart", "Перезапустить" },
    { Str::TrayLog, "Log file", "Файл журнала" },
    { Str::TrayDiagnostics, "Diagnostics", "Диагностика" },
    { Str::TraySettings, "Settings", "Настройки" },
    { Str::TrayExit, "Exit", "Выход" },

    // Status line
    { Str::StatusDisabled, "Latency reduction is off", "Снижение задержки выключено" },
    { Str::StatusNotActive, "Latency reduction is not active", "Снижение задержки не активно" },
    // The milliseconds come written in the notation of the language (see
    // Lang::Milliseconds): "7.00 ms", "7,00 мс".
    { Str::StatusDriverMinimum, "driver already uses its smallest buffer ({}) - {}",
                                "драйвер уже отдаёт минимальный буфер ({}) - {}" },
    { Str::StatusPeriodLocked, " (buffer set by another app)", " (буфер задан другим приложением)" },
    { Str::StatusMoreDevices, " (+{} more)", " (+{} ещё)" },

    // Notifications: short texts, a balloon shows only a few words
    { Str::NotifyTitle, "REAL", "REAL" },
    { Str::NotifyAudioTitle, "REAL - audio", "REAL - звук" },
    { Str::NotifyDeviceTitle, "REAL - device", "REAL - устройство" },
    { Str::NotifyDeviceNotReady, "Device is not available", "Устройство недоступно" },
    { Str::NotifyDisabledNoDevice, "Off: no audio device", "Выключено: нет аудиоустройства" },
    { Str::NotifyUpdateAvailable, "REAL {} is available", "Доступна версия REAL {}" },
    { Str::NotifyUpdateLatest, "Latest version ({})", "Последняя версия ({})" },
    { Str::NotifyUpdateFailed, "Update check failed", "Проверка обновлений не удалась" },
    { Str::NotifyUpdateClick, "Click to open the release page", "Нажмите, чтобы открыть страницу релиза" },
    { Str::NotifyReportFailed, "Could not write the report", "Не удалось записать отчёт" },
    { Str::NotifySettingsUnreadable,
      "The settings file could not be read, the defaults are used",
      "Файл настроек не прочитан, работают значения по умолчанию" },

    { Str::StatusSuspended,
      "disabled: the device does not answer",
      "отключено: устройство не отвечает" },
    { Str::StatusActive,
      "{} - {}",
      "{} - {}" },
    // The technical part of a stream in the journal: the format of the device
    // and the buffer it runs with ({1} and {3} come with their nouns).
    { Str::StreamDetails,
      ", {0} Hz, {1}, {2} bit, buffer {3} ({4})",
      ", {0} Гц, {1}, {2} бит, буфер {3} ({4})" },
    { Str::UnknownDevice,
      "<unknown device>",
      "<неизвестное устройство>" },
    { Str::FlowRender,
      "output",
      "вывод" },
    { Str::FlowCapture,
      "input",
      "ввод" },
    { Str::LogUnknownArgument,
      "Unknown command line option: {0} (ignored).",
      "Неизвестный ключ командной строки: {0} (игнорируется)." },
    // The settings of the latency reduction in the words of the settings
    // window: {0} is the direction (DescribeFlow*), {1} the buffer
    // (DescribeBuffer*). The diagnostics report builds its line from the same
    // parts (DiagConfig).
    { Str::LogAudioSettings,
      "Latency reduction applied to {0} devices; {1}.",
      "Снижение задержки применено для устройств {0}; {1}." },
    { Str::DescribeFlowRender, "playback", "воспроизведения" },
    { Str::DescribeFlowCapture, "recording", "записи" },
    { Str::DescribeFlowBoth, "playback and recording", "воспроизведения и записи" },
    { Str::DescribeBufferMinimum, "minimum buffer", "буфер минимальный" },
    { Str::DescribeBufferFixed, "fixed buffer, {0}", "буфер фиксированный, {0}" },
    // {0} is the fixed buffer of the settings, {1} the device, {2}-{3} the
    // range of its buffer, {4} the step, {5} the buffer that is used instead.
    { Str::LogFixedBufferAdjusted,
      "The fixed buffer of {0} does not fit the device \"{1}\" ({2}–{3}, step {4}), using {5}.",
      "Фиксированный буфер {0} не подходит устройству «{1}» ({2}–{3}, шаг {4}), используется {5}." },
    { Str::LogTrayForeignEvent,
      "Tray event 0x{0:04X} for the icon {1}, this icon is {2}.",
      "Событие значка 0x{0:04X} для значка {1}, а это значок {2}." },
    { Str::LogMutexFailed,
      "Could not create the single instance mutex: {0}",
      "Не удалось создать мьютекс единственного экземпляра: {0}" },
    { Str::LogSettingsUnreadable,
      "Built-in default settings are used; the settings file is left as it is.",
      "Используются встроенные значения настроек; файл настроек не изменён." },
    { Str::LogSettingsBackup,
      "The settings file that could not be read is kept as {0}.",
      "Файл настроек, который не удалось прочитать, сохранён как {0}." },
    { Str::LogComFailed,
      "Could not initialize COM: {0}",
      "Не удалось инициализировать COM: {0}" },
    { Str::LogWindowFailed,
      "Could not create the main window: {0}",
      "Не удалось создать главное окно: {0}" },
    { Str::LogInterfaceScale,
      "Interface scale: {0}%\n",
      "Масштаб интерфейса: {0}%\n" },
    { Str::LogSessionNotifications,
      "Session notifications are not available: {0}",
      "Уведомления о сеансах недоступны: {0}" },
    { Str::LogTrayHidden,
      "The tray icon is off: the window is shown instead of starting in the tray.",
      "Значок в трее выключен: окно показано, а не свёрнуто в трей." },
    { Str::LogPriorityFailed,
      "Could not change the process priority: {0}",
      "Не удалось изменить приоритет процесса: {0}" },
    { Str::LogSettingsWriteFailed,
      "Could not write the settings file {0}.",
      "Не удалось записать файл настроек {0}." },
    { Str::LogFileOff, "The log file is switched off: the log level is off (Options - Settings, the Other tab).",
                       "Файл журнала выключен: уровень журнала off (Опции → Настройки, вкладка «Прочее»)." },
    { Str::LogFileMissing, "There is no log file yet.", "Файла журнала пока нет." },
    { Str::LogLanguageChanged,
      "Language changed to {0}.",
      "Язык изменён на {0}." },
    { Str::LogUpdatesDisabled,
      "The update check at startup is switched off.",
      "Проверка обновлений при запуске выключена." },
    { Str::LogUpdateRunning,
      "An update check is already running.",
      "Проверка обновлений уже выполняется." },
    { Str::LogNoReleases,
      "No versions have been published yet.",
      "Опубликованных версий пока нет." },
    { Str::LogReinitInvalid,
      "The audio streams are no longer valid, restarting: {0}",
      "Аудиопотоки недействительны, перезапускаю: {0}" },
    { Str::LogAutostartRemoveFailed,
      "Could not remove the autostart entry: {0}",
      "Не удалось удалить запись автозапуска: {0}" },
    { Str::LogAutostartWriteFailed,
      "Could not write the autostart entry: {0}",
      "Не удалось записать запись автозапуска: {0}" },
    { Str::LogUpdateLeftover,
      "Removed the leftover file from a previous update.",
      "Удалён оставшийся файл от прошлого обновления." },
    { Str::LogStopped,
      "REAL stopped.",
      "REAL остановлен." },
    // Written instead of a line that repeats: the retries of a failed audio
    // apply produce the very same text over and over. {0} is the number of the
    // repetitions, {1} is the word for it in the right form (see RepeatWord).
    { Str::LogRepeated,
      "The previous message was repeated {0} more {1}.",
      "Предыдущее сообщение повторено ещё {0} {1}." },
    { Str::LogLowLatencyStarted,
      "Low latency stream started: {0}",
      "Поток с низкой задержкой запущен: {0}" },
    { Str::LogPeriodLocked,
      "The audio engine buffer is already set by another application, its size is used: {0}.",
      "Буфер аудиодвижка уже задан другим приложением, используется его размер: {0}." },
    { Str::LogDiagCollected,
      "Diagnostics: location and configuration collected.",
      "Диагностика: расположение и настройки собраны." },
    { Str::LogDiagEnumeratorReady,
      "Diagnostics: the device enumerator is ready.",
      "Диагностика: перечислитель устройств доступен." },
    { Str::LogDiagEnumeratorMissing,
      "Diagnostics: the device enumerator is not available.",
      "Диагностика: перечислитель устройств недоступен." },
    { Str::LogDiagEndpoints,
      "Diagnostics: endpoints for the {0} flow: {1}.",
      "Диагностика: устройств ({0}): {1}." },
    { Str::LogDiagReportBuilt,
      "Diagnostics: the report text has been built ({0} bytes).",
      "Диагностика: текст отчёта сформирован ({0} байт)." },
    { Str::LogTrayCreateFailed,
      "Could not create the tray icon.",
      "Не удалось создать значок трея." },
    { Str::LogTrayConfigureFailed,
      "Could not configure the tray icon.",
      "Не удалось настроить значок трея." },
    { Str::LogWindowClassFailed,
      "Could not register the main window class.",
      "Не удалось зарегистрировать класс главного окна." },
    { Str::LogWindowCreateFailed,
      "Could not create the main window.",
      "Не удалось создать главное окно." },

    { Str::ErrUnhandledException,
      "Unhandled exception ({0}). REAL will exit.",
      "Необработанное исключение ({0}). REAL завершит работу." },
    { Str::ErrUnhandledExceptionUnknown,
      "Unhandled exception of an unknown type. REAL will exit.",
      "Необработанное исключение неизвестного типа. REAL завершит работу." },
    { Str::ErrAudioEnumeratorFailed,
      "Could not create the audio device enumerator: {0}",
      "Не удалось создать перечислитель аудиоустройств: {0}" },
    { Str::ErrAudioNotificationsFailed,
      "Could not register for endpoint notifications: {0}",
      "Не удалось подписаться на уведомления об устройствах: {0}" },
    { Str::ErrNotInitialised,
      "The audio session has not been initialized.",
      "Аудиосессия не инициализирована." },
    { Str::ErrLowLatency,
      "Could not enable the latency reduction.",
      "Не удалось включить снижение задержки." },
    { Str::WarnNoDefaultDevice,
      "No default audio device is connected.",
      "Аудиоустройство по умолчанию не подключено." },
    { Str::ErrStreamInvalid,
      "The audio stream is no longer valid: {0}",
      "Аудиопоток больше не действителен: {0}" },
    { Str::ErrDefaultEndpointQuery,
      "Could not query the default audio endpoint: {0}",
      "Не удалось опросить устройство по умолчанию: {0}" },
    { Str::ErrDefaultDeviceChanged,
      "The default audio device has changed.",
      "Устройство по умолчанию изменилось." },
    { Str::ErrStreamNotRunning,
      "The audio stream is not running.",
      "Аудиопоток не запущен." },
    { Str::ErrNoAudioClient3,
      "The device does not support IAudioClient3, so small buffers are not available for it (typical for Bluetooth and some virtual/vendor drivers).",
      "Устройство не поддерживает IAudioClient3, поэтому малые буферы для него недоступны (обычно это Bluetooth и некоторые виртуальные драйверы)." },
    { Str::ErrActivateClient,
      "Could not activate the audio client: {0}",
      "Не удалось активировать аудиоклиент: {0}" },
    { Str::ErrMixFormat,
      "Could not read the device mix format: {0}",
      "Не удалось прочитать формат микширования устройства: {0}" },
    { Str::ErrEnginePeriods,
      "Could not query the buffer sizes of the engine: {0}",
      "Не удалось получить размеры буфера аудиодвижка: {0}" },
    { Str::ErrEngineLocked,
      "The audio engine is currently locked by another application: {0}",
      "Аудиодвижок сейчас зафиксирован другим приложением: {0}" },
    { Str::ErrInitStream,
      "Could not initialize the low latency stream: {0}",
      "Не удалось инициализировать поток низкой задержки: {0}" },
    { Str::ErrStartStream,
      "Could not start the audio stream: {0}",
      "Не удалось запустить аудиопоток: {0}" },
    { Str::ErrUrlParse,
      "Could not parse the URL: {0}",
      "Не удалось разобрать адрес: {0}" },
    { Str::ErrResponseTooLarge,
      "The response is too large.",
      "Ответ слишком большой." },
    { Str::ErrRequestCancelled,
      "the request was cancelled",
      "запрос прерван" },

    { Str::ReasonResume,
      "Resume from sleep",
      "Выход из спящего режима" },
    { Str::ReasonUnlock,
      "Session unlock",
      "Разблокировка сеанса" },
    { Str::CliReportWritten,
      "Report written to {0}",
      "Отчёт записан в {0}" },
    { Str::ArgConfigNeedsPath,
      "--config requires a path",
      "Для --config нужен путь" },
    { Str::ArgLogLevelNeedsValue,
      "--log-level requires a value",
      "Для --log-level нужно значение" },
    { Str::ArgLogLevelUnknown,
      "--log-level: unknown level \"{0}\" (allowed: {1}); ignored",
      "--log-level: неизвестный уровень «{0}» (допустимо: {1}); не применяется" },
    { Str::ErrEmptyUrl,
      "Empty URL.",
      "Пустой адрес." },
    { Str::ErrNoUpdateRepository,
      "The update repository is not configured for this build.",
      "Для этой сборки не задан репозиторий обновлений." },
    { Str::ErrGithubUnreachable,
      "Could not reach GitHub: {0}",
      "Не удалось связаться с GitHub: {0}" },
    { Str::ErrGithubRefused,
      "GitHub refused the request (HTTP {0}).",
      "GitHub отклонил запрос (HTTP {0})." },
    { Str::ErrHttpStatus,
      "GitHub returned HTTP {0}.",
      "GitHub вернул HTTP {0}." },
    { Str::ErrGithubUnexpected,
      "Unexpected GitHub response.",
      "Неожиданный ответ GitHub." },
    { Str::ErrNoReleaseVersion,
      "Could not detect the version of the latest release.",
      "Не удалось определить версию последнего релиза." },
    { Str::ErrDeleteLeftover,
      "Could not delete the leftover file from a previous update: {0}",
      "Не удалось удалить оставшийся файл от прошлого обновления: {0}" },
    { Str::DiagTitle,
      "REAL diagnostics",
      "Диагностика REAL" },
    { Str::DiagRule,
      "================",
      "================" },
    { Str::DiagVersion,
      "Version:     {0} ({1})\n",
      "Версия:      {0} ({1})\n" },
    { Str::DiagGenerated,
      "Generated:   {0}\n",
      "Создан:      {0}\n" },
    { Str::DiagWindows,
      "Windows:     {0}\n",
      "Windows:     {0}\n" },
    { Str::DiagScale,
      "DPI:         {0}%\n",
      "Масштаб:     {0}%\n" },
    { Str::DiagCpu,
      "CPU:         logical processors: {0}; reserved for audio: {1}\n",
      "Процессор:   логических ядер: {0}; зарезервировано для звука: {1}\n" },
    { Str::DiagCpuNone,
      "none",
      "нет" },
    { Str::DiagCpuItem,
      "CPU {0}",
      "ЦП {0}" },
    { Str::DiagExecutable,
      "Executable:  {0}\n",
      "Файл:        {0}\n" },
    { Str::DiagSettings,
      "Settings:    {0}\n",
      "Настройки:   {0}\n" },
    // {0} and {1} are the parts of LogAudioSettings: the direction and the
    // buffer.
    { Str::DiagConfig,
      "Config:      {0} devices; {1}\n",
      "Параметры:   устройства {0}; {1}\n" },
    { Str::DiagEnumeratorError,
      "ERROR: the audio device enumerator could not be created ({0}).\n",
      "ОШИБКА: не удалось создать перечислитель аудиоустройств ({0}).\n" },
    { Str::DiagAudiosrvHint,
      "The Windows audio service (Audiosrv) does not seem to be running.\n",
      "Похоже, не запущена служба Windows «Звук» (Audiosrv).\n" },
    { Str::DiagDevicesRender,
      "--- Output devices (playback) ---\n\n",
      "--- Устройства вывода (воспроизведение) ---\n\n" },
    { Str::DiagDevicesCapture,
      "--- Input devices (recording) ---\n\n",
      "--- Устройства ввода (запись) ---\n\n" },
    { Str::DiagNoDevices,
      "No active devices.\n\n",
      "Нет активных устройств.\n\n" },
    { Str::DiagDefaultMark,
      "   [default]",
      "   [по умолчанию]" },
    { Str::DiagCommunicationMark,
      "   [communication]",
      "   [связь]" },
    { Str::DiagDeviceId,
      "    id:       {0}\n",
      "    код:      {0}\n" },
    { Str::DiagDriver,
      "    driver:   {0} {1}\n",
      "    драйвер:  {0} {1}\n" },
    { Str::DiagFormat,
      "    format:   {0} Hz, {1}, {2} bit\n",
      "    формат:   {0} Гц, {1}, {2} бит\n" },
    { Str::DiagPeriods,
      "    buffer:   {0}\n",
      "    буфер:    {0}\n" },
    { Str::DiagUnknown,
      "unknown",
      "неизвестно" },
    { Str::DiagResult,
      "    result:   {0}\n",
      "    итог:     {0}\n" },
    { Str::DiagSmallBuffer,
      "small buffers are available, a buffer of {0} can be requested",
      "малые буферы доступны, можно запросить буфер {0}" },
    { Str::DiagNoGain,
      "the driver offers nothing smaller than its default buffer, so REAL cannot lower the latency here",
      "драйвер не предлагает буфер меньше стандартного, снизить задержку здесь нельзя" },
    { Str::DiagNoAudioClient3,
      "IAudioClient3 is not available",
      "IAudioClient3 недоступен" },
    { Str::DiagNotesHeader,
      "--- Notes ---\n\n",
      "--- Примечания ---\n\n" },
    { Str::DiagNotes,
      "A device is suitable for the latency reduction when its minimum buffer is smaller than its default one (see 'result'). Typical exceptions: Bluetooth endpoints (10 ms by design), HDMI/DisplayPort receivers, some vendor drivers (Realtek, Nahimic, ACX) and virtual devices.\n"
      "The base step is the amount by which the engine can change the size of its buffer: any value between the minimum and the maximum with that step is allowed, and these are the values a fixed buffer of the settings accepts. The step itself is not a buffer size, so a value like \"step of 1 frame\" cannot be requested; REAL never asks for a buffer below the minimum.\n"
      "The small buffer is taken on both default devices of the chosen direction: the usual default device and the default communication device (Settings - System - Sound).\n"
      "A processor reserved for audio (the CPU line above) is kept by Windows for the audio engine while a stream with a small buffer runs: other programs get it last, so monitoring tools may show it underused. This is the low latency mode of Windows itself, not a fault of REAL.\n",
      "Устройство подходит для снижения задержки, если его минимальный буфер меньше стандартного (см. «итог»). Обычные исключения: Bluetooth (10 мс по замыслу), приёмники HDMI/DisplayPort, некоторые драйверы производителей (Realtek, Nahimic, ACX) и виртуальные устройства.\n"
      "Базовый шаг — это ступень, с которой движок меняет размер буфера: допустимы значения от минимального до максимального с этим шагом, их и принимает фиксированный буфер в настройках. Сам шаг размером буфера не является, поэтому значение вида «шаг 1 фрейм» использовать нельзя — программа не запрашивает буфер меньше минимального.\n"
      "Малый буфер берётся на обоих устройствах по умолчанию выбранного направления: на обычном устройстве по умолчанию и на устройстве связи по умолчанию («Параметры → Система → Звук»).\n"
      "Ядро, зарезервированное для звука (строка «Процессор» выше), Windows держит для аудиодвижка, пока работает поток с малым буфером: другие программы получают его в последнюю очередь, поэтому в мониторинге оно может выглядеть недогруженным. Это особый режим низкой задержки самой Windows, а не ошибка REAL.\n" },

    { Str::DiagPeriodsNoClient3,
      "device buffer {0} ({1})",
      "буфер устройства {0} ({1})" },
    { Str::DiagPeriodDefault,
      "default {0} ({1})",
      "стандартный {0} ({1})" },
    { Str::DiagPeriodMinimum,
      "minimum {0} ({1})",
      "минимальный {0} ({1})" },
    { Str::DiagPeriodFundamental,
      "base step {0} ({1})",
      "базовый шаг {0} ({1})" },
    { Str::DiagPeriodMaximum,
      "maximum {0} ({1})",
      "максимальный {0} ({1})" },
    { Str::DiagNoAudioClient3Detail,
      "the driver does not expose IAudioClient3, so small buffers are not available for this device (typical for Bluetooth, HDMI/DisplayPort receivers and some virtual drivers)",
      "драйвер не предоставляет IAudioClient3, поэтому малые буферы для этого устройства недоступны (обычно это Bluetooth, приёмники HDMI/DisplayPort и некоторые виртуальные драйверы)" },
    { Str::DiagMixFormatFailed,
      "the mix format could not be read",
      "не удалось прочитать формат микширования" },
    { Str::DiagEnginePeriodsFailed,
      "the buffer sizes of the engine could not be queried: {0}",
      "не удалось получить размеры буфера аудиодвижка: {0}" },
    { Str::DiagActivateFailed,
      "the audio client could not be created: {0}",
      "не удалось создать аудиоклиент: {0}" },

    { Str::SettingsPrefix, "Settings:", "Настройки:" },
    { Str::CfgErrNotObject,
      "the file must contain a JSON object",
      "файл должен содержать объект JSON" },
    { Str::CfgErrParse,
      "the file could not be parsed: {0}",
      "файл не удалось разобрать: {0}" },
    { Str::CfgErrRead,
      "the file exists but could not be read (it may be locked by another program)",
      "файл есть, но прочитать его не удалось (возможно, он занят другой программой)" },
    { Str::CfgErrTooLarge,
      "the file is larger than 1 MB, it is not a settings file",
      "файл больше 1 МБ — это не файл настроек" },
    // A value of the file that cannot be used: {0} is the key, the last
    // placeholder the value used instead (the one in use when the file is
    // read again, the default at startup).
    { Str::CfgWarnString,
      "{0}: expected a string; using {1}",
      "{0}: ожидалась строка; используется {1}" },
    { Str::CfgWarnBool,
      "{0}: expected true or false; using {1}",
      "{0}: ожидалось true или false; используется {1}" },
    { Str::CfgWarnInteger,
      "{0}: expected a whole number; using {1}",
      "{0}: ожидалось целое число; используется {1}" },
    { Str::CfgWarnRange,
      "{0}: {1} is out of the range {2}–{3}; using {4}",
      "{0}: значение {1} вне диапазона {2}–{3}; используется {4}" },
    { Str::CfgWarnUnknownKey,
      "{0}: unknown option, ignored",
      "{0}: неизвестный параметр, игнорируется" },
    { Str::CfgWarnUnknownValue,
      "{0}: unknown value \"{1}\" (allowed: {2}); using {3}",
      "{0}: неизвестное значение «{1}» (допустимо: {2}); используется {3}" },
    { Str::CfgWarnFixedBufferZero,
      "{0}: \"fixed\" needs a size above 0; using the {1}",
      "{0}: для «fixed» нужен размер больше 0; используется {1}" },

    // Command line help
    { Str::HelpText,
      "{0} - {1} {2}\n"
      "\n"
      "Usage: REAL.exe [options]\n"
      "\n"
      "  (no options)          Start with the main window (and the tray icon when it is on)\n"
      "  --tray                Start minimized to the system tray\n"
      "  --no-tray             Start with the main window visible\n"
      "  --config <path>       Use the given settings file instead of real.settings.json\n"
      "  --no-config           Ignore the settings file, use the built-in defaults\n"
      "  --log-level <level>   off | error | warn | info | debug | trace\n"
      "\n"
      "Commands for a running instance (the command is passed to it and this process exits):\n"
      "  --reinit              Restart: re-create the audio streams, the latency reduction is enabled\n"
      "  --enable              Enable the latency reduction\n"
      "  --disable             Disable the latency reduction (the engine returns to its default)\n"
      "  --exit                Close the running instance\n"
      "\n"
      "Diagnostics:\n"
      "  --diagnose            Write a report about the audio devices to REAL-diagnostics.txt\n"
      "\n"
      "  --help, -h, /?        Show this help\n"
      "  --version             Show the version\n"
      "\n"
      "Settings: \"Options - Settings\" in the menu bar, \"Settings\" in the tray menu, or real.settings.json next to REAL.exe.\n"
      "Every parameter is explained by a comment inside that file, see also docs/CONFIG.md.\n",
      "{0} - {1} {2}\n"
      "\n"
      "Использование: REAL.exe [ключи]\n"
      "\n"
      "  (без ключей)          запуск с окном (и значком в трее, если он включён)\n"
      "  --tray                стартовать свёрнутым в системный трей\n"
      "  --no-tray             стартовать с видимым окном\n"
      "  --config <путь>       использовать другой файл настроек вместо real.settings.json\n"
      "  --no-config           не читать файл настроек, взять встроенные значения\n"
      "  --log-level <уровень> off | error | warn | info | debug | trace\n"
      "\n"
      "Команды для работающей копии (передаются ей, этот процесс завершается):\n"
      "  --reinit              перезапустить: заново создать аудиопотоки, снижение задержки включается\n"
      "  --enable              включить снижение задержки\n"
      "  --disable             выключить снижение задержки (движок вернётся к 10 мс)\n"
      "  --exit                закрыть работающую копию\n"
      "\n"
      "Диагностика:\n"
      "  --diagnose            записать отчёт об аудиоустройствах в REAL-diagnostics.txt\n"
      "\n"
      "  --help, -h, /?        показать эту справку\n"
      "  --version             показать версию\n"
      "\n"
      "Настройки: «Опции → Настройки» в строке меню, «Настройки» в меню значка или файл real.settings.json рядом с REAL.exe.\n"
      "У каждого параметра есть комментарий прямо в файле, подробнее - docs/CONFIG.md.\n" },

    // Dialogs
    { Str::AboutTitle, "About REAL", "О программе REAL" },
    { Str::AboutUsage, "Instructions:", "Инструкция:" },
    { Str::AboutConfig, "Parameters:", "Параметры:" },
    { Str::AboutProject, "Project:", "Проект:" },
    { Str::AboutSettings, "Settings:", "Настройки:" },
    // The note of the About window: one sentence, in the language of the
    // interface; "{}" is the version and is drawn by the window itself.
    { Str::AboutText,
      "While REAL is running, Windows uses the smallest audio buffer\n"
      "supported by the device driver.",
      "Пока REAL запущен, Windows использует минимальный аудиобуфер,\n"
      "поддерживаемый драйвером устройства." },

    { Str::DiagnosticsWriteFailed, "The diagnostics report could not be written to a file.",
                                   "Не удалось записать отчёт диагностики в файл." },

    // Window and log: operations
    { Str::OpStarted, "REAL {} started", "REAL {} запущен" },
    { Str::OpSettingsFile, "Settings: {}", "Настройки: {}" },
    { Str::OpSettingsCreated, "Settings file created: {}", "Создан файл настроек: {}" },
    { Str::OpCommentsRewritten, "Settings file rewritten with comments in {} (every value is kept)",
                                "Файл настроек перезаписан с комментариями на языке {} (значения сохранены)" },
    { Str::OpSettingsUpgraded, "Settings file rewritten in the current layout (every value is kept)",
                               "Файл настроек перезаписан в текущем оформлении (значения сохранены)" },
    { Str::OpAlreadyRunning, "REAL is already running; the command was passed to it",
                             "REAL уже запущен, команда передана ему" },
    { Str::OpInstanceNotResponding, "REAL is already running but does not respond.",
                                    "REAL уже запущен, но не отвечает." },
    { Str::OpLogOpened, "Log opened: {}", "Открыт журнал: {}" },
    { Str::OpApplied, "Latency reduction is active: {}", "Снижение задержки активно: {}" },
    { Str::OpDriverMinimum, "The driver already keeps the smallest buffer, nothing has to be held open: {}",
                           "Драйвер уже отдаёт минимальный буфер, держать поток открытым не нужно: {}" },
    { Str::OpEnabled, "Latency reduction enabled", "Снижение задержки включено" },
    { Str::OpDisabled, "Latency reduction disabled", "Снижение задержки выключено" },
    { Str::OpDeviceChanged, "Audio device changed, applying again", "Аудиоустройство изменилось, применяю заново" },
    { Str::OpDeviceNotReady, "Device is not ready, retrying in {} s", "Устройство не готово, повтор через {} с" },
    { Str::OpGaveUp, "The device did not respond within {} s, latency reduction is off",
                     "Устройство не ответило за {} с, снижение задержки выключено" },
    { Str::OpDiagnostics, "Diagnostics report: {}", "Отчёт диагностики: {}" },
    { Str::OpDiagnosticsFailed, "Could not write the diagnostics report", "Не удалось записать отчёт диагностики" },
    { Str::OpAutostart, "Autostart: {}", "Автозапуск: {}" },
    { Str::OpUpdateChecking, "Checking for updates...", "Проверяю обновления..." },
    { Str::OpExiting, "Exiting", "Выход" },
    { Str::OpTrayUnavailable, "The tray icon is unavailable, the window stays visible",
                              "Значок в трее недоступен, окно остаётся видимым" },
    // {} is the reason (ReasonResume, ReasonUnlock).
    { Str::OpResumeApply, "{}, applying again", "{}, применяю заново" },
    { Str::OpDiagHint, "The device does not answer. Check the available devices by running the diagnostics.",
                       "Устройство не отвечает. Проверьте доступные устройства, запустив диагностику." },
    { Str::ValueOn, "on", "вкл" },
    { Str::ValueOff, "off", "выкл" },

    // Settings window
    { Str::SettingsWindowTitle, "REAL settings", "Настройки REAL" },
    { Str::SettingsSave, "Save", "Сохранить" },
    { Str::SettingsCancel, "Cancel", "Отмена" },
    { Str::SettingsOpenFile, "Open", "Открыть" },
    { Str::SettingsReload, "Reload", "Перечитать" },
    { Str::SettingsOpenFileFailed, "Could not open the settings file.", "Не удалось открыть файл настроек." },
    { Str::SettingsReloadFailed,
      "Could not read the settings file; the values in the window are left as they are.\n{0}",
      "Не удалось прочитать файл настроек; значения в окне оставлены как есть.\n{0}" },
    // Followed by one line per value that could not be used.
    { Str::SettingsReloadWarnings,
      "The file has been read again, but some values could not be used:",
      "Файл перечитан, но некоторые значения не подошли:" },
    // A click on the path of the settings file copies its folder.
    { Str::SettingsPathCopied, "Copied: {0}", "Скопировано: {0}" },
    { Str::SettingsPathCopyFailed, "Could not copy the path.", "Не удалось скопировать путь." },
    { Str::SettingsTabWindow, "Window", "Окно" },
    { Str::SettingsTabAudio, "Audio", "Звук" },
    { Str::SettingsTabOther, "Other", "Прочее" },
    { Str::SettingsHeaderApplication, "Application", "Приложение" },
    { Str::SettingsHeaderTray, "Tray icon", "Значок в трее" },
    { Str::SettingsHeaderNotifications, "Notifications", "Уведомления" },
    { Str::SettingsHeaderAudio, "Audio streams", "Аудиопотоки" },
    { Str::SettingsHeaderReinit, "Automatic restart", "Автоматический перезапуск" },
    { Str::SettingsHeaderPerformance, "REAL process", "Процесс REAL" },
    { Str::SettingsHeaderUpdates, "Updates", "Обновления" },
    { Str::SettingsHeaderLog, "Log", "Журнал" },
    { Str::SettingsHeaderSettingsFile, "Settings file", "Файл настроек" },
    { Str::SettingsLanguage, "Language:", "Язык:" },
    { Str::SettingsLanguageAuto, "As in Windows", "Как в Windows" },
    // The names of the languages are written in the languages themselves and
    // do not change with the language of the interface.
    { Str::SettingsLanguageEnglish, "English", "English" },
    { Str::SettingsLanguageRussian, "Русский", "Русский" },
    { Str::SettingsStartWithWindows, "Start with Windows", "Запускать с Windows" },
    { Str::SettingsStartMinimized, "Start minimized to tray", "Запускать свёрнутым в трей" },
    { Str::SettingsMinimizeToTray, "Minimize to tray", "Сворачивать в трей" },
    { Str::SettingsCloseAction, "Close button", "Кнопка закрытия" },
    { Str::SettingsCloseMinimize, "Minimize to tray", "Свернуть в трей" },
    { Str::SettingsCloseExit, "Exit", "Завершить программу" },
    { Str::SettingsTrayEnabled, "Show the tray icon", "Показывать значок в трее" },
    { Str::SettingsTrayMenuCaption, "Icon menu items:", "Пункты меню значка:" },
    { Str::SettingsNotifyError, "Notify about errors", "Уведомлять об ошибках" },
    { Str::SettingsNotifyDeviceChange, "Notify about a device change", "Уведомлять о смене устройства" },
    { Str::SettingsNotifyStateChange, "Notify when switched on or off", "Уведомлять о включении и выключении" },
    { Str::SettingsDataFlow, "Devices", "Устройства" },
    { Str::SettingsFlowRender, "Playback", "Воспроизведение" },
    { Str::SettingsFlowCapture, "Recording", "Запись" },
    { Str::SettingsFlowBoth, "Playback and recording", "Воспроизведение и запись" },
    { Str::SettingsBuffer, "Buffer", "Буфер" },
    { Str::SettingsBufferMinimum, "Minimum", "Минимальный" },
    { Str::SettingsBufferFixed, "Fixed", "Фиксированный" },
    { Str::SettingsFixedBufferFrames, "Fixed buffer, frames", "Фиксированный буфер, фреймы" },
    // The range of the buffer of the device under the field: {0}-{1} frames,
    // {2} the step, {3}-{4} the same range in milliseconds.
    { Str::SettingsBufferHint, "Allowed: {0}–{1}, step {2} ({3}–{4} ms)", "Допустимо: {0}–{1}, шаг {2} ({3}–{4} мс)" },
    // The balloon of a field whose value was not accepted. The fixed buffer:
    // {0} is the device, {1}-{2} its range, {3} the step, then the nearest
    // values it accepts.
    { Str::SettingsBufferAccepted,
      "The device \"{0}\" accepts {1} to {2} in steps of {3}.",
      "Устройство «{0}» принимает от {1} до {2} с шагом {3}." },
    { Str::SettingsBufferOutOfRange,
      "The device \"{0}\" accepts {1} to {2} in steps of {3}; the nearest value: {4}.",
      "Устройство «{0}» принимает от {1} до {2} с шагом {3}; ближайшее подходящее значение: {4}." },
    { Str::SettingsBufferNotOnStep,
      "The device \"{0}\" accepts {1} to {2} in steps of {3}; the nearest values: {4} and {5}.",
      "Устройство «{0}» принимает от {1} до {2} с шагом {3}; ближайшие подходящие значения: {4} и {5}." },
    // The other numbers, the path of the log, and the value put back.
    { Str::SettingsAllowedRange, "Allowed: {0}–{1}.", "Допустимо: {0}–{1}." },
    { Str::SettingsAllowedPath,
      "The path cannot be empty, and the characters < > \" | ? * are not allowed in it.",
      "Путь не может быть пустым, а символы < > \" | ? * в нём недопустимы." },
    { Str::SettingsValueRestored, "Restored: {0}.", "Возвращено: {0}." },
    // The descriptions that show when the mouse rests on a setting.
    { Str::SettingsHintStartWithWindows,
      "REAL starts automatically when you sign in to Windows (an entry in the startup programs of the current user).",
      "REAL запускается автоматически при входе в Windows (запись в автозагрузке текущего пользователя)." },
    { Str::SettingsHintStartMinimized,
      "At startup the window stays closed: REAL works in the tray right away. A click on the icon opens the window.",
      "При запуске окно не открывается: REAL сразу работает в трее. Окно открывается щелчком по значку." },
    { Str::SettingsHintMinimizeToTray,
      "The Minimize button hides the window in the tray instead of the taskbar.",
      "Кнопка «Свернуть» прячет окно в трей, а не на панель задач." },
    { Str::SettingsHintCloseAction,
      "What the close button of the window does: hides the window in the tray or exits REAL.",
      "Что делает крестик окна: прячет окно в трей или завершает REAL." },
    { Str::SettingsHintDataFlow,
      "Which default devices get the lower latency: playback, recording or both. In each direction every default device is handled, the communication device included.",
      "Для каких устройств по умолчанию снижать задержку: воспроизведения, записи или тех и других. В каждом направлении обрабатываются все устройства по умолчанию, включая устройство связи." },
    { Str::SettingsHintBuffer,
      "Minimum: the smallest buffer the driver supports, for the lowest latency. Fixed: a size of your choice, with a higher latency and less load on the processor.",
      "Минимальный — самый маленький буфер, который поддерживает драйвер: наименьшая задержка. Фиксированный — заданный размер: задержка больше, нагрузка на процессор меньше." },
    { Str::SettingsHintFixedBuffer,
      "The size of the buffer for \"Fixed\". A frame is one sample of every channel: at 48,000 Hz, 480 frames are 10 ms. The range of the device is shown under the name of the field.",
      "Размер буфера для варианта «Фиксированный». Фрейм — один отсчёт звука по всем каналам: при 48 000 Гц 480 фреймов — это 10 мс. Диапазон устройства указан под названием поля." },
    { Str::SettingsHintProcessPriority,
      "The priority of the REAL process itself. It does not affect the sound: the sound is processed by the Windows Audio service.",
      "Приоритет самого процесса REAL. На звук не влияет: звук обрабатывает служба Windows Audio." },
    { Str::SettingsHintLogFilePath,
      "The log file: a path relative to the folder of REAL.exe or a full path.",
      "Файл журнала: путь относительно папки REAL.exe или полный путь." },
    { Str::SettingsHintLogMaxFileSize,
      "When the log file reaches this size, a new file is started.",
      "Когда файл журнала дорастает до этого размера, начинается новый." },
    { Str::SettingsHintLogMaxFiles,
      "How many log files to keep; the oldest one is deleted when a new one starts.",
      "Сколько файлов журнала хранить; самый старый удаляется, когда начинается новый." },
    { Str::SettingsHintSettingsPath,
      "A click copies the path of the folder with the file.",
      "Щелчок копирует путь к папке с файлом." },
    { Str::SettingsReinitDeviceChanged, "Default device change", "Смена устройства по умолчанию" },
    { Str::SettingsReinitDeviceState, "Device connected or disconnected", "Подключение и отключение устройства" },
    { Str::SettingsReinitDeviceAdded, "New device", "Появление нового устройства" },
    { Str::SettingsReinitDeviceRemoved, "Device removal", "Удаление устройства" },
    { Str::SettingsReinitResume, "Resume from sleep", "Выход из спящего режима" },
    { Str::SettingsReinitUnlock, "Session unlock", "Разблокировка сеанса" },
    { Str::SettingsReinitFailureTimeout, "Wait for the device, ms", "Ждать ответа устройства, мс" },
    { Str::SettingsReinitDebounce, "Pause before restarting, ms", "Пауза перед перезапуском, мс" },
    { Str::SettingsProcessPriority, "Process priority", "Приоритет процесса" },
    { Str::SettingsPriorityNormal, "Normal", "Обычный" },
    { Str::SettingsPriorityBelowNormal, "Below normal", "Ниже среднего" },
    { Str::SettingsPriorityIdle, "Low", "Низкий" },
    { Str::SettingsCheckOnStartup, "Check at startup", "Проверять при запуске" },
    { Str::SettingsLogLevel, "Level", "Уровень" },
    // The value of the file stays in parentheses: the list and the settings
    // file read the same, and the reference names the value.
    { Str::SettingsLogLevelOff, "Off (off)", "Выключен (off)" },
    { Str::SettingsLogLevelError, "Errors (error)", "Ошибки (error)" },
    { Str::SettingsLogLevelWarn, "Warnings (warn)", "Предупреждения (warn)" },
    { Str::SettingsLogLevelInfo, "Main events (info)", "Основное (info)" },
    { Str::SettingsLogLevelDebug, "Detailed (debug)", "Подробно (debug)" },
    { Str::SettingsLogLevelTrace, "Everything (trace)", "Всё (trace)" },
    { Str::SettingsLogFilePath, "File path", "Путь к файлу" },
    { Str::SettingsLogMaxFileSize, "File size, MB", "Размер файла, МБ" },
    { Str::SettingsLogMaxFiles, "Files to keep", "Сколько файлов хранить" },

    // Settings file comments
    { Str::CfgFileHeader,
      "Settings of REAL. The file is created automatically and is read when the program starts or when the settings are reloaded. Comments can be removed.",
      "Настройки REAL. Файл создаётся автоматически и читается при запуске и перезагрузке настроек. Комментарии можно удалять." },
    { Str::CfgApplicationSection, "Window, start and autostart.", "Окно, запуск и автозапуск приложения." },
    { Str::CfgStartMinimizedToTray, "true - start minimized in the tray (same as the --tray key); needs the tray icon.",
                                    "true - стартовать сразу свёрнутым в трей (то же, что ключ запуска --tray); нужен значок в трее." },
    { Str::CfgMinimizeToTray, "true - the Minimize button hides the window to the tray, not to the taskbar; needs the tray icon.",
                              "true - кнопка \"Свернуть\" прячет окно в трей, а не в панель задач; нужен значок в трее." },
    { Str::CfgCloseButtonAction, "What the close button does: \"minimize\" (to the tray) or \"exit\" (quit); without the tray icon it always quits.",
                                 "Что делает крестик окна: \"minimize\" (в трей) или \"exit\" (завершить программу); без значка в трее - всегда завершает." },
    { Str::CfgStartWithWindows, "true - start automatically after logon (HKCU Run key).",
                                "true - автозапуск при входе в систему (запись REAL в HKCU Run)." },
    { Str::CfgLanguage, "Language of the interface, the log and these comments: \"auto\" (Windows), \"en\", \"ru\". The comments follow the language: at once when it is changed in the settings window, on the next start after an edit of this file; the values stay.",
                        "Язык интерфейса, журнала и этих комментариев: \"auto\" (язык Windows), \"en\", \"ru\". Комментарии переписываются на новом языке: сразу при смене в окне настроек, при правке этого файла - при следующем запуске; значения сохраняются." },
    { Str::CfgTraySection, "Icon in the notification area.", "Значок в системном трее." },
    { Str::CfgTrayEnabled, "true - show the tray icon (left click shows the window, right click opens the menu).",
                           "true - показывать значок в трее (левый клик - окно, правый - меню)." },
    { Str::CfgNotificationsSection, "Balloon notifications: shown only while the window of the program is hidden or minimized.",
                                    "Всплывающие уведомления: показываются, только когда окно программы скрыто или свёрнуто." },
    { Str::CfgNotifyOnError, "true - notify about failures (one notification per outage, not per retry).",
                             "true - уведомлять об ошибках (одно уведомление на сбой, а не на каждую попытку)." },
    { Str::CfgNotifyOnDeviceChange, "true - notify about a device change.",
                                    "true - уведомлять о смене устройства." },
    { Str::CfgNotifyOnStateChange, "true - notify when the latency reduction is switched on or off (and about a manual restart).",
                                   "true - уведомлять о включении и выключении снижения задержки (и о ручном перезапуске)." },
    { Str::CfgMenuSection, "Tray menu items that can be hidden (false hides an item); the status line, \"Settings\" and \"Exit\" are always there.",
                           "Пункты меню значка, которые можно скрыть (false - пункт скрыт); строка состояния, \"Настройки\" и \"Выход\" есть всегда." },
    { Str::CfgMenuToggle, "The \"REAL is running\" item: switches the latency reduction on and off.",
                          "Пункт \"REAL запущен\": включает и выключает снижение задержки." },
    { Str::CfgMenuReinitialize, "The \"Restart\" item: the audio streams are created again.",
                                "Пункт \"Перезапустить\": аудиопотоки создаются заново." },
    { Str::CfgMenuLog, "The \"Log file\" item: opens the log.", "Пункт \"Файл журнала\": открывает журнал." },
    { Str::CfgMenuDiagnostics, "The \"Diagnostics\" item: a report about the audio devices.",
                               "Пункт \"Диагностика\": отчёт об аудиоустройствах." },
    { Str::CfgAudioSection, "How REAL talks to the audio engine.", "Параметры работы с аудиодвижком." },
    { Str::CfgDataFlow, "Devices to process: \"render\" (playback), \"capture\" (recording), \"both\" (playback and recording).",
                        "Какие устройства обрабатывать: \"render\" (воспроизведение), \"capture\" (запись), \"both\" (воспроизведение и запись)." },
    { Str::CfgBuffer, "Buffer: \"min\" - the smallest one the device driver supports; \"fixed\" - a fixed one, its size is in fixedBufferFrames.",
                      "Буфер: \"min\" - минимальный, который поддерживает драйвер устройства; \"fixed\" - фиксированный, размер в fixedBufferFrames." },
    { Str::CfgFixedBufferFrames, "Size of the fixed buffer in frames, for \"fixed\" only: from the minimum to the maximum buffer of the device in its base step (see the diagnostics report); a value that does not fit is adjusted, and the journal says so. 0 - not set.",
                                 "Размер фиксированного буфера во фреймах, только для \"fixed\": от минимального до максимального буфера устройства с его базовым шагом (см. отчёт диагностики); неподходящее значение подгоняется, о чём пишется в журнал. 0 - не задан." },
    { Str::CfgReinitSection, "When to restart automatically (the audio streams are created again).",
                             "Когда перезапускать автоматически (аудиопотоки создаются заново)." },
    { Str::CfgReinitDeviceChanged, "Default device change (the main case).",
                                   "Смена устройства по умолчанию (основной случай)." },
    { Str::CfgReinitDeviceState, "A device connected or disconnected: it became active or inactive (headphones, USB).",
                                 "Подключение и отключение устройства: оно стало активным или неактивным (наушники, USB)." },
    { Str::CfgReinitDeviceAdded, "A new device that did not exist in the system before (a known device plugged back in belongs to deviceStateChanged).",
                                 "Появление нового устройства, которого раньше не было в системе (знакомое устройство при повторном подключении относится к deviceStateChanged)." },
    { Str::CfgReinitDeviceRemoved, "A device removed from the system.", "Удаление устройства из системы." },
    { Str::CfgReinitResume, "Resume from sleep or Modern Standby.", "Выход из сна или Modern Standby." },
    { Str::CfgReinitUnlock, "Session unlock (Win+L).", "Разблокировка сеанса (Win+L)." },
    // The ranges of the numbers ({0}-{1}) come from Settings.h.
    { Str::CfgReinitFailureTimeout, "How long to keep retrying before the latency reduction is switched off and polling stops (ms, {0}–{1}).",
                                    "Сколько миллисекунд повторять попытки, прежде чем выключить снижение задержки и прекратить опрос ({0}–{1})." },
    { Str::CfgReinitDebounce, "Pause before restarting: Windows sends a burst of events (ms, {0}–{1}).",
                              "Пауза перед перезапуском: Windows присылает пачку событий подряд (мс, {0}–{1})." },
    { Str::CfgPerformanceSection, "The REAL process itself. It does not affect the sound: the sound is processed by the Windows Audio service.",
                                  "Сам процесс REAL. На звук не влияет: звук обрабатывает служба Windows Audio." },
    { Str::CfgProcessPriority, "Process priority as in the Task Manager: \"normal\", \"belowNormal\" (below normal) or \"idle\" (low).",
                               "Приоритет процесса как в диспетчере задач: \"normal\" (обычный), \"belowNormal\" (ниже среднего), \"idle\" (низкий)." },
    { Str::CfgUpdatesSection, "Update check: once at startup at most, never while REAL is running.",
                              "Проверка обновлений: не больше одного раза при запуске, во время работы запросов нет." },
    { Str::CfgUpdatesCheckOnStartup, "true - check once at startup; false - no network request at all.",
                                     "true - один раз проверить обновления при запуске; false - ни одного сетевого запроса." },
    { Str::CfgLoggingSection, "Log file. The window of the program always shows the operations at the info level.",
                              "Файл журнала. В окне программы всегда видны основные операции уровня info." },
    { Str::CfgLoggingLevel, "\"off\" - the file is not written at all; \"error\", \"warn\", \"info\", \"debug\", \"trace\" - from the shortest file to the most detailed one.",
                            "\"off\" - файл не ведётся совсем; \"error\", \"warn\", \"info\", \"debug\", \"trace\" - от самого краткого файла к самому подробному." },
    { Str::CfgLoggingFilePath, "Path of the log file: relative to the REAL.exe directory or absolute.",
                               "Путь к файлу журнала: относительно каталога REAL.exe или абсолютный." },
    { Str::CfgLoggingMaxFileSize, "Size of one log file before the next one is started (MB, {0}–{1}).",
                                  "Размер одного файла журнала, после которого начинается следующий (МБ, {0}–{1})." },
    { Str::CfgLoggingMaxFiles, "How many log files to keep ({0}–{1}).", "Сколько файлов журнала хранить ({0}–{1})." },
};

constexpr size_t TABLE_SIZE = sizeof(TABLE) / sizeof(TABLE[0]);

// Every identifier has to have a row, and no row may belong to nothing: the
// list and the table cannot drift apart without the build failing.
static_assert(
    TABLE_SIZE == static_cast<size_t>(Str::Count),
    "Every Str identifier needs a row in TABLE and the other way round (see Lang.h).");

// Both texts of every identifier, indexed by Str and built once in
// Initialize() (the first call comes from the main thread, before any other
// thread is started).
const char* g_english[static_cast<size_t>(Str::Count)] = {};
const char* g_russian[static_cast<size_t>(Str::Count)] = {};
bool g_initialized = false;

// The thread of the update check reads the language too, while the settings
// window may change it.
std::atomic<Language> g_language{ Language::English };

void Initialize() {
    if (g_initialized) {
        return;
    }

    for (size_t i = 0; i < TABLE_SIZE; ++i) {
        const size_t index = static_cast<size_t>(TABLE[i].id);
        if (index < static_cast<size_t>(Str::Count)) {
            g_english[index] = TABLE[i].english;
            g_russian[index] = TABLE[i].russian;
        }
    }

    g_initialized = true;
}

const char* Pick(Str id, Language language) {
    Initialize();

    const size_t index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(Str::Count) || g_english[index] == nullptr) {
        return "?";
    }

    return language == Language::Russian ? g_russian[index] : g_english[index];
}

}

Language miniant::Lang::Detect() {
    const LANGID language = ::GetUserDefaultUILanguage();
    const WORD primary = PRIMARYLANGID(language);

    if (primary == LANG_RUSSIAN || primary == LANG_UKRAINIAN || primary == LANG_BELARUSIAN) {
        return Language::Russian;
    }

    return Language::English;
}

Language miniant::Lang::FromCode(const std::string& code) {
    // Accept "auto", "en", "english", "ru", "russian" in any case. Anything
    // else is "auto", as in the settings window (the reader of the settings
    // file warns about such a value).
    std::string value;
    value.reserve(code.size());
    for (char c : code) {
        value.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (value.empty() || value == "auto" || value == "system" || value == "default") {
        return Detect();
    }

    if (value == "ru" || value == "rus" || value == "russian") {
        return Language::Russian;
    }

    if (value == "en" || value == "english") {
        return Language::English;
    }

    return Detect();
}

const char* miniant::Lang::Code(Language language) {
    return language == Language::Russian ? "ru" : "en";
}

void miniant::Lang::Set(Language language) {
    Initialize();
    g_language = language;
}

Language miniant::Lang::Current() {
    Initialize();
    return g_language;
}

const char* miniant::Lang::Utf8(Str id) {
    return Pick(id, g_language.load());
}

std::wstring miniant::Lang::Wide(Str id) {
    return miniant::Text::ToWide(Pick(id, g_language.load()));
}

const char* miniant::Lang::Utf8(Str id, Language language) {
    return Pick(id, language);
}

std::wstring miniant::Lang::Wide(Str id, Language language) {
    return miniant::Text::ToWide(Pick(id, language));
}

namespace {

// The form of a Russian noun after a number: 1 фрейм, 2 фрейма, 5 фреймов,
// 11 фреймов, 21 фрейм, 1344 фрейма.
const char* RussianForm(unsigned int count, const char* one, const char* few, const char* many) {
    const unsigned int lastTwo = count % 100;
    const unsigned int last = count % 10;

    if (lastTwo >= 11 && lastTwo <= 14) {
        return many;
    }

    if (last == 1) {
        return one;
    }

    if (last >= 2 && last <= 4) {
        return few;
    }

    return many;
}

std::string Counted(
    Language language,
    unsigned int count,
    const char* englishOne,
    const char* englishMany,
    const char* russianOne,
    const char* russianFew,
    const char* russianMany) {
    const char* noun = language == Language::Russian
        ? RussianForm(count, russianOne, russianFew, russianMany)
        : (count == 1 ? englishOne : englishMany);

    return std::to_string(count) + " " + noun;
}

}

std::string miniant::Lang::Frames(unsigned int count) {
    return Frames(count, Current());
}

std::string miniant::Lang::Frames(unsigned int count, Language language) {
    return Counted(language, count, "frame", "frames", "фрейм", "фрейма", "фреймов");
}

std::string miniant::Lang::Channels(unsigned int count) {
    return Counted(Current(), count, "channel", "channels", "канал", "канала", "каналов");
}

std::string miniant::Lang::Decimal(double value) {
    return Decimal(value, Current());
}

std::string miniant::Lang::Decimal(double value, Language language) {
    std::string text = fmt::format("{:.2f}", value);

    // A decimal comma in Russian: "7,00 мс", not "7.00 мс".
    if (language == Language::Russian) {
        std::replace(text.begin(), text.end(), '.', ',');
    }

    return text;
}

std::string miniant::Lang::Milliseconds(double value) {
    return Decimal(value) + (Current() == Language::Russian ? " мс" : " ms");
}
