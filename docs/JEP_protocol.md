# JEP — JSON Exchange Protocol
## Расширенная спецификация обмена с Jetson

**Версия:** 1.3  
**Дата:** 21 октября 2026  

Назначение: единый канал управления нагрузками (камера, гироплатформа, дальномер, видеопоток, трекинг) через Jetson. Прямой обмен с нагрузками со стороны хоста исключается.

## 1. Общие положения

### 1.1. Формат пакета

```
Header (4 байта ASCII) | Size (4 байта BE) | MD5 hex (32 байта) | Unitime (8 байт BE, мс) | JSON Data (переменный)
```

- **Header** — идентификатор типа команды (4 символа ASCII).

**Используемые заголовки:**
- **MDPL** — MediaPlayer control (управление видеопотоком)
- **CAPT** — Capture / tracking (сопровождение цели)
- **CAMR** — Camera (VISCA MC-108-M3)
- **GYRO** — гироплатформа (SimpleBGC)
- **RNGF** — лазерный дальномер

- Size — длина JSON Data в байтах (big-endian).
- MD5 — hex-представление хеша: `PRIVATE_KEY + Unitime_BE + JSON Data`.
- PRIVATE_KEY = `"my_secret_jep_key_2026"`.
- Unitime — время в миллисекундах (big-endian uint64).
- JSON Data — тело команды или ответа в формате JSON (UTF-8).

### 1.2. Общие правила

- Все запросы от клиента содержат поле `"command"`.
- Все ответы от Jetson содержат поле `"stat"`.
- Неизвестная команда → `{ "stat": "error", "msg": "unknown command" }`.
- Ошибка связи с нагрузкой → `{ "stat": "error", "device": "<имя>", "msg": "timeout" | "checksum" | ... }`.
- Для команд без возвращаемых данных достаточно ответа `{ "stat": "ack", "command": "<имя>" }`.
- Jetson самостоятельно формирует, отправляет и разбирает нативные протоколы нагрузок.
- Периодические данные могут присылаться Jetson по собственной инициативе или по запросу `"status"`.

## 2. Существующие заголовки

### 2.1. MDPL — MediaPlayer control

Управление запуском/остановкой стрима и параметрами кодирования. Команды камеры вынесены в CAMR.

**Команды клиента → Jetson**

- Запуск стрима: `{ "command": "play", "play_ip": "192.168.137.100", "play_port": 5000 }`
- Остановка: `{ "command": "stop" }`
- Параметры: `{ "command": "set", "bitrate": 2500000, "resolut": "HD" }`
- Статус оборудования: `{ "command": "status" }`
- Синхронизация времени: `{ "command": "sys_time", "unitime": 1729500000 }`

**Ответы Jetson → клиент**

- `{ "stat": "set" }` / `"stop"` / `"play"`
- `{ "stat": "status", "cpu_use": 45, "temp_cpu": 62, ... }`
- `{ "stat": "sys_time" }`

### 2.2. CAPT — Capture / tracking

**Команды**

- Установка параметров: `{ "command": "set", "TRACK_CMD": 1, "VIDEO_CHANEL": 1, "STROB_X_POS": 0, ... }`
- Запрос: `{ "command": "get_set" }`

**Ответы**

- `{ "stat": "set" }`
- `{ "stat": "get_set", "TRACK_STATUS": 1, ... }`

## 3. Новые заголовки

### 3.1. CAMR — камера (VISCA)

**Команды и текущие эквиваленты (VISCA)**

- `zoom_in` → `81 01 04 07 02 FF`
- `zoom_out` → `81 01 04 07 03 FF`
- `zoom_stop` → `81 01 04 07 00 FF`
- `zoom_pos` (index или pos) → готовый пакет из конфигурации
- `autofocus` → `81 01 04 38 02 FF`
- `focus_infinity` → `81 01 04 48 0F 0F 0F 0F FF`
- `brightness_up` / `brightness_down` → AE Bright + шаг
- `ae_bright` → `81 01 04 39 0D FF`
- `tv_fps25` / `tv_fps30` → запись регистров
- `tv_pwr_reset` → `81 01 04 00 03 FF`
- `zoom_inquiry` → `81 09 04 47 FF`
- `status`

### 3.2. GYRO — гироплатформа (SimpleBGC v1, старт 0x3E)

- `speed` → CMD_CONTROL (67), mode=1 (SPEED)
- `heading` → mode=5 (ANGLE_REL_FRAME)
- `stab_null` / `angle` → mode=2 (ANGLE)
- `home` → CMD_EXECUTE_MENU (69), payload=18
- `motors` → CMD_MOTORS_ON (77) / OFF (109)
- `reset` → CMD_RESET (114)
- `status` → CMD_REALTIME_DATA_4 (25)

### 3.3. RNGF — дальномер

Формат: `55 CMD LEN DATAH DATAL CHK`

- `shoot` → `55 01 02 00 00 56`
- `stop` → `55 00 02 00 00 57`
- `continuous` → cmd=0x02
- `selftest` → `55 03 02 00 00 54`
- `set_min_dist` → cmd=0x04
- `set_target_mode` → cmd=0x22
- `status`

**Удалено:** команда установки скорости UART (опасная).

## 4. Примечания

Документ является черновиком для согласования и реализации на Jetson.  
Полная версия с подробными комментариями — в файле `JEP_расширенная_спецификация.docx` (локально).
