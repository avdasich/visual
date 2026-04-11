
# AndroidBackend

Desktop backend-приложение на языке C++ для Android-проекта сбора GPS-координат, параметров мобильной сети и проведения drive-test анализа.

Приложение принимает телеметрию со смартфона через ZeroMQ, сохраняет данные в `.json` файл, записывает информацию в PostgreSQL и отображает данные в интерфейсе ImGui/ImPlot.

---

# Возможности

- Приём GPS и телеметрии мобильной сети через ZeroMQ
- Работа backend-сервера в отдельном потоке
- Графический интерфейс на ImGui
- Построение графиков через ImPlot
- Отображение маршрута движения
- Отображение LTE / GSM / NR параметров
- Загрузка и анализ накопленных `.json` файлов
- Realtime обновление данных
- PostgreSQL интеграция
- Сохранение телеметрии в базу данных
- Multi-PCI графики LTE
- Подготовка данных для drive-test

---

# Используемые технологии

- C++20
- ZeroMQ
- PostgreSQL
- SDL2
- OpenGL
- ImGui
- ImPlot
- CMake

---

# Структура проекта

```text
src/
├── database.cpp
├── database.h
├── gui.cpp
├── gui.h
├── json_parser.cpp
├── json_parser.h
├── main.cpp
├── server.cpp
├── server.h
└── types.h

third_party/
├── imgui/
└── implot/
````

---

# Сборка macOS (Apple Silicon)

## Установка зависимостей

```bash
brew install sdl2
brew install zeromq
brew install cppzmq
brew install postgresql@14
brew install cmake
```

---

# Установка библиотек

```bash
cd third_party

git clone https://github.com/ocornut/imgui.git
git clone https://github.com/epezent/implot.git
```

---

# Настройка PostgreSQL

Запуск PostgreSQL:

```bash
brew services start postgresql@14
```

Создание базы данных:

```bash
createdb phone_monitor
```

---

# Сборка проекта

```bash
mkdir build
cd build

cmake ..
make
```

---

# Запуск

```bash
./main
```

---

# Формат входящего JSON

```json
{
    "location": {
        "latitude": 55.0415,
        "longitude": 82.9346,
        "altitude": 164.2,
        "accuracy": 5.0
    },

    "telephony": [
        {
            "type": "LTE",

            "CellIdentityLte": {
                "PCI": 13,
                "TAC": 2451,
                "EARFCN": 1300
            },

            "CellSignalStrengthLte": {
                "RSRP": -92,
                "RSRQ": -11,
                "RSSI": -67,
                "RSSNR": 18
            }
        }
    ]
}
```

---

# Архитектура

Приложение состоит из двух потоков.

## GUI поток

Отвечает за:

* ImGui интерфейс
* ImPlot графики
* отображение маршрута
* realtime визуализацию телеметрии
* загрузку и анализ `.json` файлов

GUI работает в `main thread`.

---

## Server поток

Отвечает за:

* ZeroMQ сервер
* получение телеметрии
* сохранение JSON
* сохранение данных в PostgreSQL
* обновление общей структуры данных
* накопление истории сигналов

Server поток запускается через:

```cpp
std::thread
```

---

# PostgreSQL

Приложение автоматически:

* подключается к PostgreSQL
* создаёт таблицу `telemetry`
* сохраняет входящую телеметрию

В базу данных сохраняются:

* GPS координаты
* LTE/GSM/NR параметры
* traffic statistics
* raw JSON пакеты

---

# Поддерживаемые технологии связи

## LTE

Отображаются:

* PCI
* EARFCN
* TAC
* Band
* RSRP
* RSRQ
* RSSI
* SINR
* Timing Advance

---

## GSM

Отображаются:

* CI
* ARFCN
* LAC
* Dbm

---

## NR (5G)

Отображаются:

* PCI
* SS-RSRP
* SS-RSRQ
* SS-SINR

---

# Графики ImPlot

Приложение строит:

* маршрут движения устройства
* LTE RSRP
* LTE RSRQ
* LTE RSSI
* LTE SINR
* GSM Dbm
* NR SS-RSRP

Для LTE поддерживается отображение нескольких PCI одновременно.

Каждый PCI:

* отображается отдельной линией
* имеет собственный цвет
* автоматически добавляется в легенду ImPlot

---

# Работа с JSON файлами

Приложение поддерживает загрузку накопленного `.json` файла.

После загрузки:

* восстанавливается маршрут
* восстанавливается история сигналов
* строятся графики drive-test

---

# Особенности реализации под macOS

На macOS GUI поток должен запускаться только в main thread.

Поэтому:

* ImGui + SDL2 работают в `main()`
* ZMQ сервер работает в отдельном `std::thread`

---

# Лабораторная работа №13

Реализовано:

* PostgreSQL интеграция
* автоматическое создание таблицы telemetry
* сохранение телеметрии в БД
* realtime накопление истории сигналов
* multi-PCI LTE графики
* LTE/GSM/NR визуализация
* JSON logging
* drive-test подготовка
* backend на C++ + ZeroMQ
* ImGui + ImPlot интерфейс

```
