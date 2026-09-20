# SQLite (vendored)

Официальный amalgamation-дистрибутив SQLite, версия **3.49.1**.

Источник: <https://www.sqlite.org/2025/sqlite-amalgamation-3490100.zip>

## Зачем здесь

`CMakeLists.txt` собирает статическую библиотеку из исходника напрямую:

```cmake
add_library(gno_sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(gno_sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(gno_sqlite3 PRIVATE SQLITE_THREADSAFE=1 SQLITE_OMIT_LOAD_EXTENSION)
```

Сеть на этапе конфигурации не используется (`FetchContent`/`find_package` для SQLite нет),
поэтому файлы обязаны лежать в репозитории — без `sqlite3.c` сборка падает на этапе
`add_library` с «Cannot find source file». Используется в `src/core/sqlite_history_store.cpp`.

## Состав

| Файл | Нужен сборке | Назначение |
|---|---|---|
| `sqlite3.c` | да | реализация библиотеки |
| `sqlite3.h` | да | публичный API |
| `sqlite3ext.h` | нет | API расширений; часть штатной поставки, оставлен для полноты |
| `shell.c` | нет | CLI-утилита `sqlite3`; не собирается, оставлен для полноты поставки |

Определения при сборке: `SQLITE_THREADSAFE=1` (история сессий пишется из фоновых
потоков мониторинга) и `SQLITE_OMIT_LOAD_EXTENSION` (расширения не загружаются —
меньше поверхность атаки).

## Как обновить

1. Скачать свежий amalgamation с <https://www.sqlite.org/download.html>.
2. Заменить файлы здесь, обновить версию в этом README.
3. Пересобрать (`ninja -C C:/gno-build`) и прогнать тесты — `sqlite_history_store`
   покрыт кейсами на сохранение/чтение истории.
