
# AndroidBackend

Desktop backend-приложение на языке C++ для Android-проекта сбора GPS-координат и параметров мобильной сети.

Приложение принимает данные со смартфона через ZeroMQ, сохраняет их в `.json` файл и отображает информацию в интерфейсе ImGui.

---

# Возможности

- Приём GPS-данных от Android-приложения через ZeroMQ
- Работа backend-сервера в отдельном потоке
- Графический интерфейс ImGui
- Отображение:
  - Latitude
  - Longitude
  - Altitude
  - Accuracy
  - LTE PCI
  - TAC
  - EARFCN
  - RSRP
  - RSRQ
  - RSSI
  - SINR
- Сохранение полученных данных в `location.json`
- Realtime обновление интерфейса

---

# Используемые технологии

- C++17
- ZeroMQ
- SDL2
- OpenGL
- ImGui
- CMake

---

# Структура проекта

```text
src/
 └── main.cpp

third_party/
 └── imgui/

CMakeLists.txt
````

---

# Сборка macOS (Apple Silicon)

## Установка зависимостей

```bash
brew install sdl2
brew install zeromq
brew install cppzmq
brew install cmake
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
./phone_monitor
```

---

# Формат входящего JSON

```json
{
    "latitude": 55.0415,
    "longitude": 82.9346,
    "altitude": 164.2,
    "accuracy": 5.0,
    "time": "2026-02-20 21:10:00",

    "pci": 13,
    "tac": 2451,
    "earfcn": 1300,

    "rsrp": -92,
    "rsrq": -11,
    "rssi": -67,
    "sinr": 18
}
```

---

# Архитектура

Приложение состоит из двух потоков.

## GUI поток

Отвечает за:

* ImGui интерфейс
* отображение телеметрии
* realtime обновление данных

## Server поток

Отвечает за:

* ZeroMQ сервер
* получение данных
* сохранение JSON
* обновление общей структуры данных

---

# Взаимодействие потоков

Потоки используют общую структуру:

```cpp
struct Location
```

Доступ к структуре синхронизирован через:

```cpp
std::mutex
```

---

# Особенности реализации под macOS

На macOS GUI поток должен запускаться только в main thread.

Поэтому:

* ImGui + SDL2 работают в `main()`
* ZMQ сервер работает в отдельном `std::thread`

---

# Лабораторная работа №10

Реализовано:

* Android CellInfo
* LTE параметры сети
* GPS координаты
* многопоточность
* JSON логирование
* графический интерфейс
* realtime обновление данных

```
```
