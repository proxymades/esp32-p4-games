# Сторонние компоненты

Ядро Gwenesis взято из [ESP32_P4_Launcher](https://github.com/giltal/ESP32_P4_Launcher/tree/7d16abd2ea4497e610dd9e5cbf88090cd2521755/apps/genesis/core), commit `7d16abd2ea4497e610dd9e5cbf88090cd2521755`. Исходный README указывает снимок bzhxx/gwenesis `168e466`.

Оригинальные файлы сохранены в `components/gwenesis/upstream/`; их Git blob SHA-1 проверяется по `upstream.json`. Скрипт `prepare_core.py` создаёт исправленную копию в каталоге сборки. Изменения описаны в [PORTING.md](components/gwenesis/PORTING.md).

В дереве ядра сохранён файл GNU GPL v3 [LICENSE](components/gwenesis/upstream/LICENSE). Отдельные исходники содержат собственные уведомления, включая Musashi/M68000 Карла Стенеруда, Z80 Марата Файзуллина, YM2612 и SN76489. В исходниках CPU присутствуют ограничения коммерческого использования. Оригинальные уведомления сохранены; единая разрешительная лицензия всему набору не назначается.

ESP-IDF 5.4.4 и `espressif/esp_codec_dev` 1.5.7 используются со своими уведомлениями. Версия и хеш компонента закреплены в `dependencies.lock`.

ROM-образы коммерческих игр и бинарные образы прошивок в репозитории не распространяются.
