#include "Lang.h"

#include "Text.h"

#include <Windows.h>

#include <cctype>
#include <cstddef>

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
    { Str::ButtonReinitialize, "Reinitialize", "Переинициализировать" },
    { Str::ButtonSettings, "Settings file...", "Файл настроек..." },
    { Str::ButtonLog, "Open log", "Открыть журнал" },
    { Str::ButtonDiagnostics, "Diagnostics", "Диагностика" },
    { Str::ButtonHideToTray, "Hide to tray", "Свернуть в трей" },
    { Str::ButtonExit, "Exit", "Выход" },

    // Tray menu
    { Str::TrayToggleEnabled, "Latency reduction enabled", "Снижение задержки включено" },
    { Str::TrayReinitialize, "Reinitialize now", "Переинициализировать" },
    { Str::TraySettings, "Settings file...", "Файл настроек..." },
    { Str::TrayLog, "Open log", "Открыть журнал" },
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
    { Str::NotifyDeviceNotReadyHelp, "Retrying...", "Повторяю попытку..." },
    { Str::NotifyDisabledNoDevice, "Off: no audio device", "Выключено: нет аудиоустройства" },
    { Str::NotifyEnabled, "Latency reduction is on", "Снижение задержки включено" },
    { Str::NotifyDisabled, "Latency reduction is off", "Снижение задержки выключено" },
    { Str::NotifyDeviceChanged, "Device changed", "Устройство изменено" },
    { Str::NotifyUpdateAvailable, "REAL {} is available", "Доступна версия REAL {}" },
    { Str::NotifyUpdateLatest, "Latest version ({})", "Последняя версия ({})" },
    { Str::NotifyUpdateFailed, "Update check failed", "Проверка обновлений не удалась" },
    { Str::NotifyUpdateClick, "Click to open the release page", "Нажмите, чтобы открыть страницу релиза" },
    { Str::NotifyReportFailed, "Could not write the report", "Не удалось записать отчёт" },

    { Str::StatusSuspended,
      "disabled: the device does not answer - {:.2f} ms",
      "отключено: устройство не отвечает" },
    { Str::StatusActive,
      "{:.2f} ms - {}",
      "{:.2f} мс - {}" },
    { Str::UnknownDevice,
      "<unknown device>",
      "<неизвестное устройство>" },
    { Str::FlowRender,
      "output",
      "вывод" },
    { Str::FlowCapture,
      "input",
      "ввод" },
    { Str::LogBanner,
      "{0} - {1} {2}, mini)(ant 2018-2019, fork maintained by Mutaracha",
      "{0} - {1} {2}, mini)(ant 2018-2019, форк Mutaracha" },
    { Str::LogUpstream,
      "Upstream: {0}",
      "Исходный проект: {0}" },
    { Str::LogLanguage,
      "Language: {0} (setting: {1})",
      "Язык: {0} (настройка: {1})" },
    { Str::LogMutexFailed,
      "Could not create the single instance mutex: {0}",
      "Не удалось создать мьютекс единственного экземпляра: {0}" },
    { Str::LogInstanceNoAnswer,
      "Another instance seems to be running but did not answer; starting a new one.",
      "Другая копия, похоже, запущена, но не отвечает; запускаем новую." },
    { Str::LogSettingsUnreadable,
      "The settings file could not be read; built-in default settings are used.",
      "Файл настроек не удалось прочитать; используются встроенные значения." },
    { Str::LogComFailed,
      "Could not initialise COM: {0}",
      "Не удалось инициализировать COM: {0}" },
    { Str::LogWindowFailed,
      "Could not create the main window: {0}",
      "Не удалось создать главное окно: {0}" },
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
    { Str::LogSettingsSaved,
      "Settings saved to {0}.",
      "Настройки сохранены в {0}." },
    { Str::LogSettingsWriteFailed,
      "Could not write the settings file {0}.",
      "Не удалось записать файл настроек {0}." },
    { Str::LogConsoleRestart,
      "The console setting (showConsole) is applied after a restart.",
      "Настройка консоли (showConsole) применяется после перезапуска." },
    { Str::LogLanguageChanged,
      "Language changed to {0}.",
      "Язык изменён на {0}." },
    { Str::LogLogSnapshotFailed,
      "Could not write the log snapshot to {0}.",
      "Не удалось записать снимок журнала в {0}." },
    { Str::LogUpdatesDisabled,
      "Update checks are disabled (updates.mode = \"off\").",
      "Проверка обновлений отключена (updates.mode = \"off\")." },
    { Str::LogUpdateRunning,
      "An update check is already running.",
      "Проверка обновлений уже выполняется." },
    { Str::LogRepository,
      "Repository: {0}",
      "Репозиторий: {0}" },
    { Str::LogReinitInvalid,
      "Reinitialising the audio streams: {0}",
      "Переинициализация потоков: {0}" },
    { Str::LogApplyRetry,
      "The latency reduction could not be applied earlier; trying again.",
      "Ранее не удалось применить снижение задержки; пробуем снова." },
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
    { Str::LogLowLatencyFailed,
      "Could not enable the low latency mode ({0}).",
      "Не удалось включить режим низкой задержки ({0})." },
    { Str::LogDriverMinimum,
      "The driver of '{0}' does not offer a period smaller than the default one ({1} frames, {2:.2f} ms): the audio engine already uses its smallest buffer for this device, so nothing has to be held open.",
      "Драйвер устройства «{0}» не предлагает период меньше стандартного ({1} кадров, {2:.2f} мс): движок уже использует минимальный буфер для этого устройства, держать поток открытым не нужно." },
    { Str::LogLowLatencyStarted,
      "Low latency stream started: {0}",
      "Поток с низкой задержкой запущен: {0}" },
    { Str::LogPeriodLocked,
      "Another application has already locked the audio engine period; snapped to {0} frames.",
      "Другое приложение уже зафиксировало период аудиодвижка; выбран ближайший размер {0} кадров." },
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
      "Diagnostics: {0} endpoints for the {1} flow.",
      "Диагностика: устройств - {0}, направление - {1}." },
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
    { Str::ErrOpenEndpoint,
      "Could not open the default audio endpoint",
      "Не удалось открыть аудиоустройство по умолчанию" },
    { Str::ErrEndpointTransient,
      ": the audio endpoint is not available at the moment (device change or audio service restart); another attempt will be made automatically",
      ": аудиоустройство сейчас недоступно (смена устройства или перезапуск службы звука); попытка будет повторена автоматически" },
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

    { Str::ErrConsoleAttach,
      "Could not attach a console window.",
      "Не удалось подключиться к окну консоли." },
    { Str::ReasonResume,
      "resume from sleep",
      "выход из спящего режима" },
    { Str::ReasonUnlock,
      "session unlock",
      "разблокировка сеанса" },
    { Str::ConsoleReportWritten,
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
      "No repository is configured for update checks (updates.repository).",
      "Для проверки обновлений не задан репозиторий (updates.repository)." },
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
      "Version:    {0} ({1})\\n",
      "Версия:     {0} ({1})\\n" },
    { Str::DiagGenerated,
      "Generated:  {0}\\n",
      "Создан:     {0}\\n" },
    { Str::DiagWindows,
      "Windows:    {0}\\n",
      "Windows:    {0}\\n" },
    { Str::DiagExecutable,
      "Executable: {0}\\n",
      "Файл:       {0}\\n" },
    { Str::DiagSettings,
      "Settings:   {0}\\n",
      "Настройки:  {0}\\n" },
    { Str::DiagConfig,
      "Config:     {0}\\n",
      "Параметры:  {0}\\n" },
    { Str::DiagEnumeratorError,
      "ERROR: the audio device enumerator could not be created ({0}).\\n",
      "ОШИБКА: не удалось создать перечислитель аудиоустройств ({0}).\\n" },
    { Str::DiagAudiosrvHint,
      "The Windows audio service (Audiosrv) is probably not running.\\n",
      "Вероятно, не запущена служба Windows «Звук» (Audiosrv).\\n" },
    { Str::DiagFlowHeader,
      "--- {0} devices ---\\n\\n",
      "--- устройства: {0} ---\\n\\n" },
    { Str::DiagNoDevices,
      "No active devices.\\n\\n",
      "Активных устройств нет.\\n\\n" },
    { Str::DiagDefaultMark,
      "   [default]",
      "   [по умолчанию]" },
    { Str::DiagDeviceId,
      "    id:       {0}\\n",
      "    код:      {0}\\n" },
    { Str::DiagDriver,
      "    driver:   {0} {1}\\n",
      "    драйвер:  {0} {1}\\n" },
    { Str::DiagFormat,
      "    format:   {0} Hz, {1} channels, {2} bit\\n",
      "    формат:   {0} Гц, каналов {1}, бит {2}\\n" },
    { Str::DiagPeriods,
      "    periods:  {0}\\n",
      "    периоды:  {0}\\n" },
    { Str::DiagUnknown,
      "unknown",
      "неизвестно" },
    { Str::DiagResult,
      "    result:   {0}\\n",
      "    итог:     {0}\\n" },
    { Str::DiagSmallBuffer,
      "small buffers are available, a buffer of {0} can be requested",
      "малые буферы доступны, можно запросить буфер {0}" },
    { Str::DiagNoGain,
      "the driver offers nothing smaller than its default buffer, so REAL cannot lower the latency here",
      "драйвер не предлагает буфер меньше стандартного, снизить задержку здесь нельзя" },
    { Str::DiagNoAudioClient3,
      "IAudioClient3 is not available",
      "IAudioClient3 недоступен" },
    { Str::DiagSummaryHeader,
      "--- summary ---\\n\\n",
      "--- итог ---\\n\\n" },
    { Str::DiagSummary,
      "A device is suitable for the latency reduction when its minimum period is\\nsmaller than its default period (see 'result' above). Typical exceptions:\\nBluetooth endpoints (10 ms by design), HDMI/DisplayPort receivers and some\\nvendor drivers (Realtek, Nahimic, ACX) as well as virtual devices.\\n\\nIf a suitable device is used by default right after the next start, the status\\nline of the window shows the buffer size the audio engine is running with,\\nfor example '2.67 ms - Speakers (Realtek Audio)'.\\n",
      "Устройство подходит для снижения задержки, если его минимальный период меньше\\nстандартного (см. строку «итог» выше). Обычные исключения: Bluetooth (10 мс по\\nзамыслу), приёмники HDMI/DisplayPort и некоторые драйверы производителей\\n(Realtek, Nahimic, ACX), а также виртуальные устройства.\\n\\nЕсли подходящее устройство выбрано по умолчанию, в строке состояния окна виден\\nразмер буфера, с которым работает аудиодвижок, например\\n'2.67 мс - Динамики (Realtek Audio)'.\\n" },
    { Str::DiagFlowRender,
      "playback (render)",
      "воспроизведение (render)" },
    { Str::DiagFlowCapture,
      "recording (capture)",
      "запись (capture)" },
    { Str::DiagPeriodsNoClient3,
      "device period {0} frames ({1})",
      "период устройства {0} кадров ({1})" },
    { Str::DiagPeriodsDetail,
      "default {0} frames ({1}), minimum {2} frames ({3}), fundamental {4} frames, maximum {5} frames",
      "стандартный {0} кадров ({1}), минимальный {2} кадров ({3}), основной {4} кадров, максимальный {5} кадров" },
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

    // Command line help
    { Str::HelpText,
      "{0} - {1} {2}\n"
      "\n"
      "Usage: REAL.exe [options]\n"
      "\n"
      "  (no options)          Start with the main window; the tray icon is created as well\n"
      "  --tray                Start minimised to the system tray\n"
      "  --no-tray             Start with the main window visible\n"
      "  --console             Also open a console window with the operations log\n"
      "  --config <path>       Use the given settings file instead of real.settings.json\n"
      "  --no-config           Ignore the settings file, use the built-in defaults\n"
      "  --log-level <level>   trace | debug | info | warn | error | off\n"
      "  --multi-instance      Do not reuse an already running instance\n"
      "\n"
      "Commands for a running instance (the command is passed to it and this process exits):\n"
      "  --reinit              Re-initialise the audio streams and enable the mode again\n"
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
      "Settings: real.settings.json next to REAL.exe (created on the first run).\n"
      "Every parameter is explained by a comment inside that file, see also docs/CONFIG.md.\n",
      "{0} - {1} {2}\n"
      "\n"
      "Использование: REAL.exe [ключи]\n"
      "\n"
      "  (без ключей)          запуск с окном; значок в трее создаётся всегда\n"
      "  --tray                стартовать свёрнутым в системный трей\n"
      "  --no-tray             стартовать с видимым окном\n"
      "  --console             дополнительно открыть консоль с журналом операций\n"
      "  --config <путь>       использовать другой файл настроек вместо real.settings.json\n"
      "  --no-config           не читать файл настроек, взять встроенные значения\n"
      "  --log-level <уровень> trace | debug | info | warn | error | off\n"
      "  --multi-instance      не переиспользовать уже запущенную копию\n"
      "\n"
      "Команды для работающей копии (передаются ей, этот процесс завершается):\n"
      "  --reinit              переинициализировать потоки и включить режим заново\n"
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
      "Настройки: real.settings.json рядом с REAL.exe (создаётся при первом запуске).\n"
      "У каждого параметра есть комментарий прямо в файле, подробнее - docs/CONFIG.md.\n" },

    // Dialogs
    { Str::AboutTitle, "About REAL", "О программе REAL" },
    { Str::AboutText,
      "REAL {}\n"
      "While REAL is running, Windows uses the smallest buffer the driver of the default audio device supports.\n"
      "\n"
      "A click on the tray icon shows or hides this window, the tray menu can re-initialise the audio streams.\n"
      "\n"
      "Settings: {}\n"
      "Project:  {}",
      "REAL {}\n"
      "Пока REAL запущен, Windows использует минимальный буфер, который поддерживает драйвер устройства по умолчанию.\n"
      "\n"
      "Клик по значку в трее показывает и прячет это окно, через меню значка можно переинициализировать аудиопотоки.\n"
      "\n"
      "Настройки: {}\n"
      "Проект:  {}" },
    { Str::DiagnosticsWriteFailed, "The diagnostics report could not be written to a file.",
                                   "Не удалось записать отчёт диагностики в файл." },

    // Console and log: operations
    { Str::OpStarted, "REAL {} started", "REAL {} запущен" },
    { Str::OpSettingsFile, "Settings: {}", "Настройки: {}" },
    { Str::OpSettingsCreated, "Settings file created: {}", "Создан файл настроек: {}" },
    { Str::OpSettingsReloaded, "Settings reloaded", "Настройки перечитаны" },
    { Str::OpAlreadyRunning, "REAL is already running; the command was passed to it",
                             "REAL уже запущен, команда передана ему" },
    { Str::OpApplying, "Applying the low latency mode", "Применяю режим низкой задержки" },
    { Str::OpApplied, "Latency reduction is active: {}", "Снижение задержки активно: {}" },
    { Str::OpApplyFailed, "Could not apply the low latency mode", "Не удалось применить режим низкой задержки" },
    { Str::OpEnabled, "Latency reduction enabled", "Снижение задержки включено" },
    { Str::OpDisabled, "Latency reduction disabled", "Снижение задержки выключено" },
    { Str::OpReinitialising, "Reinitialising the audio streams", "Переинициализация аудиопотоков" },
    { Str::OpDeviceChanged, "Audio device changed, applying again", "Аудиоустройство изменилось, применяю заново" },
    { Str::OpDeviceNotReady, "Device is not ready, retrying in {} s", "Устройство не готово, повтор через {} с" },
    { Str::OpRetry, "Retrying", "Повторяю" },
    { Str::OpGaveUp, "The device did not respond within {} s, latency reduction is off",
                     "Устройство не ответило за {} с, снижение задержки выключено" },
    { Str::OpDiagnosticsStart, "Diagnostics: collecting information about the audio devices",
                               "Диагностика: собираю сведения об аудиоустройствах" },
    { Str::OpDiagnostics, "Diagnostics report: {}", "Отчёт диагностики: {}" },
    { Str::OpDiagnosticsFailed, "Could not write the diagnostics report", "Не удалось записать отчёт диагностики" },
    { Str::OpHotkeys, "Hotkeys: toggle {}, reinitialise {}", "Горячие клавиши: переключение {}, переинициализация {}" },
    { Str::OpAutostart, "Autostart: {}", "Автозапуск: {}" },
    { Str::OpUpdateChecking, "Checking for updates...", "Проверяю обновления..." },
    { Str::OpExiting, "Exiting", "Выход" },
    { Str::OpTrayUnavailable, "The tray icon is unavailable, the window stays visible",
                              "Значок в трее недоступен, окно остаётся видимым" },
    { Str::OpSessionNotificationFailed, "Session notifications are unavailable",
                                        "Уведомления о сеансе недоступны" },
    { Str::OpReportHint, "Report about the devices: REAL.exe --diagnose",
                         "Отчёт об устройствах: REAL.exe --diagnose" },
    { Str::ValueOn, "on", "вкл" },
    { Str::ValueOff, "off", "выкл" },

    // Settings file comments
    { Str::CfgFileHeader,
      "Settings of REAL. The file is created automatically and is read when the program starts or when the settings are reloaded. Comments can be removed.",
      "Настройки REAL. Файл создаётся автоматически и читается при запуске и перезагрузке настроек. Комментарии можно удалять." },
    { Str::CfgConfigVersion, "Format version, service field. Current version: 1.",
                             "Версия формата настроек, служебное поле. Текущая версия: 1." },
    { Str::CfgApplicationSection, "Window, start and autostart.", "Окно, запуск и автозапуск приложения." },
    { Str::CfgStartMinimizedToTray, "true - start minimised in the tray (same as the --tray key).",
                                    "true - стартовать сразу свёрнутым в трей (то же, что ключ запуска --tray)." },
    { Str::CfgMinimizeToTray, "true - the Minimise button hides the window to the tray, not to the taskbar.",
                              "true - кнопка \"Свернуть\" прячет окно в трей, а не в панель задач." },
    { Str::CfgCloseButtonAction, "What the close button does: \"minimize\" (to the tray) or \"exit\" (quit).",
                                 "Что делает крестик окна: \"minimize\" (в трей) или \"exit\" (завершить программу)." },
    { Str::CfgShowConsole, "true - open a console window with the operations log (same as --console).",
                           "true - дополнительно открыть окно консоли с журналом операций (то же, что --console)." },
    { Str::CfgSingleInstance, "true - a single copy: starting REAL.exe again passes the command to it.",
                              "true - одна копия: повторный запуск передаёт команду работающей (--reinit, --exit)." },
    { Str::CfgStartWithWindows, "true - start automatically after logon (HKCU Run key).",
                                "true - автозапуск при входе в систему (запись REAL в HKCU Run)." },
    { Str::CfgLanguage, "Language of the interface, the log and these comments: \"auto\" (Windows), \"en\", \"ru\". Comments are written once, when the file is created.",
                        "Язык интерфейса, журнала и этих комментариев: \"auto\" (язык Windows), \"en\", \"ru\". Комментарии пишутся один раз, при создании файла." },
    { Str::CfgTraySection, "Icon in the notification area.", "Значок в системном трее." },
    { Str::CfgTrayEnabled, "true - show the tray icon (left click shows the window, right click opens the menu).",
                           "true - показывать значок в трее (левый клик - окно, правый - меню)." },
    { Str::CfgTrayTooltip, "true - show the current buffer size in the tray tooltip.",
                           "true - показывать текущий размер буфера в подсказке значка." },
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
                                "Пункт переинициализации без перезапуска." },
    { Str::CfgMenuSettings, "Item that opens this settings file.", "Пункт открытия этого файла настроек." },
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
    { Str::CfgRole, "Default device role: \"console\", \"multimedia\", \"communications\".",
                    "Роль устройства по умолчанию: \"console\", \"multimedia\", \"communications\"." },
    { Str::CfgPeriodSelection, "Which buffer to request: \"min\", \"fundamental\" or \"fixed\" (see the next key).",
                               "Какой буфер запрашивать: \"min\" (минимальный), \"fundamental\" (базовый), \"fixed\"." },
    { Str::CfgRequestedPeriodFrames, "Buffer size in frames for periodSelection = \"fixed\" (0 - decide automatically).",
                                     "Размер буфера в кадрах для periodSelection = \"fixed\" (0 - решает приложение)." },
    { Str::CfgAllowPeriodSnap, "true - if the buffer is already locked by another application, accept it instead of reporting an error.",
                               "true - если буфер уже зафиксирован другим приложением, принять его, а не сообщать об ошибке." },
    { Str::CfgReleaseOnExit, "true - release the audio stream on exit (the engine returns to 10 ms by itself).",
                             "true - освобождать аудиопоток при выходе (движок сам вернётся к 10 мс)." },
    { Str::CfgReinitSection, "When to re-initialise the streams automatically.",
                             "Когда переинициализировать потоки автоматически." },
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
                              "Пауза перед переинициализацией: Windows присылает пачку событий подряд (мс)." },
    { Str::CfgPerformanceSection, "Side effects of the low latency mode.", "Побочные эффекты низкой задержки." },
    { Str::CfgProcessPriority, "Process priority: \"normal\", \"belowNormal\" or \"idle\".",
                               "Приоритет процесса: \"normal\", \"belowNormal\" или \"idle\"." },
    { Str::CfgDisablePowerThrottling, "true - disable the execution speed throttling (Windows 11) so that the audio stream does not keep a CPU core busy.",
                                      "true - снять троттлинг скорости исполнения (Windows 11), чтобы аудиопоток не занимал ядро CPU." },
    { Str::CfgUpdatesSection, "Update checks. Off by default; REAL never checks while it is running.",
                              "Проверка обновлений. По умолчанию выключена; во время работы запросов нет." },
    { Str::CfgUpdatesMode, "\"off\" - no network request at all; \"manual\" - one check at startup if checkOnStartup is true.",
                           "\"off\" - ни одного сетевого запроса; \"manual\" - одна проверка при запуске, если checkOnStartup включён." },
    { Str::CfgUpdatesRepository, "GitHub repository the releases are taken from (owner/name).",
                                 "Репозиторий GitHub, из которого берутся релизы (owner/name)." },
    { Str::CfgUpdatesCheckOnStartup, "true - check once at startup (only with mode = \"manual\").",
                                     "true - один раз проверить обновления при запуске (только при mode = \"manual\")." },
    { Str::CfgHotkeysSection, "Global hotkeys.", "Глобальные горячие клавиши." },
    { Str::CfgHotkeysEnabled, "true - register the hotkeys.", "true - регистрировать горячие клавиши." },
    { Str::CfgHotkeysToggle, "Enable or disable the mode. Keys: Ctrl, Alt, Shift, Win, A-Z, 0-9, F1-F24.",
                             "Включить и выключить режим. Клавиши: Ctrl, Alt, Shift, Win, A-Z, 0-9, F1-F24." },
    { Str::CfgHotkeysReinitialize, "Re-initialise the audio streams.", "Переинициализировать аудиопотоки." },
    { Str::CfgLoggingSection, "Log of the program and console output.", "Журнал работы программы и вывод в консоль." },
    { Str::CfgLoggingLevel, "Verbosity: \"trace\", \"debug\", \"info\", \"warn\", \"error\", \"off\".",
                            "Подробность: \"trace\", \"debug\", \"info\", \"warn\", \"error\", \"off\"." },
    { Str::CfgLoggingToConsole, "true - mirror the log to the console (needs showConsole or --console).",
                                "true - дублировать журнал в консоль (нужен showConsole или ключ --console)." },
    { Str::CfgLoggingToFile, "true - write the log file.", "true - писать файл журнала." },
    { Str::CfgLoggingFilePath, "Log path: relative to the REAL.exe directory or absolute.",
                               "Путь к журналу: относительно каталога REAL.exe или абсолютный." },
    { Str::CfgLoggingMaxFileSize, "Log file size before rotation (MB).", "Размер файла журнала до ротации (МБ)." },
    { Str::CfgLoggingMaxFiles, "How many log files to keep.", "Сколько файлов журнала хранить." },
};

constexpr size_t TABLE_SIZE = sizeof(TABLE) / sizeof(TABLE[0]);

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
