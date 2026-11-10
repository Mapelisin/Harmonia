# Harmonia — инструкции для Claude

Harmonia — бесплатная DAW с открытым кодом и локальными ИИ-инструментами.
Репозиторий: https://github.com/Mapelisin/Harmonia (публичный с 2026-11-08), ветка `main`. Лицензия — GNU AGPLv3 (`LICENSE`, решение 45).

## В начале каждой сессии
1. Прочитай `docs/PROJECT.md`: там принятые решения, текущий этап и следующий шаг.
2. Не пересматривай принятые решения без новой причины. Если причина появилась — объясни её пользователю и получи согласие.
3. В конце работы обнови в `docs/PROJECT.md` статус этапов, журнал решений и журнал сессий.

## Пользователь
- Музыкант, не программист. Общение на русском. Технические термины объясняй простыми словами.
- Часто пишет голосом, распознавание путает слова: «Juice» = JUCE, «DAV» / «давка» = DAW.
- Технические решения доверил Claude («ты тут главный»). Продуктовые решения (что делаем, как это выглядит, в каком порядке) — согласовывать с пользователем.
- Предложения других ИИ (например, `docs/original-spec.md` от ChatGPT) — входные данные для оценки, а не истина.

## Порядок работы
Интервью → исследование (разрешены маленькие пробные программы) → архитектура → план → реализация.
Реализация идёт небольшими этапами. После каждого этапа: тесты, сборка, проверка изменений,
обновление документации, показать результат пользователю, получить согласие на следующий этап.

## Правила качества
- Не заявлять о готовности того, что не проверено. Не имитировать функции заглушками.
- Никаких временных архитектурных решений. Небольшие файлы, без дублирования логики.
- Аудиопоток: никакого выделения памяти, блокирующих mutex, файлового ввода-вывода и ИИ-инференса.
  Воспроизведение не зависит от GUI.
- Весь платформенный код — за абстракциями: macOS (Apple Silicon) добавим позже.
- Код, имена и комментарии в коде — на английском.
- Всё, что читает посетитель GitHub, — сначала по-английски, русский — дополнением (решение 36): `README.md` и
  шпаргалки `docs/user/<имя>.md` — английские, рядом русские `README.ru.md` и `docs/user/<имя>.ru.md`; вверху
  обоих — строка-переключатель «English | Русский». Меняешь один файл — в том же коммите меняй и его пару;
  ссылки в русских файлах ведут на `.ru.md`. Новая шпаргалка — сразу в двух файлах. Описание репозитория
  на GitHub — по-английски. Внутренние документы (`docs/PROJECT.md`, `docs/roadmap.md`, `docs/research`,
  `docs/architecture`) остаются на русском: их читает пользователь.
- ИИ и помощники (генераторы, Mix Doctor) ничего не меняют в песне сами: показать и объяснить, изменить по кнопке,
  дать услышать до принятия, отмена одним Ctrl+Z (решение 28).
- Минимум действий: в описании этапа — за сколько действий делается главное, и это проверено (решение 29).
  Чего ещё не хватает по сравнению с чужими DAW — `docs/research/competitor-pain-points.md`.

## Сборка
- Зависимости: подмодуль `external/tracktion_engine` (JUCE внутри него, `modules/juce`). Вложенный подмодуль
  прописан через SSH, поэтому инициализировать так:
  `git -c url."https://github.com/".insteadOf="git@github.com:" submodule update --init --recursive`
- C++: MSVC + CMake + Ninja из Visual Studio 2026 через `vcvars64.bat`.
- **Одна команда:** `tools\build.cmd [windows-release|windows-debug]` — настройка, сборка и тесты (её же запускает CI).
  Пресеты — в `CMakePresets.json`. Программа: `build\windows-release\src\app\Harmonia_artefacts\Release\Harmonia.exe`.
- Установщик (решение 25): после сборки Release — `tools\make-installer.cmd` (Inno Setup 6) → `build\installer`;
  проверка — `tools\test-installer.ps1 -Installer <exe>` (ставит «для себя» во временную папку и удаляет; где Harmonia
  установлена — отказывается). Версия — одна, в `CMakeLists.txt` (0.<номер этапа>.0); `AppId` в `.iss` не менять.
- Структура: `src/core` (модель проекта, без GUI), `src/audio` (движок, устройства), `src/ui` (интерфейс),
  `src/app` (точка входа), `tests/` (doctest, по папке на модуль). JUCE+Tracktion компилируются один раз
  в библиотеку `harmonia_juce` (`cmake/HarmoniaJuce.cmake`); новые цели настраивать через `harmonia_configure_target`.
- Предупреждения — ошибки (`/W4 /WX`). Каждое изменение модели — с тестом.
- Перед коммитом изменений в `core`/`audio` гонять и `windows-debug`: там тесты падают на любом jassert JUCE/Tracktion
  (`JUCE_LOG_ASSERTIONS` + ловушка в `tests/TestMain.cpp`). Release этого не видит.
- Ноты клипа меняются только через `core::NoteEditor` (undo + связанные копии), жесты — `Project::beginGesture`.
  Бенд, колесо модуляции и педаль клипа — только через `core::controllers` (то же: undo и связанные копии).
- После изменений в `src/audio` гонять самопроверку звука (нужна звуковая карта):
  `Harmonia.exe --selftest --log=<файл> "--plugin=E:/HarmoniaTestPlugins/surge-xt/Surge Synth Team/Surge XT.vst3"`.
  Код возврата = число проваленных проверок. В Debug она же ловит jassert движка.
- Play/Stop — только через `audio::Transport` (он продолжает игру после самопроизвольных остановок движка).
- Piano roll: всё поведение — в `ui/pianoroll/PianoRollModel` (команды, выделение) и `GridGestures` (мышь),
  их тесты гоняют синтетические события без окна. Компоненты только рисуют и передают ввод. Время в piano roll —
  доли от начала песни; в клипе ноты хранятся от начала его содержимого (`PianoRollModel` переводит).
- Пошаговый секвенсор (на месте piano roll, Alt+T): поведение — `ui/stepseq/StepSequencerModel`, своих данных
  нет, ноты меняет только через `PianoRollModel`. Показан ли он — `MainComponent::areStepsShown`.
- Проверка интерфейса без окна: тесты «the main window paints…» рисуют главное окно в картинку; с переменной
  `HARMONIA_SNAPSHOT_DIR=<папка>` сохраняют `main-recording.png`, `main-arrangement.png`, `main-mixer.png`,
  `main-automation.png`, `main-audio.png` и `main-audio-recording.png`
  (`harmonia_tests.exe -tc="*paints*"`). Живая проверка мышью (скрипт
  кликает по окну) захватывает настоящий курсор — только если пользователь не за компьютером или разрешил.
- Самопроверка без звука в динамиках: `--selftest --quiet` (громкость мастера в ноль, уровни меряются на дорожке).
- Горячие клавиши сверять по `ui::keys::getLetter` (русская раскладка); клавиатура-пианино — по скан-кодам.
- Аранжировка, жесты (решение 44, как в Ableton): клип берётся за полоску с именем (`clipHeaderHeight`),
  протяжка в любом другом месте дорожек выделяет кусок времени (`ArrangementModel::TimeSelection`); пока он
  есть, команды правки (Del, Ctrl+X/C/V/B/E/L, перенос мышью) работают с ним: `deleteTime`, `copyTime`,
  `moveTime`… Новая команда над клипами в аранжировке должна решить, что она делает с куском времени. В тестах
  клип тащить за имя (`onName`), не за тело.
- Аранжировка: поведение — в `ui/arrangement/ArrangementModel` и `ArrangementGestures` (тесты без окна).
  Открытый клип и текущая дорожка живут в `ArrangementModel`; `MainComponent` переносит их в piano roll.
  Перед сменой клипа piano roll — `NoteInput::finishTake()`.
- Редактор аудиоклипа (нижняя панель аудиодорожки): поведение — `ui/audioeditor/AudioEditorModel` (клип —
  `ArrangementModel::getFocusedAudioClip`, секунды файла ↔ доли песни, команды) и `AudioEditorGestures` (мышь),
  тесты без окна. Ручки фейдов и громкости аудиоклипа (и в аранжировке) — общий `ui/AudioClipHandles`.
- Несохранённые изменения считает `core::Project` (любое действие в undo); `clearUndoHistory()` и `save()` их сбрасывают.
- Undo/redo — только через `core::Project::undo/redo` (не `UndoManager` напрямую): Tracktion после undo не
  переносит значения в параметры плагинов, `Project` их синхронизирует.
- Клипы — `te::Clip` (MIDI или аудио) через `core::clips`; `getMidiClips` — только где нужны ноты. Вид дорожки —
  `core::tracks`: аудиоклипы только на аудиодорожки. В тестах после правки клипов — `dispatch()` перед undo:
  Tracktion сортирует клипы дорожки асинхронно и с undo, иначе этот шаг съест redo. В программе этот «хвост»
  остаётся в шаге своей команды: `core::Project` придерживает таймер движка «новый шаг undo через 350 мс»,
  пока песня не успокоится (`Project::isSettling`; в тесте дождаться — крутить `dispatch`, пока оно true).
- Запись звука: готовность/вход — только через `audio::AudioInputs`; запись — `audio::Transport` (`recordsAudio`).
  Тесты записи и живого звука — `test::AudioRig` (`tests/TestAudioRig.h`: свой `core::Session` с
  `HostedAudioDeviceInterface`, тест сам гонит звук и меряет выход), общий тестовый движок без устройств не трогать.
- Смены темпа по ходу песни — только через `core::tempo_changes` (держит клипы, ноты и автоматизацию на
  долях; не `tempoSequence.insertTempo` напрямую). Темп «на всю песню» больше не один: где нужен темп
  в месте — `tempo_changes::getBpmAt`, длины считать в долях. Размер по-прежнему один (`getTimeSig (0)`).
- Дубли аудиоклипа — только через `core::takes` (не `te::WaveAudioClip::setCurrentTake`: дубли Tracktion
  требуют его проектов и рендерят в фоне). Запись по кругу включает `Transport::setRecordsTakes`.
- Автоматизация — только через `audio::automation` (доли и 0..1; правки в текущую транзакцию undo — жест
  начинает вызывающий). Мышь панели — `ui/automation/AutomationGestures` (тесты без окна).
- Посылы и возвратные дорожки — только через `audio::sends`. `Project::getTracks` — дорожки песни (без
  возвратных и групп, `core::tracks::isBus`), `getReturnTracks` — возвратные, `getGroupTracks` — группы.
- Сэмплы на клавишах драм-машины — только через функции `audio/DrumSamples.h` (`setSample`, `clearSample`,
  `setSampleGainDb`…: undo, песня, пресеты). Звуковой части новый набор отдаёт `drum_kit::Plugin` сам, чуть
  позже изменения; нужно сразу (послушать, экспорт в тесте) — `Plugin::updateSamplesNow()`. Файлы читаются
  в память вне звукового потока; в звуковом потоке — ни чтения, ни освобождения памяти.
- Сэмплер (`audio::sampler`, один звук нотами): звук и основная нота — только через `sampler::setFile` /
  `setRoot` (undo, песня, пресеты); новый звук звуковой части отдаёт `sampler::Plugin` сам, чуть позже —
  нужно сразу: `Plugin::updateSampleNow()`. Звук в памяти — `audio::SampleData` (общий с драм-машиной;
  годится ли файл — `SampleData::check`). Дорожка-сэмплер — `ArrangementModel::addSamplerTrack`, окно —
  `ui::SamplerPanel`, в piano roll — `PianoRollModel::getSampler / setSamplerSound / setSamplerRoot`.
- Что можно выбрать дорожке (инструменты и эффекты, свои и найденные сканом) — `audio::plugin_catalog`:
  поиск, избранное и недавние (хранятся в настройках программы, не в песне). Окно выбора — `ui::PluginChooser`
  (`show`), инструмент по выбору — `InstrumentPanel::choose`, эффект — `ChannelStrip::addEffect (Entry)`.
  В тестах «найденные» плагины и чистое избранное — `test::ScannedPlugins` (`tests/TestScannedPlugins.h`).
- Сохранение песни из окна — через `core::backups::save` (прежняя версия уходит в «<песня> Backups»), не
  `Project::save` напрямую. Всё, что в песне называет звуковой файл (клипы, дубли, сэмплы драм-машины, звук
  сэмплера), — через `core::song_files` (`forEachReference`, `resolve`): новый вид ссылки на файл добавлять
  туда, иначе его не соберёт «Collect all files», не найдёт поиск пропавших и путь не станет относительным
  при сохранении. Путь в узле может быть относительным (от файла песни) — читать через `song_files::resolve`.
- Заморозка дорожки — только через `audio::freeze` (в аранжировке — `ArrangementModel::freezeTrack`). Файл
  заморозки — скрытый клип (`core::clips::isFreezeClip`): `core::clips::getClips` его не отдаёт, а
  `track.getClips()` отдаёт — где перебираешь клипы движка напрямую, проверяй. Строка замороженной дорожки
  не правится (`isLockedRow`).
- Папки дорожек — только через `core::folders` (после переноса дорожки — `trackMoved`, после удаления или
  копии — `tidy`). `ArrangementModel::getTracks / getTrack / indexOf` — строки аранжировки (без дорожек,
  спрятанных в свёрнутой папке); все дорожки песни — `getAllTracks()` или `Project::getTracks()`.
  Строка свёрнутой папки (`isFoldedRow`) не правится: клипы в ней — `DrawnClip::folded`.
- Группы — только через `audio::groups` (выход дорожки в группу — `setGroup`; хранится по ID, патч 0002),
  сайдчейн — через `audio::sidechain` (удаляешь дорожку — `forgetSource`). Рендер части песни берёт источники
  сайдчейна беззвучно (`sidechain::getSourcesFor`).
- Внешние плагины работают в своих процессах (`src/sandbox`, решение 23): `Session` подменяет
  `PluginManager::createPluginInstance`, процесс — тот же exe с `--harmonia-plugin-host` (и тестовая программа
  тоже: `main` проверяет ключ первым). Упавшие плагины, перезапуск, окно поверх — `audio::plugin_safety`; окно
  плагина показывает его процесс (`ui::RemotePluginWindow`), у такого плагина не звать `createEditor`. Тесты
  плагинов — тестовые плагины формата HarmoniaTest (`tests/sandbox/TestPlugins.h`, умеют падать и зависать;
  песня с ними — `TestPluginSong.h`). Свои папки скана — `PluginScanner::addFolder` (настройки).
- Экспорт песни — `audio::exportSong` с `audio::ExportFormat` (WAV 16/24/32, FLAC, MP3; `exportWav` — WAV 24).
  MP3 — `audio::mp3` (Windows: Media Foundation, `Mp3EncoderWindows.cpp`; решение 24). Окно формата —
  `ui::ExportOptions`, последний формат — `getLastExportFormat`.
- Настройки — `ui::SettingsPanel` (вкладки в `src/ui/settings`). MIDI-клавиатуры вкл/выкл — `MidiInputs::setKeyboardOn`
  (хранит Tracktion); плагины в своих процессах вкл/выкл — `Session::setPluginSandboxOn` (в тестах включено на
  старте). Скан плагинов — один на программу (`PluginScanner::startScan` вернёт false).
- Эффекты — через `audio::effects` (порядок в списке плагинов: инструмент, эффекты, громкость, индикатор);
  перенос и копия на другую дорожку — `moveTo` / `copyTo`, цепочки в файлах — `audio::chains`. Узел плагина
  с его текущими настройками (для копии, файла, сохранения) — `core::plugin_state::snapshot`, не
  `flushPluginStateToValueTree` (он пишет в undo).
- MIDI Learn — только через `audio::midi_learn` (привязки в песне, с undo; окно держит один `Controller`,
  остальные находят его `Controller::find`). Запись автоматизации вкл/выкл — `audio::automation::Writer`.
  Ползунок, который двигает параметр мышью, берёт его `automation::beginTouch` / `endTouch` (или `Touch`),
  не `parameterChangeGestureBegin` напрямую: мышь и MIDI-ручка могут держать параметр одновременно.
  Меню ПКМ ползунка — `ui::parameter_menu`, сам ползунок — `ui::MenuSlider`.
- Пресеты — только через `audio::presets` (меню — `ui::preset_menu`, папка в тестах — своя временная). Новый
  встроенный плагин должен уметь `restorePluginStateFromValueTree` (иначе jassert); настройки VST грузятся
  своим действием undo, не `restorePluginStateFromValueTree` напрямую.
  Ручки плагинов — через `audio::PluginControl` (у Chorus/Phaser/времени Delay/типа фильтра нет параметров).
- Исправления Tracktion — патчи в `external/patches/tracktion_engine` (решение 21), их накладывает настройка CMake.
  Правишь движок — потом `git -C external/tracktion_engine diff > <патч>`; меняешь готовый патч — сначала
  `git -C external/tracktion_engine checkout -- .`. В `git status` подмодуль с патчем не виден (`ignore = dirty`).
- Повтор аудиоклипа по кругу и кроссфейды на наложении — через `core::audio_clips` (`setLooping`,
  `setCrossfades`); `core::clips::isLooping` — только про MIDI-клипы. Где клип рисуется или пересчитывается
  из долей в секунды файла — учитывать петлю (`getLoopFileSeconds`, `getLoopSongStartBeat`).
- Темп и первая доля звука измеряются по самому звуку: `core::audio_tempo::measure` (анализатор —
  `core::beat_finder`, без движка: удары, сетка шестнадцатых, ближайшая к темпу песни «октава»). Follow tempo —
  `ArrangementModel::setFollowsTempo` (меряет, ставит клип на первую долю `startOnBeat`, возвращает, что сказать
  пользователю); сдвиг звука под сеткой — `audio_tempo::slide`.
- Растяжение и высота аудиоклипа — только через `core::audio_tempo`. Каждый новый аудиоклип и каждый клип
  открытого проекта — через `core::audio_clips::prepareForPlayback`: иначе движок готовит копии файла в фоне
  (растянутую, или WAV для FLAC/Ogg/MP3), а экспорт ждёт их вечно. Обычный клип играет путём движка (без него
  в тестах `AudioRig` он молчит).
- Сборки — в `build/` (не в git). Пробные программы — в `spikes/`, их выводы — в `docs/research/`.

## Git
- Коммитить по завершении проверенного шага, с понятным сообщением; затем `git push`.
- Не коммитить аудио, сэмплы и веса моделей (см. `.gitignore`).
- GitHub CLI: `C:\Program Files\GitHub CLI\gh.exe` (вход выполнен, аккаунт Mapelisin).

## Выпуски для пользователей (обязательно, решение 33)
- **После каждого пятого этапа** (M60, M65, M70, …) — выпуск на странице Releases: установщик, который скачивается
  одной кнопкой. Этап, номер которого делится на 5, не закончен, пока выпуск не опубликован. Пропущенный выпуск
  делается сразу, как это замечено.
- Установщик — только из зелёного запуска CI на коммите этапа (artifact **Harmonia-Setup**): CI собирает с нуля,
  гоняет тесты, ставит и удаляет установщик. Скачать: `gh run download <id> -n Harmonia-Setup -D <папка>`.
  Если запуск этапа отменён следующим коммитом в `main` (так бывает: новый push отменяет прежний запуск) —
  сначала тег на коммит этапа (`git tag v0.<этап>.0 <коммит>`, `git push origin refs/tags/v0.<этап>.0`), потом
  `gh workflow run ci.yml --ref v0.<этап>.0`: запуск на теге другим коммитам не мешает и ими не отменяется.
- Тег и имя — `v0.<этап>.0` (как версия в `CMakeLists.txt`), на коммите этапа:
  `gh release create v0.<этап>.0 <установщик> --target <коммит> --title "Harmonia 0.<этап>.0" --notes-file <файл>`.
- Описание выпуска — простыми словами, сначала по-английски, ниже то же по-русски под заголовком «По-русски»
  (решение 36): что нового с прошлого выпуска (по этапам из `docs/PROJECT.md`), как поставить (ссылка на
  `docs/user/install.md`, в русской части — на `docs/user/install.ru.md`), предупреждение Windows о неподписанном
  установщике.
- После выпуска — запись в журнале сессий `docs/PROJECT.md`; README и картинки `docs/images` обновить, если программа
  заметно изменилась.
