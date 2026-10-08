# dnslab — примеры

Сохранённые примеры работы `dnslab`: консольный вывод CLI и скриншоты
Android-приложения.

## CLI (`cli/`)

| Файл | Команда | Что показывает |
|------|---------|----------------|
| [compose-request-bebra.shma.txt](cli/compose-request-bebra.shma.txt) | `dnslab --compose-request bebra.shma` | Сборка A-запроса без отправки в сеть: hex dump, разбор заголовка и секции Question, ASCII-схемы пакета (28 байт) |

## Android (`android/`)

Пока пусто, скриншоты появятся после реализации UI
(см. [docs/android-tech-spec.md § 4](../docs/android-tech-spec.md#4-экранные-формы-uiux)).

## Соглашения

- **CLI:** `cli/<режим>-<домен>.txt`, например `lookup-example.com.txt`,
  `details-example.com.txt`.
- **Android:** `android/<экран>-<состояние>.png`, например
  `lookup-success.png`, `details-hex.png`.
- Каждый новый файл добавляем строкой в таблицу выше.
