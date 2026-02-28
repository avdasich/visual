# AndroidBackend

Desktop backend-приложение на языке C++ для Android-проекта сбора GPS-координат.

Приложение принимает координаты смартфона через ZeroMQ, сохраняет данные в `.json` файл и отображает местоположение в интерфейсе ImGui.



# Возможности

- Приём GPS-данных от Android-приложения через ZeroMQ
- Работа backend-сервера в отдельном потоке
- Графический интерфейс ImGui
- Отображение:
  - Latitude
  - Longitude
  - Altitude
- Сохранение полученных данных в `location.json`
- Realtime обновление интерфейса



# Используемые технологии

- C++17
- ZeroMQ
- SDL2
- OpenGL
- ImGui
- CMake



# Структура проекта

```text
src/
 └── main.cpp

third_party/
 └── imgui/

CMakeLists.txt