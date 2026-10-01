#include "Lang.h"

#include "Text.h"

#include <Windows.h>

#include <spdlog/fmt/fmt.h>

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
    // Main window
    { Str::WindowTitle, "REAL - REduce Audio Latency", "REAL - REduce Audio Latency" },
    { Str::StatusStarting, "Starting...", "Запуск..." },
    { Str::ButtonReinitialize, "Restart", "Перезапустить" },
    { Str::ButtonOptions, "Options", "Опции" },
    { Str::ButtonFileMenu, "File", "Файл" },
    { Str::ButtonAbout, "About", "О программе" },
    { Str::ButtonDiagnostics, "Diagnostics", "Диагностика" },
    { Str::ButtonOpenLog, "Open the log", "Открыть журнал" },
    { Str::ButtonExit, "Exit", "Выход" },

    // Tray menu
    { Str::TrayToggleEnabled, "Latency reduction enabled", "Снижение задержки включено" },
    { Str::TrayReinitialize, "Restart now", "Перезапустить" },
    { Str::TrayLog, "Log file", "Файл журнала" },
    { Str::TrayDiagnostics, "Diagnostics", "Диагностика" },
    { Str::TrayStartWithWindows, "Start with Windows", "Запускать с Windows" },
    { Str::TrayAbout, "About REAL", "О программе" },
    { Str::TrayExit, "Exit", "Выход" },

    // Status line
    { Str::StatusDisabled, "Latency reduction is off", "Снижение задержки выключено" },
    { Str::StatusNotActive, "Latency reduction is not active", "Снижение задержки не активно" },
    { Str::StatusDriverMinimum, "driver already uses its smallest buffer ({:.2f} ms) - {}",
                                "драйвер уже отдаёт минимальный буфер ({:.2f} мс) - {}" },
    { Str::StatusPeriodLocked, " (period locked by another app)", " (период занят другим приложением)" },
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
      "{:.2f} ms - {}",
      "{:.2f} мс - {}" },
    // The technical part of a stream in the journal: the format of the device
    // and the buffer it runs with ({1} and {3} come with their nouns).
    { Str::StreamDetails,
      ", {0} Hz, {1}, {2} bit, period {3} ({4})",
      ", {0} Гц, {1}, {2} бит, период {3} ({4})" },
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
    { Str::LogAudioSettings,
      "Latency reduction settings: {0} (the default and the default communication devices).",
      "Параметры снижения задержки: {0} (устройства по умолчанию и устройства связи по умолчанию)." },
    { Str::LogTrayForeignEvent,
      "Tray event 0x{0:04X} for the icon {1}, this icon is {2}.",
      "Событие значка 0x{0:04X} для значка {1}, а это значок {2}." },
    { Str::LogMutexFailed,
      "Could not create the single instance mutex: {0}",
      "Не удалось создать мьютекс единственного экземпляра: {0}" },
    { Str::LogInstanceNoAnswer,
      "Another instance seems to be running but did not answer; starting a new one.",
      "Другая копия, похоже, запущена, но не отвечает; запускаем новую." },
    { Str::LogSettingsUnreadable,
      "Built-in default settings are used; the settings file is left as it is.",
      "Используются встроенные значения настроек; файл настроек не изменён." },
    { Str::LogSettingsBackup,
      "The settings file that could not be read is kept as {0}.",
      "Файл настроек, который не удалось прочитать, сохранён как {0}." },
    { Str::LogComFailed,
      "Could not initialise COM: {0}",
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
      "The tray icon is disabled but the window would start hidden: showing the window.",
      "Значок трея отключён, но окно стартовало бы скрытым: показываем окно." },
    { Str::LogPriorityFailed,
      "Could not change the process priority: {0}",
      "Не удалось изменить приоритет процесса: {0}" },
    { Str::LogPowerThrottlingFailed,
      "Power throttling could not be disabled: {0}",
      "Не удалось отключить ограничение скорости выполнения: {0}" },
    { Str::LogPowerThrottlingOff,
      "Power throttling (execution speed) disabled for this process.",
      "Ограничение скорости выполнения отключено для этого процесса." },
    { Str::LogPowerThrottlingUnavailable,
      "Power throttling is not available with the Windows SDK used for this build.",
      "Ограничение скорости выполнения недоступно в SDK, с которым собран файл." },
    { Str::LogSettingsWriteFailed,
      "Could not write the settings file {0}.",
      "Не удалось записать файл настроек {0}." },
    { Str::RestartNeededTitle, "Restart REAL", "Перезапуск REAL" },
    { Str::RestartNeededText,
      "This setting is applied when REAL starts.\nRestart the program now?",
      "Эта настройка применяется при запуске REAL.\nПерезапустить программу сейчас?" },
    { Str::RestartNeededHint, "The setting needs a restart, the user is asked about it.",
                              "Настройка требует перезапуска, пользователю задан вопрос." },
    { Str::RestartLaterHint, "The restart was postponed; the setting works after the next start.",
                             "Перезапуск отложен: настройка вступит в силу при следующем запуске." },
    { Str::ErrRestartFailed,
      "Could not start a new copy of REAL. Close the program and start it again.",
      "Не удалось запустить новую копию REAL. Закройте программу и запустите её снова." },
    { Str::OpRestarting, "Restarting REAL.", "Перезапускаю REAL." },
    { Str::LogFileOff, "The log file is switched off (logging.level = \"off\").",
                       "Файл журнала выключен (logging.level = \"off\")." },
    { Str::LogFileMissing, "There is no log file yet.", "Файла журнала пока нет." },
    { Str::LogLanguageChanged,
      "Language changed to {0}.",
      "Язык изменён на {0}." },
    { Str::LogUpdatesDisabled,
      "The update check at start-up is switched off.",
      "Проверка обновлений при запуске выключена." },
    { Str::LogUpdateRunning,
      "An update check is already running.",
      "Проверка обновлений уже выполняется." },
    { Str::LogReinitInvalid,
      "The audio streams are no longer valid, activating again: {0}",
      "Аудиопотоки недействительны, активирую заново: {0}" },
    { Str::LogResumeApply,
      "The system reported {0}; re-applying the low latency mode.",
      "Система сообщила {0}; применяем режим заново." },
    { Str::LogHotkeyRegisterFailed,
      "Could not register the hotkey '{0}'.",
      "Не удалось зарегистрировать горячую клавишу «{0}»." },
    { Str::LogHotkeyParseFailed,
      "Could not parse the hotkey '{0}'.",
      "Не удалось разобрать горячую клавишу «{0}»." },
    { Str::LogAutostartOpenFailed,
      "Could not open the registry key for the autostart entry: {0}",
      "Не удалось открыть раздел реестра для автозапуска: {0}" },
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
      "Another application has already locked the audio engine period; the nearest one is used: {0}.",
      "Другое приложение уже зафиксировало период аудиодвижка; выбран ближайший период: {0}." },
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
      "The audio session has not been initialised.",
      "Аудиосессия не инициализирована." },
    { Str::ErrLowLatency,
      "Could not enable the low latency mode.",
      "Не удалось включить режим низкой задержки." },
    { Str::ErrNoEndpoint,
      "No audio endpoint could be inspected.",
      "Ни одно аудиоустройство не удалось проверить." },
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
      "Could not query the engine periods: {0}",
      "Не удалось получить периоды аудиодвижка: {0}" },
    { Str::ErrEngineLocked,
      "The audio engine is currently locked by another application: {0}",
      "Аудиодвижок сейчас зафиксирован другим приложением: {0}" },
    { Str::ErrInitStream,
      "Could not initialise the low latency stream: {0}",
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

    { Str::ReasonResume,
      "resume from sleep",
      "выход из спящего режима" },
    { Str::ReasonUnlock,
      "session unlock",
      "разблокировка сеанса" },
    { Str::CliReportWritten,
      "Report written to {0}",
      "Отчёт записан в {0}" },
    { Str::ArgConfigNeedsPath,
      "--config requires a path",
      "Для --config нужен путь" },
    { Str::ArgLogLevelNeedsValue,
      "--log-level requires a value",
      "Для --log-level нужно значение" },
    { Str::ErrEmptyUrl,
      "Empty URL.",
      "Пустой адрес." },
    { Str::ErrNoUpdateRepository,
      "The update repository is not configured for this build.",
      "Для этой сборки не задан репозиторий обновлений." },
    { Str::ErrGithubUnreachable,
      "Could not reach GitHub: {0}",
      "Не удалось связаться с GitHub: {0}" },
    { Str::ErrNoReleases,
      "The repository '{0}' has no published releases.",
      "В репозитории «{0}» нет опубликованных релизов." },
    { Str::ErrRateLimit,
      "GitHub refused the request (HTTP {0}), probably the API rate limit.",
      "GitHub отклонил запрос (HTTP {0}), вероятно, исчерпан лимит обращений к API." },
    { Str::ErrHttpStatus,
      "GitHub returned HTTP {0}.",
      "GitHub вернул HTTP {0}." },
    { Str::ErrGithubParse,
      "Could not parse the GitHub response: {0}",
      "Не удалось разобрать ответ GitHub: {0}" },
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
    { Str::DiagExecutable,
      "Executable:  {0}\n",
      "Файл:        {0}\n" },
    { Str::DiagSettings,
      "Settings:    {0}\n",
      "Настройки:   {0}\n" },
    { Str::DiagConfig,
      "Config:      {0}\n",
      "Параметры:   {0}\n" },
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
      "    periods:  {0}\n",
      "    периоды:  {0}\n" },
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
      "A device is suitable for the latency reduction when its minimum period is smaller than its default period (see 'result'). Typical exceptions: Bluetooth endpoints (10 ms by design), HDMI/DisplayPort receivers, some vendor drivers (Realtek, Nahimic, ACX) and virtual devices.\n"
      "The base step is the amount by which the engine can change its period: any value between the minimum and the maximum with that step is allowed. The step itself is not a period, so a value like \"step of 1 frame\" cannot be requested; REAL never asks for a period below the minimum.\n"
      "The small buffer is taken on both default devices of the chosen direction: the usual default device and the default communication device (Settings - System - Sound).\n",
      "Устройство подходит для снижения задержки, если его минимальный период меньше стандартного (см. «итог»). Обычные исключения: Bluetooth (10 мс по замыслу), приёмники HDMI/DisplayPort, некоторые драйверы производителей (Realtek, Nahimic, ACX) и виртуальные устройства.\n"
      "Базовый шаг — это ступень, с которой движок меняет период: допустимы значения от минимального до максимального с этим шагом. Сам шаг периодом не является, поэтому значение вида «шаг 1 фрейм» использовать нельзя — программа запрашивает период не меньше минимального.\n"
      "Малый буфер берётся на обоих устройствах по умолчанию выбранного направления: на обычном устройстве по умолчанию и на устройстве связи по умолчанию («Параметры → Система → Звук»).\n" },

    { Str::DiagPeriodsNoClient3,
      "device period {0} ({1})",
      "период устройства {0} ({1})" },
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
      "the engine periods could not be queried: {0}",
      "не удалось получить периоды аудиодвижка: {0}" },
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
    { Str::CfgWarnString,
      "{0}: expected a string, the default value is used",
      "{0}: ожидалась строка, взято значение по умолчанию" },
    { Str::CfgWarnBool,
      "{0}: expected true or false, the default value is used",
      "{0}: ожидалось true или false, взято значение по умолчанию" },
    { Str::CfgWarnInteger,
      "{0}: expected an integer, the default value is used",
      "{0}: ожидалось целое число, взято значение по умолчанию" },
    { Str::CfgWarnRange,
      "{0}: the value is out of the allowed range, the default value is used",
      "{0}: значение вне допустимого диапазона, взято значение по умолчанию" },
    { Str::CfgWarnUnknownKey,
      "{0}: unknown option, ignored",
      "{0}: неизвестный параметр, игнорируется" },
    { Str::CfgWarnUnknownValue,
      "{0}: unknown value \"{1}\" (allowed: {2}), the default value is used",
      "{0}: неизвестное значение «{1}» (допустимо: {2}), взято значение по умолчанию" },

    // Command line help
    { Str::HelpText,
      "{0} - {1} {2}\n"
      "\n"
      "Usage: REAL.exe [options]\n"
      "\n"
      "  (no options)          Start with the main window; the tray icon is created as well\n"
      "  --tray                Start minimised to the system tray\n"
      "  --no-tray             Start with the main window visible\n"
      "  --config <path>       Use the given settings file instead of real.settings.json\n"
      "  --no-config           Ignore the settings file, use the built-in defaults\n"
      "  --log-level <level>   trace | debug | info | warn | error | off\n"
      "  --multi-instance      Do not reuse an already running instance\n"
      "\n"
      "Commands for a running instance (the command is passed to it and this process exits):\n"
      "  --reinit              Restart: activate again (re-create the audio streams), the mode is enabled\n"
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
      "Settings: the \"Options\" item of the menu bar, or real.settings.json next to REAL.exe.\n"
      "Every parameter is explained by a comment inside that file, see also docs/CONFIG.md.\n",
      "{0} - {1} {2}\n"
      "\n"
      "Использование: REAL.exe [ключи]\n"
      "\n"
      "  (без ключей)          запуск с окном; значок в трее создаётся всегда\n"
      "  --tray                стартовать свёрнутым в системный трей\n"
      "  --no-tray             стартовать с видимым окном\n"
      "  --config <путь>       использовать другой файл настроек вместо real.settings.json\n"
      "  --no-config           не читать файл настроек, взять встроенные значения\n"
      "  --log-level <уровень> trace | debug | info | warn | error | off\n"
      "  --multi-instance      не переиспользовать уже запущенную копию\n"
      "\n"
      "Команды для работающей копии (передаются ей, этот процесс завершается):\n"
      "  --reinit              перезапустить: активировать заново (пересоздать потоки), режим включается\n"
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
      "Настройки: пункт «Опции» в строке меню или файл real.settings.json рядом с REAL.exe.\n"
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
      "While REAL is running, Windows uses the smallest buffer\n"
      "that the driver of the default audio device supports.",
      "Пока REAL запущен, Windows использует минимальный буфер,\n"
      "который поддерживает драйвер устройства по умолчанию." },

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
    { Str::OpHotkeys, "Hotkeys: toggle {}, activate {}", "Горячие клавиши: переключение {}, активация {}" },
    { Str::OpAutostart, "Autostart: {}", "Автозапуск: {}" },
    { Str::OpUpdateChecking, "Checking for updates...", "Проверяю обновления..." },
    { Str::OpExiting, "Exiting", "Выход" },
    { Str::OpTrayUnavailable, "The tray icon is unavailable, the window stays visible",
                              "Значок в трее недоступен, окно остаётся видимым" },
    { Str::OpDiagHint, "The device does not answer. Check the available devices: REAL.exe --diagnose",
                       "Устройство не отвечает. Проверьте доступные устройства: REAL.exe --diagnose" },
    { Str::ValueOn, "on", "вкл" },
    { Str::ValueOff, "off", "выкл" },

    // Settings window
    { Str::SettingsWindowTitle, "REAL options", "Опции REAL" },
    { Str::SettingsSave, "Save", "Сохранить" },
    { Str::SettingsCancel, "Cancel", "Отмена" },
    { Str::SettingsOpenFile, "Open the file", "Открыть файл" },
    { Str::SettingsReload, "Reload", "Перечитать" },
    { Str::SettingsInvalidValues, "Check these values: {0}", "Проверьте значения: {0}" },
    { Str::SettingsOpenFileFailed, "Could not open the settings file.", "Не удалось открыть файл настроек." },
    { Str::SettingsReloadFailed,
      "Could not read the settings file; the values in the window are left as they are.\n{0}",
      "Не удалось прочитать файл настроек; значения в окне оставлены как есть.\n{0}" },
    { Str::SettingsTabWindow, "Window", "Окно" },
    { Str::SettingsTabAudio, "Audio", "Звук" },
    { Str::SettingsTabOther, "Other", "Прочее" },
    { Str::SettingsHeaderApplication, "Application", "Приложение" },
    { Str::SettingsHeaderTray, "Tray icon and its menu", "Значок в трее и его меню" },
    { Str::SettingsHeaderMenu, "Tray menu items", "Пункты меню в трее" },
    { Str::SettingsHeaderAudio, "Audio streams", "Аудиопотоки" },
    { Str::SettingsHeaderReinit, "Reactivation", "Повторная активация" },
    { Str::SettingsHeaderPerformance, "Performance", "Производительность" },
    { Str::SettingsHeaderHotkeys, "Hotkeys", "Горячие клавиши" },
    { Str::SettingsHeaderUpdates, "Updates", "Обновления" },
    { Str::SettingsHeaderLog, "Log", "Журнал" },
    { Str::SettingsLanguage, "Language", "Язык" },
    { Str::SettingsLanguageAuto, "As in Windows", "Как в Windows" },
    { Str::SettingsLanguageEnglish, "English", "Английский" },
    { Str::SettingsLanguageRussian, "Russian", "Русский" },
    { Str::SettingsStartWithWindows, "Start with Windows", "Запускать вместе с Windows" },
    { Str::SettingsStartMinimized, "Start minimized to tray", "Запускать свёрнутым в трей" },
    { Str::SettingsMinimizeToTray, "Minimize to tray", "Сворачивать в трей" },
    { Str::SettingsCloseAction, "Close button", "Кнопка закрытия" },
    { Str::SettingsCloseMinimize, "Minimize to tray", "Свернуть в трей" },
    { Str::SettingsCloseExit, "Exit", "Завершить программу" },
    { Str::SettingsSingleInstance, "One copy only", "Один экземпляр" },
    { Str::SettingsTrayEnabled, "Show the tray icon", "Показывать значок в трее" },
    { Str::SettingsNotifyError, "Notify about errors", "Уведомлять об ошибках" },
    { Str::SettingsNotifyDeviceChange, "Notify about device changes", "Уведомлять о смене устройств" },
    { Str::SettingsNotifyStateChange, "Notify about the mode switching", "Уведомлять о переключении режима" },
    { Str::SettingsMenuStatus, "Status line", "Строка состояния" },
    { Str::SettingsEnabledOnStartup, "Turn on at start-up", "Включать при запуске" },
    { Str::SettingsDataFlow, "Streams", "Потоки" },
    { Str::SettingsFlowRender, "Playback (render)", "Воспроизведение (render)" },
    { Str::SettingsFlowCapture, "Recording (capture)", "Запись (capture)" },
    { Str::SettingsFlowBoth, "Playback and recording", "Воспроизведение и запись" },
    { Str::SettingsPeriod, "Period", "Период" },
    { Str::SettingsPeriodMinimum, "Minimum", "Минимальный" },
    { Str::SettingsPeriodFundamental, "By the base step", "По базовому шагу" },
    { Str::SettingsPeriodFixed, "Fixed value", "Фиксированный" },
    { Str::SettingsRequestedPeriod, "Requested period, frames", "Запрашиваемый период, фреймы" },
    { Str::SettingsAllowPeriodSnap, "Let Windows adjust the period", "Разрешить Windows подбирать период" },
    { Str::SettingsReinitDeviceChanged, "Default device changed", "Сменилось устройство по умолчанию" },
    { Str::SettingsReinitDeviceState, "Device state changed", "Сменилось состояние устройства" },
    { Str::SettingsReinitDeviceAdded, "Device added", "Устройство добавлено" },
    { Str::SettingsReinitDeviceRemoved, "Device removed", "Устройство удалено" },
    { Str::SettingsReinitResume, "Resume from sleep", "Выход из спящего режима" },
    { Str::SettingsReinitUnlock, "Session unlock", "Разблокировка сеанса" },
    { Str::SettingsReinitEnableWhenDisabled, "Enable when the driver resets the mode", "Включать, если драйвер сбросил режим" },
    { Str::SettingsReinitFailureTimeout, "Wait for the device, ms", "Ждать ответа устройства, мс" },
    { Str::SettingsReinitDebounce, "Pause before activating, ms", "Пауза перед активацией, мс" },
    { Str::SettingsProcessPriority, "Process priority", "Приоритет процесса" },
    { Str::SettingsPriorityNormal, "Normal", "Обычный" },
    { Str::SettingsPriorityBelowNormal, "Below normal", "Ниже среднего" },
    { Str::SettingsPriorityIdle, "Low", "Низкий" },
    { Str::SettingsDisablePowerThrottling, "Disable power throttling", "Отключить энергосбережение" },
    { Str::SettingsHotkeysEnabled, "Use hotkeys", "Использовать горячие клавиши" },
    { Str::SettingsHotkeyToggle, "Enable / disable", "Включить / выключить" },
    { Str::SettingsHotkeyReinitialize, "Activate again", "Активировать заново" },
    { Str::SettingsCheckOnStartup, "Check on start-up", "Проверять при запуске" },
    { Str::SettingsLogLevel, "Level", "Уровень" },
    { Str::SettingsLogFilePath, "File", "Файл" },
    { Str::SettingsLogMaxFileSize, "Max size, MB", "Максимальный размер, МБ" },
    { Str::SettingsLogMaxFiles, "Files to keep", "Хранить файлов, шт." },

    // Settings file comments
    { Str::CfgFileHeader,
      "Settings of REAL. The file is created automatically and is read when the program starts or when the settings are reloaded. Comments can be removed.",
      "Настройки REAL. Файл создаётся автоматически и читается при запуске и перезагрузке настроек. Комментарии можно удалять." },
    { Str::CfgApplicationSection, "Window, start and autostart.", "Окно, запуск и автозапуск приложения." },
    { Str::CfgStartMinimizedToTray, "true - start minimised in the tray (same as the --tray key).",
                                    "true - стартовать сразу свёрнутым в трей (то же, что ключ запуска --tray)." },
    { Str::CfgMinimizeToTray, "true - the Minimise button hides the window to the tray, not to the taskbar.",
                              "true - кнопка \"Свернуть\" прячет окно в трей, а не в панель задач." },
    { Str::CfgCloseButtonAction, "What the close button does: \"minimize\" (to the tray) or \"exit\" (quit).",
                                 "Что делает крестик окна: \"minimize\" (в трей) или \"exit\" (завершить программу)." },
    { Str::CfgSingleInstance, "true - a single copy: starting REAL.exe again passes the command to it.",
                              "true - одна копия: повторный запуск передаёт команду работающей (--reinit, --exit)." },
    { Str::CfgStartWithWindows, "true - start automatically after logon (HKCU Run key).",
                                "true - автозапуск при входе в систему (запись REAL в HKCU Run)." },
    { Str::CfgLanguage, "Language of the interface, the log and these comments: \"auto\" (Windows), \"en\", \"ru\". When the language changes, the comments are rewritten on the next start, the values stay.",
                        "Язык интерфейса, журнала и этих комментариев: \"auto\" (язык Windows), \"en\", \"ru\". При смене языка комментарии перезаписываются при следующем запуске, значения сохраняются." },
    { Str::CfgTraySection, "Icon in the notification area.", "Значок в системном трее." },
    { Str::CfgTrayEnabled, "true - show the tray icon (left click shows the window, right click opens the menu).",
                           "true - показывать значок в трее (левый клик - окно, правый - меню)." },
    { Str::CfgNotificationsSection, "Balloon notifications.", "Всплывающие уведомления." },
    { Str::CfgNotifyOnError, "true - notify about failures (at most one message per minute).",
                             "true - уведомлять об ошибках (не чаще одного сообщения в минуту)." },
    { Str::CfgNotifyOnDeviceChange, "true - notify when the audio device changed.",
                                    "true - уведомлять о смене аудиоустройства." },
    { Str::CfgNotifyOnStateChange, "true - notify when the latency reduction is switched on or off.",
                                   "true - уведомлять о включении и выключении режима." },
    { Str::CfgMenuSection, "Tray menu items (false hides an item).", "Состав меню значка (false - пункт скрыт)." },
    { Str::CfgMenuShowStatus, "Current status as the first line of the menu.",
                              "Строка с текущим статусом первой строкой меню." },
    { Str::CfgMenuToggle, "Item that enables or disables the latency reduction.",
                          "Пункт включения и выключения режима." },
    { Str::CfgMenuReinitialize, "Item that re-initialises the audio streams without a restart.",
                                "Пункт активации без перезапуска." },
    { Str::CfgMenuLog, "Item that opens the log.", "Пункт открытия журнала." },
    { Str::CfgMenuDiagnostics, "Item that writes a report about the audio devices.",
                               "Пункт создания отчёта об аудиоустройствах." },
    { Str::CfgMenuStartWithWindows, "Item that toggles the autostart.", "Пункт-переключатель автозапуска." },
    { Str::CfgMenuAbout, "Item with information about the program.", "Пункт \"О программе\"." },
    { Str::CfgMenuExit, "Item that quits the program.", "Пункт выхода из программы." },
    { Str::CfgAudioSection, "How REAL talks to the audio engine.", "Параметры работы с аудиодвижком." },
    { Str::CfgEnabledOnStartup, "true - apply the low latency mode right after the start.",
                                "true - включать снижение задержки сразу при запуске." },
    { Str::CfgDataFlow, "Devices to process: \"render\" (playback), \"capture\" (recording), \"both\".",
                        "Какие устройства обрабатывать: \"render\" (воспроизведение), \"capture\" (запись), \"both\"." },
    { Str::CfgPeriodSelection, "Which buffer to request: \"min\" - the smallest period of the device; \"fundamental\" - the same smallest period rounded up to the base step of the engine (the step itself is not a period, so a value below the minimum is never requested); \"fixed\" - exactly the number of frames written in requestedPeriodFrames.",
                               "Какой буфер запрашивать: \"min\" - минимальный период устройства; \"fundamental\" - он же, выровненный по базовому шагу движка (сам шаг периодом не является, поэтому меньше минимального не запрашивается никогда); \"fixed\" - ровно столько фреймов, сколько указано в requestedPeriodFrames." },
    { Str::CfgRequestedPeriodFrames, "The buffer in frames that \"fixed\" asks for: it is rounded to the base step of the engine and kept inside the range the device supports (0 - the smallest period of the device).",
                                     "Буфер во фреймах, который запрашивает \"fixed\": округляется по базовому шагу движка и удерживается в поддерживаемом устройством диапазоне (0 - минимальный период устройства)." },
    { Str::CfgAllowPeriodSnap, "true - if the buffer is already locked by another application, accept it instead of reporting an error.",
                               "true - если буфер уже зафиксирован другим приложением, принять его, а не сообщать об ошибке." },
    { Str::CfgReinitSection, "When to activate again automatically (the streams are re-created).",
                             "Когда активировать заново автоматически (потоки создаются заново)." },
    { Str::CfgReinitDeviceChanged, "The default device changed (the main case).",
                                   "Сменилось устройство по умолчанию (основной случай)." },
    { Str::CfgReinitDeviceState, "A device became active or inactive (headphones switched on).",
                                 "Устройство стало активным или неактивным (включение наушников)." },
    { Str::CfgReinitDeviceAdded, "A new device appeared.", "Подключено новое устройство." },
    { Str::CfgReinitDeviceRemoved, "A device was removed.", "Устройство удалено." },
    { Str::CfgReinitResume, "Resume from sleep or Modern Standby.", "Выход из сна или Modern Standby." },
    { Str::CfgReinitUnlock, "Session unlock (Win+L).", "Разблокировка сеанса (Win+L)." },
    { Str::CfgReinitEnableWhenDisabled, "true - a device change switches the latency reduction back on if it was off.",
                                        "true - при смене устройства включать снижение задержки, если оно было выключено." },
    { Str::CfgReinitFailureTimeout, "How long to keep retrying before the mode is switched off and polling stops (ms).",
                                    "Сколько миллисекунд повторять попытки, прежде чем выключить режим и прекратить опрос." },
    { Str::CfgReinitDebounce, "Pause before re-initialising: Windows sends a burst of events (ms).",
                              "Пауза перед активацией: Windows присылает пачку событий подряд (мс)." },
    { Str::CfgPerformanceSection, "Side effects of the low latency mode.", "Побочные эффекты низкой задержки." },
    { Str::CfgProcessPriority, "Process priority as in the Task Manager: \"normal\", \"belowNormal\" (below normal) or \"idle\" (low).",
                               "Приоритет процесса как в диспетчере задач: \"normal\" (обычный), \"belowNormal\" (ниже среднего), \"idle\" (низкий)." },
    { Str::CfgDisablePowerThrottling, "true - disable the execution speed throttling (Windows 11) so that the audio stream does not keep a CPU core busy.",
                                      "true - снять троттлинг скорости исполнения (Windows 11), чтобы аудиопоток не занимал ядро CPU." },
    { Str::CfgUpdatesSection, "Update checks. Off by default; REAL never checks while it is running.",
                              "Проверка обновлений. По умолчанию выключена; во время работы запросов нет." },
    { Str::CfgUpdatesCheckOnStartup, "true - check once at startup; false - no network request at all.",
                                     "true - один раз проверить обновления при запуске; false - ни одного сетевого запроса." },
    { Str::CfgHotkeysSection, "Global hotkeys.", "Глобальные горячие клавиши." },
    { Str::CfgHotkeysEnabled, "true - register the hotkeys.", "true - регистрировать горячие клавиши." },
    { Str::CfgHotkeysToggle, "Enable or disable the mode. Keys: Ctrl, Alt, Shift, Win, A-Z, 0-9, F1-F24.",
                             "Включить и выключить режим. Клавиши: Ctrl, Alt, Shift, Win, A-Z, 0-9, F1-F24." },
    { Str::CfgHotkeysReinitialize, "Activate the audio streams.", "Активировать аудиопотоки." },
    { Str::CfgLoggingSection, "Log file. The window of the program always shows the operations at the info level.",
                              "Файл журнала. В окне программы всегда видны основные операции уровня info." },
    { Str::CfgLoggingLevel, "\"off\" - the file is not written at all; \"trace\", \"debug\", \"info\", \"warn\", \"error\" - how detailed the file is.",
                            "\"off\" - файл не ведётся совсем; \"trace\", \"debug\", \"info\", \"warn\", \"error\" - насколько подробно ведётся файл." },
    { Str::CfgLoggingFilePath, "Log path: relative to the REAL.exe directory or absolute.",
                               "Путь к журналу: относительно каталога REAL.exe или абсолютный." },
    { Str::CfgLoggingMaxFileSize, "Log file size before rotation (MB).", "Размер файла журнала до ротации (МБ)." },
    { Str::CfgLoggingMaxFiles, "How many log files to keep.", "Сколько файлов журнала хранить." },
};

constexpr size_t TABLE_SIZE = sizeof(TABLE) / sizeof(TABLE[0]);

// Every identifier has to have a row, and no row may belong to nothing: the
// list and the table cannot drift apart without the build failing.
static_assert(
    TABLE_SIZE == static_cast<size_t>(Str::Count),
    "Every Str identifier needs a row in TABLE and the other way round (see Lang.h).");

// Indexed by Str, built once in Initialize().
const char* g_text[static_cast<size_t>(Str::Count)] = {};
Language g_language = Language::English;
bool g_initialized = false;

void Initialize() {
    if (g_initialized) {
        return;
    }

    for (size_t i = 0; i < TABLE_SIZE; ++i) {
        const size_t index = static_cast<size_t>(TABLE[i].id);
        if (index < static_cast<size_t>(Str::Count)) {
            g_text[index] = TABLE[i].english;
        }
    }

    g_initialized = true;
}

const char* Pick(Str id) {
    Initialize();

    const size_t index = static_cast<size_t>(id);
    if (index >= static_cast<size_t>(Str::Count) || g_text[index] == nullptr) {
        return "?";
    }

    if (g_language == Language::Russian) {
        for (size_t i = 0; i < TABLE_SIZE; ++i) {
            if (TABLE[i].id == id) {
                return TABLE[i].russian;
            }
        }
    }

    return g_text[index];
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
    // Accept "auto", "en", "english", "ru", "russian" in any case.
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

    return Language::English;
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
    return Pick(id);
}

std::wstring miniant::Lang::Wide(Str id) {
    return miniant::Text::ToWide(Pick(id));
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
    unsigned int count,
    const char* englishOne,
    const char* englishMany,
    const char* russianOne,
    const char* russianFew,
    const char* russianMany) {
    const char* noun = miniant::Lang::Current() == Language::Russian
        ? RussianForm(count, russianOne, russianFew, russianMany)
        : (count == 1 ? englishOne : englishMany);

    return std::to_string(count) + " " + noun;
}

}

std::string miniant::Lang::Frames(unsigned int count) {
    return Counted(count, "frame", "frames", "фрейм", "фрейма", "фреймов");
}

std::string miniant::Lang::Channels(unsigned int count) {
    return Counted(count, "channel", "channels", "канал", "канала", "каналов");
}

std::string miniant::Lang::Milliseconds(double value) {
    return fmt::format(Current() == Language::Russian ? "{:.2f} мс" : "{:.2f} ms", value);
}
