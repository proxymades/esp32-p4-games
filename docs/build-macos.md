# Сборка и загрузка с macOS

Требуются ESP-IDF **5.4.4**, инструменты цели `esp32p4` и плата с 16 MB Flash / 32 MB PSRAM. Конфигурация рассчитана на ESP32-P4 revision 1.x.

## Установка SDK

Используйте [официальную инструкцию ESP-IDF 5.4.4 для macOS](https://docs.espressif.com/projects/esp-idf/en/v5.4.4/esp32p4/get-started/linux-macos-setup.html). При установке инструментов выберите `esp32p4`.

Скрипты поддерживают:

- Активированное окружение ESP-IDF или путь в `IDF_PATH`.
- Установку через Espressif Installation Manager.
- Стандартную установку в `$HOME/esp/esp-idf`.

Команды ниже выполняются из корня проекта. Для прямого вызова `idf.py`, `python` и `esptool` активируйте окружение в bash:

```bash
bash
. scripts/idf-env.sh
idf.py --version
```

Ожидаемая версия: `ESP-IDF v5.4.4`. Компонент `esp_codec_dev` 1.5.7 закреплён в `main/idf_component.yml` и `dependencies.lock`.

Исходники Gwenesis включены в репозиторий и проверяются по Git blob SHA-1. Для восстановления отсутствующих файлов закреплённого снимка:

```bash
python3 scripts/fetch_core.py
```

## Подготовка microSD

Карта должна иметь FAT32. Прошивка не форматирует карту автоматически.

Подготовьте собственный несжатый Mega Drive ROM с заголовком SEGA. Расширения исходного файла могут быть `.gen`, `.md` или `.bin`; ZIP и interleaved SMD не поддерживаются. Скопируйте его как:

```text
roms/comix_zone.bin
```

Например, при подключении карты к Mac через картридер:

```bash
mkdir -p "/Volumes/CARD_NAME/roms"
cp "/path/to/game.gen" "/Volumes/CARD_NAME/roms/comix_zone.bin"
shasum -a 256 "/Volumes/CARD_NAME/roms/comix_zone.bin"
```

Извлеките карту средствами macOS и установите её в выключенную плату. Программа читает `/sd/roms/comix_zone.bin`, проверяет заголовок, размер и reset vector, выводит SHA-256 и загружает ROM в PSRAM. Затем карта размонтируется и её питание выключается.

Поддержка длинных имён включена через `CONFIG_FATFS_LFN_HEAP=y`; она нужна для имени `comix_zone.bin`.

## Выбор порта и резервная копия

Подключите штатный USB-C и найдите порт платы:

```bash
ls /dev/cu.usb*
port=/dev/cu.usbmodemXXXXXXXXXXX
python -m esptool --chip esp32p4 -p "$port" --no-stub flash_id
```

Замените пример реальным портом. Команда идентификации вызывает перезапуск.

Перед заменой имеющейся прошивки её можно сохранить локально:

```bash
mkdir -p .private/backups/manual
python -m esptool --chip esp32p4 -p "$port" read_flash 0x0 0x1000000 .private/backups/manual/flash-16MiB.bin
shasum -a 256 .private/backups/manual/flash-16MiB.bin
```

## Игра с карты

Подключите дисплей, кнопки и динамик по [таблице](hardware-plan.md), установите microSD, затем соберите и запишите приложение:

```bash
bash scripts/build.sh play
bash scripts/flash.sh play "$port"
```

Скрипт записи проверяет режим сборки и занятость порта. Перед записью закройте монитор, использующий тот же порт.

В нормальном запуске ожидаются `CODEC_I2C PASS`, `PINMUX_CHECK PASS`, `ROM_LOADED` с размером и SHA-256, затем `EMU_START diagnostic=0`. Нажмите Start для начала игры.

Если карты или файла нет, программа сообщает об ошибке и остаётся в ожидании. После установки карты нужен Reset. Автоматические повторы и форматирование отсутствуют.

Если вход в загрузчик не срабатывает, удерживайте BOOT, нажмите Reset, отпустите BOOT и повторите запись.

## Настройки

После первой сборки:

```bash
idf.py -B build-play -DSDKCONFIG="$PWD/build-play/sdkconfig" menuconfig
```

Параметры находятся в разделе **P4-NANO Games**:

| Параметр | Значение по умолчанию |
| --- | --- |
| `P4GAME_AUDIO_VOLUME` | 95%, общий уровень музыки и эффектов |
| `P4GAME_LCD_CLOCK_HZ` | 20 000 000 Hz |
| `P4GAME_ROM_PATH` | `/sd/roms/comix_zone.bin` |

Пересоберите и запишите приложение после изменения настроек.

Каждый режим имеет отдельный каталог и `sdkconfig`: `build-play`, `build-board-check`, `build-peripheral-test`. Изменения defaults не заменяют уже сохранённую конфигурацию. Для новой конфигурации можно использовать отдельную папку:

```bash
P4GAME_BUILD_DIR=build-play-new bash scripts/build.sh play
P4GAME_BUILD_DIR=build-play-new bash scripts/flash.sh play "$port"
```

## Журнал и диагностика

```bash
python scripts/capture_serial.py --port "$port" --output .private/play.log --seconds 30 --reset
```

Опция `--reset` перезапускает плату. Подробные режимы и ожидаемые результаты описаны в [порядке проверки](testing.md).

## Локальный объединённый образ

При необходимости можно создать единый файл обычной прошивки:

```bash
python scripts/package.py --build build-play
(cd firmware && shasum -a 256 -c SHA256SUMS)
python -m esptool --chip esp32p4 -p "$port" -b 460800 write_flash 0x0 firmware/p4nano-games.bin
```

Упаковщик принимает только режим `play`, проверяет отсутствие символов встроенного тестового ROM в ELF и создаёт `firmware/manifest.json` с хешами частей. Каталог `firmware/` локальный и исключён из Git.

## Восстановление локальной копии Flash

Выберите копию для той же платы и проверьте её сохранённую контрольную сумму. Для образа полного Flash:

```bash
python -m esptool --chip esp32p4 -p "$port" -b 460800 write_flash 0x0 .private/backups/manual/flash-16MiB.bin
```

Операции проекта не записывают eFuse и не обновляют Flash ESP32-C6.
