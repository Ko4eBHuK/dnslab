# Техническое задание: Android-приложение «DNS Resolver»

> **Пет-проект**: нативное Android-приложение на базе библиотеки `dnslab` (C17)  
> **Дата**: 2026-07-16  
> **Версия ТЗ**: 1.0

---

## 1. Общие сведения

### 1.1 Назначение

Приложение **«DNS Resolver»** — образовательный DNS-клиент для Android, использующий
библиотеку `dnslab` через JNI. Позволяет вручную формировать DNS-запросы, отправлять
их на указанный сервер и визуализировать сырые DNS-пакеты (hex, структура, схемы).

**UI-фреймворк**: весь пользовательский интерфейс реализуется на **Jetpack Compose**
(декларативный UI-тулкит Android). Никаких XML-вёрсток (`LinearLayout`, `ConstraintLayout`
и т.п.) — только `@Composable`-функции, Material 3, Compose Navigation.

Приложение ориентировано на:
- Системных администраторов, желающих «посмотреть под капот» DNS на мобильном устройстве.
- Студентов и изучающих сетевые протоколы.
- Разработчиков, отлаживающих DNS-конфигурации.

### 1.2 Границы проекта

| Входит | Не входит |
|--------|-----------|
| Разрешение A (IPv4) и AAAA (IPv6) | DNSSEC, EDNS0 |
| UDP-запросы на порт 53 | DNS-over-TLS, DNS-over-HTTPS |
| Hex-dump и 3 ASCII-схемы пакета | TCP-fallback при TC-флаге |
| Ручной ввод DNS-сервера | Автоопределение через DHCP/систему |
| Локальная история запросов | Экспорт/импорт pcap |
| Тёмная тема (Android 10+) | Виджеты |
| One-shot запросы (без фонового режима) | Push-уведомления, фоновая синхронизация |

### 1.3 Целевая платформа

| Параметр | Значение |
|----------|----------|
| ОС | Android 8.0 (API 26) и выше |
| Архитектуры | `arm64-v8a`, `x86_64` (эмулятор) |
| Язык UI | Kotlin + Jetpack Compose (декларативный UI) |
| UI Framework | Jetpack Compose + Material 3 (никакого XML) |
| Язык нативного слоя | C17 (библиотека `dnslab`) |
| Связка Kotlin ↔ C | JNI (Android NDK) |
| Сборка нативного кода | CMake + `externalNativeBuild` |

---

## 2. Архитектура приложения

### 2.1 Слои приложения

```
┌─────────────────────────────────────────────────┐
│                   UI Layer                       │
│         Jetpack Compose (Kotlin)                 │
│  ┌───────────┐ ┌──────────┐ ┌────────────────┐  │
│  │ MainScreen│ │ResultScreen│ │PacketViewScreen│  │
│  └───────────┘ └──────────┘ └────────────────┘  │
├─────────────────────────────────────────────────┤
│               ViewModel Layer                    │
│         Kotlin ViewModels + StateFlow            │
│  ┌──────────────────────────────────────────┐   │
│  │        DnsResolverViewModel               │   │
│  │  state: DnsUiState, query(), compose()    │   │
│  └──────────────────────────────────────────┘   │
├─────────────────────────────────────────────────┤
│               Domain Layer                       │
│         Kotlin (чистая бизнес-логика)            │
│  ┌──────────────────────────────────────────┐   │
│  │  DnsQuery, DnsResponse, DnsPacketView     │   │
│  │  DnsRepository (интерфейс)                │   │
│  │  HistoryRepository (интерфейс)            │   │
│  └──────────────────────────────────────────┘   │
├─────────────────────────────────────────────────┤
│               Data Layer                         │
│  ┌──────────────────┐ ┌─────────────────────┐   │
│  │ DnsNativeBridge   │ │ HistoryLocalStorage │   │
│  │ (JNI-обёртка)     │ │ (Room DB)           │   │
│  └──────┬───────────┘ └─────────────────────┘   │
├─────────┼───────────────────────────────────────┤
│         │          Native Layer (C17)            │
│  ┌──────┴───────────────────────────────────┐   │
│  │  libdnslab.so                            │   │
│  │  engine/dns.c  net/net.c                 │   │
│  │  decor/print.c command/lookup.c          │   │
│  │  command/compose.c command/details.c     │   │
│  └──────────────────────────────────────────┘   │
└─────────────────────────────────────────────────┘
```

### 2.2 JNI-прослойка (Native Bridge)

JNI-прослойка — это новый C-файл `src/jni/dnslab_bridge.c`, который:

1. Принимает вызовы из Kotlin с параметрами запроса.
2. Дёргает внутренние функции `dnslab`.
3. Возвращает структурированный результат (JSON или плоскую структуру) обратно в Kotlin.

Текущий публичный API `dnslab` **непригоден для прямого JNI-использования**,
так как функции `print_*` пишут в `stdout`. Поэтому JNI-прослойка должна:

- Вызывать `dns_build_query()` и `dns_parse_response()` напрямую.
- Заменить вызовы `printf` на запись в строковые буфера (snprintf-подход).
- Предоставить плоский C-интерфейс, удобный для JNI.

### 2.3 Схема навигации

```
                    ┌─────────────────┐
                    │   Главный экран  │
                    │  (форма запроса) │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
     ┌────────────┐ ┌────────────┐ ┌────────────┐
     │  Результат │ │  Compose   │ │  Details   │
     │  (lookup)  │ │  (без сети)│ │  (полный)  │
     └─────┬──────┘ └─────┬──────┘ └─────┬──────┘
           │              │              │
           ▼              ▼              ▼
     ┌─────────────────────────────────────────┐
     │        Просмотр пакета (packet view)     │
     │  Hex dump / Structured / 3 ASCII modes   │
     └─────────────────────────────────────────┘
```

---

## 3. Функциональные требования

### 3.1 Режимы работы

#### FR-01: Режим Lookup (основной)
**Приоритет**: P0 — MVP

Пользователь вводит домен, выбирает тип записи (A / AAAA), нажимает «Разрешить».

- Приложение формирует DNS-запрос, отправляет через UDP на DNS-сервер.
- При успехе — показывает список IP-адресов с TTL.
- При ошибке (NXDOMAIN, таймаут, и т.д.) — показывает понятное сообщение.
- Поддерживается флаг `--verbose`: расширенный вывод с заголовками ответа, RCODE, флагами.

#### FR-02: Режим Compose (без сети)
**Приоритет**: P0 — MVP

Пользователь вводит домен, выбирает тип записи, нажимает «Собрать запрос».

- Без отправки по сети. Формируется DNS-пакет и отображается:
  - Hex dump (смещение | hex | ASCII).
  - Структурированный разбор полей.
  - Три ASCII-визуализации (схема, бинарный, человекочитаемый).
- У каждой схемы — переключатель (Tab/SegmentedControl).

#### FR-03: Режим Details (полный трассировка)
**Приоритет**: P1

Комбинация Compose + отправка по сети с логированием каждого шага.

- Отображает попытки отправки: «Attempt 1/3: sending to 8.8.8.8:53».
- Hex dump запроса → hex dump ответа → структурированный разбор → результат.
- Итог: `example.com → 93.184.216.34`.
- Подсказка: `Hint: compare with dig +short example.com A`.

### 3.2 Настройки запроса

#### FR-04: Выбор DNS-сервера
**Приоритет**: P0

- Поле ввода IP-адреса DNS-сервера.
- По умолчанию: `8.8.8.8` (Google Public DNS).
- Кнопка «Сбросить на默认».
- Валидация IP-адреса (IPv4/IPv6) на клиенте.

#### FR-05: Таймаут и повторные попытки
**Приоритет**: P1

- Слайдер/поле ввода таймаута (500–10000 мс, по умолчанию 2000).
- Слайдер/поле ввода количества повторных попыток (0–5, по умолчанию 2).
- Отображаются в разделе «Advanced settings» (свернут по умолчанию).

#### FR-06: Тип записи
**Приоритет**: P0

- Переключатель A / AAAA (IPv4 / IPv6).
- В будущем (v2): MX, NS, CNAME, TXT, SOA, ANY — после доработки библиотеки.

#### FR-07: Порт сервера
**Приоритет**: P2

- По умолчанию `53`.
- Возможность изменить для нестандартных DNS-серверов.

### 3.3 История запросов

#### FR-08: Локальная история
**Приоритет**: P1

- Автоматическое сохранение всех выполненных запросов в локальную БД (Room).
- Каждая запись: домен, тип записи, сервер, временная метка, результат (IP/ошибка).
- Список истории на главном экране (последние 20).
- Тап по записи → повтор запроса с теми же параметрами.
- Свайп для удаления записи.
- Кнопка «Очистить историю».

### 3.4 Копирование и шейринг

#### FR-09: Копирование результатов
**Приоритет**: P2

- Долгий тап по IP-адресу → копировать в буфер обмена.
- Кнопка «Copy all» для всего вывода.
- Кнопка «Share» → системный intent с текстом результата.

---

## 4. Экранные формы (UI/UX)

> **Все экраны реализуются на Jetpack Compose (Material 3).**
> Никакого XML/Legacy Views — только `@Composable`-функции,
> `Modifier`, `LazyColumn`, `AnimatedVisibility` и другие compose-API.

### 4.1 Главный экран (MainScreen)

```
┌──────────────────────────────────────┐
│  Status Bar                           │
├──────────────────────────────────────┤
│  DNS Resolver                    ⚙️   │
│                                      │
│  ┌──────────────────────────────┐    │
│  │  example.com          [ ✕ ]  │    │
│  └──────────────────────────────┘    │
│                                      │
│  ○ A (IPv4)    ● AAAA (IPv6)        │
│                                      │
│  ┌──────────────────────────────┐    │
│  │  DNS Server: 8.8.8.8        │    │
│  └──────────────────────────────┘    │
│                                      │
│  ▶ Advanced settings                │
│                                      │
│  ┌──────────┐ ┌──────────┐         │
│  │ РАЗРЕШИТЬ│ │ СОБРАТЬ  │         │
│  │ (lookup) │ │(compose) │         │
│  └──────────┘ └──────────┘         │
│                                      │
│  ── История ──────────────────────  │
│  ┌──────────────────────────────┐    │
│  │ example.com → 93.184.216.34 │    │
│  │ A • 8.8.8.8 • 5 мин назад   │    │
│  ├──────────────────────────────┤    │
│  │ google.com → NXDOMAIN       │    │
│  │ AAAA • 1.1.1.1 • 12 мин     │    │
│  └──────────────────────────────┘    │
│                                      │
└──────────────────────────────────────┘
```

«Advanced settings» в раскрытом виде:

```
  ▼ Advanced settings
  ┌──────────────────────────────┐
  │ Timeout: ───●─── 2000 ms    │
  │ Retries: ──●──── 2          │
  │ Port:      [ 53 ]           │
  └──────────────────────────────┘
```

### 4.2 Экран результата (ResultScreen) — Lookup

```
┌──────────────────────────────────────┐
│  ← Назад       example.com          │
├──────────────────────────────────────┤
│                                      │
│  ✅ Успешно (RCODE: NOERROR)        │
│                                      │
│  ┌──────────────────────────────┐    │
│  │  93.184.216.34              │    │
│  │  TTL: 300s          [📋]    │    │
│  ├──────────────────────────────┤    │
│  │  93.184.216.35              │    │
│  │  TTL: 300s          [📋]    │    │
│  └──────────────────────────────┘    │
│                                      │
│  ┌──────────────────────────────┐    │
│  │    [ Просмотреть пакет ]     │    │
│  └──────────────────────────────┘    │
│                                      │
│  ┌──────┐ ┌──────────┐             │
│  │Коп-ть│ │Поделиться│             │
│  └──────┘ └──────────┘             │
│                                      │
└──────────────────────────────────────┘
```

### 4.3 Экран ошибки

```
┌──────────────────────────────────────┐
│  ← Назад       example.com          │
├──────────────────────────────────────┤
│                                      │
│           ⚠️                         │
│     Домен не существует              │
│       (NXDOMAIN)                     │
│                                      │
│  Сервер 8.8.8.8:53                  │
│  Время запроса: 342 мс               │
│                                      │
│  ┌──────────────────────────────┐    │
│  │       [ Попробовать снова ]   │    │
│  └──────────────────────────────┘    │
│                                      │
└──────────────────────────────────────┘
```

Возможные состояния ошибок и их отображение:

| Ошибка | Заголовок | Текст |
|--------|-----------|-------|
| NXDOMAIN | `Домен не существует` | Домен `...` не найден в DNS. Проверьте правильность имени. |
| TIMEOUT | `Сервер не отвечает` | DNS-сервер `...` не ответил за `...` мс. Проверьте соединение. |
| REFUSED | `Запрос отклонён` | Сервер `...` отклонил запрос. Возможно, сервер не принимает внешние запросы. |
| BAD_SERVER | `Неверный адрес сервера` | Не удалось разрешить адрес `...`. Проверьте IP-адрес. |
| SOCKET | `Ошибка сокета` | Не удалось создать сетевой сокет. |
| SEND | `Ошибка отправки` | Не удалось отправить пакет. Проверьте подключение к сети. |
| TRUNCATED | `Ответ повреждён` | Получен некорректный ответ от сервера. |

### 4.4 Экран просмотра пакета (PacketViewScreen) — Compose

```
┌──────────────────────────────────────┐
│  ← Назад      Packet: example.com   │
├──────────────────────────────────────┤
│  [ Hex ] [ Struct ] [ Schema0 ][1][2]│
│                                      │
│  ┌──────────────────────────────┐    │
│  │ OFFSET  HEX           ASCII  │    │
│  │ ------  ------------- -------│    │
│  │ 0x0000  12 34 01 00   .4..  │    │
│  │ 0x0004  00 01 00 00   ....  │    │
│  │ 0x0008  00 00 00 00   ....  │    │
│  │ 0x000C  07 65 78 61   .exa  │    │
│  │ 0x0010  6d 70 6c 65   mple  │    │
│  │ 0x0014  03 63 6f 6d   .com  │    │
│  │ 0x0018  00 00 01 00   ....  │    │
│  │ 0x001C  01             .     │    │
│  └──────────────────────────────┘    │
│                                      │
└──────────────────────────────────────┘
```

Tab `Struct`:

```
┌──────────────────────────────────────┐
│  === DNS HEADER ===                  │
│  ID:              0x1234 (4660)     │
│  FLAGS:           0x0100            │
│    QR:            0 (Query)         │
│    OPCODE:        0 (Standard)      │
│    AA:            0                 │
│    TC:            0                 │
│    RD:            1 (Recursion)     │
│    RA:            0                 │
│    Z:             0                 │
│    RCODE:         0 (NoError)       │
│  QDCOUNT:         1                 │
│  ANCOUNT:         0                 │
│  NSCOUNT:         0                 │
│  ARCOUNT:         0                 │
│                                      │
│  === QUESTION SECTION ===            │
│  QNAME:           example.com       │
│  QTYPE:           1 (A)             │
│  QCLASS:          1 (IN)            │
└──────────────────────────────────────┘
```

Tab `Schema0` (схема, как RFC-диаграмма):

```
┌──────────────────────────────────────┐
│  0   1   2   3   4   5   6   7  ...  │
│ +---+---+---+---+---+---+---+---+    │
│ |              ID               |    │
│ +---+---+---+---+---+---+---+---+    │
│ |QR| OPCODE |AA|TC|RD|RA|Z|RCODE|    │
│ +---+---+---+---+---+---+---+---+    │
│           ...                        │
└──────────────────────────────────────┘
```

### 4.5 Экран Details (полный трассировка)

Прокручиваемый экран с секциями:

1. **Compose**: hex dump + structured + 3 схемы (как FR-02).
2. **Network**: логи попыток отправки.
3. **Response**: hex dump ответа + structured + схемы.
4. **Result**: итоговый IP / ошибка.

Каждая секция — раскрывающийся `ExpandableSection`.

### 4.6 Настройки (SettingsScreen)

```
┌──────────────────────────────────────┐
│  ← Назад       Настройки            │
├──────────────────────────────────────┤
│                                      │
│  Тема                                │
│  ○ Системная  ● Тёмная  ○ Светлая   │
│                                      │
│  По умолчанию                       │
│  ┌──────────────────────────────┐    │
│  │ DNS Server  8.8.8.8         │    │
│  │ Timeout     2000 ms         │    │
│  │ Retries     2               │    │
│  └──────────────────────────────┘    │
│                                      │
│  ── Данные ──────────────────────── │
│  [ Очистить историю ]               │
│                                      │
│  ── О приложении ────────────────── │
│  Версия: 1.0.0                      │
│  Библиотека: dnslab (C17)           │
│  Лицензия: MIT                      │
│                                      │
└──────────────────────────────────────┘
```

---

## 5. JNI-прослойка: спецификация

### 5.1 Проблемы текущего API `dnslab`

Текущий C-код `dnslab` использует `printf`/`stdout` для вывода, что неприемлемо
для Android-приложения. Необходимо:

1. **Убрать прямую печать** — заменить функции `print_*` на варианты,
   пишущие в строковый буфер (`snprintf`).
2. **Собрать всё в `.so`** — библиотека должна компилироваться под Android NDK.
3. **Обеспечить потокобезопасность** — запросы из Kotlin могут приходить
   с разных корутин.

### 5.2 Целевой C-интерфейс для JNI

```c
// dnslab_bridge.h — новый публичный заголовок для JNI

#ifndef DNSLAB_BRIDGE_H
#define DNSLAB_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#define DNSLAB_MAX_IP_COUNT     16
#define DNSLAB_MAX_PACKET_VIEW  65536
#define DNSLAB_MAX_ERROR_LEN    256

/* ---------- Типы ---------- */

typedef struct {
    char    ip[40];        // IPv4 или IPv6 строка
    int32_t ttl;
} dnslab_ip_t;

typedef struct {
    int32_t       rcode;
    int32_t       answer_count;
    dnslab_ip_t   answers[DNSLAB_MAX_IP_COUNT];
    int32_t       query_time_ms;
    char          error[DNSLAB_MAX_ERROR_LEN];  // пусто при успехе
} dnslab_result_t;

typedef struct {
    char    data[DNSLAB_MAX_PACKET_VIEW];
    int32_t length;
} dnslab_packet_view_t;

/* ---------- Операции ---------- */

/*
 * Выполнить DNS-запрос (lookup) — аналог cmd_lookup.
 * Возвращает 0 при успехе, -1 при ошибке.
 */
int dnslab_lookup(
    const char *domain,
    int         qtype,          // DNS_TYPE_A (1) или DNS_TYPE_AAAA (28)
    const char *server,         // IPv4/IPv6 адрес или NULL для 8.8.8.8
    int         port,
    int         timeout_ms,
    int         retries,
    int         verbose,
    dnslab_result_t *out
);

/*
 * Собрать DNS-запрос без отправки — аналог cmd_compose.
 * Заполняет packet_view представлениями пакета.
 */
int dnslab_compose(
    const char *domain,
    int         qtype,
    dnslab_packet_view_t *hex_out,         // hex dump
    dnslab_packet_view_t *structured_out,  // структурированный разбор
    dnslab_packet_view_t *scheme_out[3]    // 3 ASCII-схемы
);

/*
 * Полный трассировочный запрос — аналог cmd_details.
 * Заполняет compose-выходы + network_log + result.
 */
int dnslab_details(
    const char *domain,
    int         qtype,
    const char *server,
    int         port,
    int         timeout_ms,
    int         retries,
    dnslab_packet_view_t *hex_out,
    dnslab_packet_view_t *structured_out,
    dnslab_packet_view_t *scheme_out[3],
    dnslab_packet_view_t *network_log,
    dnslab_packet_view_t *response_hex,
    dnslab_packet_view_t *response_structured,
    dnslab_packet_view_t *response_scheme[3],
    dnslab_result_t      *out
);

#endif // DNSLAB_BRIDGE_H
```

### 5.3 Kotlin-обёртка (Native Bridge)

```kotlin
// DnsNativeBridge.kt — загрузка .so и объявление external-функций

object DnsNativeBridge {
    init {
        System.loadLibrary("dnslab")
    }

    // Структуры данных Kotlin (соответствуют C-структурам)

    data class DnsIp(
        val ip: String,
        val ttl: Int
    )

    data class DnsResult(
        val rcode: Int,
        val answers: List<DnsIp>,
        val queryTimeMs: Int,
        val error: String?
    )

    data class PacketView(
        val data: String,
        val length: Int
    )

    // JNI-методы

    external fun lookup(
        domain: String,
        qtype: Int,
        server: String?,
        port: Int,
        timeoutMs: Int,
        retries: Int,
        verbose: Int
    ): DnsResult

    external fun compose(
        domain: String,
        qtype: Int
    ): ComposeResult  // hex + structured + 3 schemes

    external fun details(
        domain: String,
        qtype: Int,
        server: String?,
        port: Int,
        timeoutMs: Int,
        retries: Int
    ): DetailsResult  // compose + network_log + response + result
}
```

### 5.4 Необходимые доработки C-библиотеки

| Задача | Файлы | Описание |
|--------|-------|----------|
| Замена `printf` на `snprintf` | `decor/print.c`, `command/*.c` | Все функции печати должны принимать `char *buf, size_t size` |
| Новый bridge-модуль | `src/jni/dnslab_bridge.c` | Плоский C-интерфейс поверх существующего API |
| JNI-обвязка | `app/src/main/cpp/dnslab_jni.c` | Регистрация native-методов, маршалинг строк/структур |
| CMakeLists для NDK | `app/src/main/cpp/CMakeLists.txt` | Сборка `libdnslab.so` |
| Удаление `main.c` из сборки | `CMakeLists.txt` | Android-приложение — не CLI, `main()` не нужен |
| Потокобезопасность | `net/net.c` | Убрать глобальное состояние, сделать всё per-call |

---

## 6. Модели данных Kotlin

### 6.1 Domain-модели

```kotlin
// Тип DNS-записи
enum class DnsRecordType(val code: Int, val label: String) {
    A(1, "A (IPv4)"),
    AAAA(28, "AAAA (IPv6)")
}

// Режим работы
enum class DnsMode {
    LOOKUP,
    COMPOSE,
    DETAILS
}

// Результат разрешения
sealed class DnsResultState {
    data class Success(
        val ips: List<DnsIp>,
        val rcode: Int,
        val queryTimeMs: Int,
        val rawResponseHex: String? = null
    ) : DnsResultState()

    data class Error(
        val code: DnsErrorCode,
        val message: String,
        val server: String,
        val queryTimeMs: Int? = null
    ) : DnsResultState()

    data object Loading : DnsResultState()
}

// Коды ошибок
enum class DnsErrorCode {
    NXDOMAIN,
    TIMEOUT,
    REFUSED,
    BAD_SERVER,
    SOCKET_ERROR,
    SEND_ERROR,
    TRUNCATED,
    UNKNOWN
}

// Запись в истории
@Entity(tableName = "history")
data class HistoryEntry(
    @PrimaryKey(autoGenerate = true) val id: Long = 0,
    val domain: String,
    val recordType: Int,       // DnsRecordType.code
    val server: String,
    val port: Int,
    val timestamp: Long,       // System.currentTimeMillis()
    val resultIps: String?,    // IP через запятую или null
    val errorCode: String?,    // DnsErrorCode.name или null
    val rcode: Int?
)

// Представление пакета
data class PacketPresentation(
    val hexDump: String,
    val structured: String,
    val schemes: List<String>  // 3 схемы (индексы 0,1,2)
)
```

### 6.2 UI State

```kotlin
// Главный экран
data class MainUiState(
    val domain: String = "",
    val recordType: DnsRecordType = DnsRecordType.A,
    val server: String = "8.8.8.8",
    val port: Int = 53,
    val timeoutMs: Int = 2000,
    val retries: Int = 2,
    val advancedExpanded: Boolean = false,
    val history: List<HistoryEntry> = emptyList(),
    val isLoading: Boolean = false
)

// Экран результата
data class ResultUiState(
    val domain: String = "",
    val recordType: DnsRecordType = DnsRecordType.A,
    val server: String = "",
    val result: DnsResultState = DnsResultState.Loading
)

// Экран пакета
data class PacketUiState(
    val domain: String = "",
    val recordType: DnsRecordType = DnsRecordType.A,
    val packet: PacketPresentation? = null,
    val selectedTab: PacketTab = PacketTab.HEX
)

enum class PacketTab { HEX, STRUCTURED, SCHEME_0, SCHEME_1, SCHEME_2 }
```

---

## 7. Обработка ошибок

### 7.1 Стратегия

Каждая точка отказа должна давать пользователю **понятное, actionable сообщение**,
а не технический код ошибки.

### 7.2 Матрица ошибок

| C-код (`dns_net_status_t`) | RCODE | Сообщение пользователю | Действие |
|-----------------------------|-------|------------------------|----------|
| `DNS_NET_OK` + `DNS_RCODE_NXDOMAIN` | 3 | `Домен «…» не существует` | Проверить имя |
| `DNS_NET_OK` + другой RCODE | ≠0 | `Ошибка DNS: код …` | Повторить / сменить сервер |
| `DNS_NET_TIMEOUT` | — | `Сервер … не отвечает (таймаут … мс)` | Проверить сеть |
| `DNS_NET_REFUSED` | — | `Сервер … отклонил запрос` | Сменить сервер |
| `DNS_NET_BAD_SERVER` | — | `Неверный адрес сервера «…»` | Проверить IP |
| `DNS_NET_SOCKET` | — | `Не удалось создать сетевое соединение` | Перезапустить |
| `DNS_NET_SEND` | — | `Ошибка отправки пакета` | Проверить сеть |
| `DNS_NET_TRUNCATED` | — | `Получен повреждённый ответ` | Повторить |
| `dns_encode_name()` = -1 | — | `Некорректное доменное имя «…»` | Проверить имя |
| `dns_build_query()` = -1 | — | `Домен слишком длинный` | Сократить имя |

### 7.3 Валидация ввода на клиенте (до отправки в JNI)

| Поле | Правило | Сообщение |
|------|---------|-----------|
| Домен | Непустой, ≤253 символа, валидные символы | `Введите доменное имя` |
| IP сервера | Валидный IPv4 или IPv6 | `Введите корректный IP-адрес` |
| Порт | 1–65535 | `Порт должен быть от 1 до 65535` |
| Таймаут | 100–30000 мс | `Таймаут: 100–30000 мс` |
| Retries | 0–10 | `Повторы: 0–10` |

---

## 8. Нефункциональные требования

### 8.1 Производительность

| Метрика | Цель |
|---------|------|
| Время запуска (cold start) | ≤ 1.5 с |
| Время выполнения lookup (типовой) | ≤ 3 с |
| Потребление памяти (idle) | ≤ 50 МБ |
| Потребление памяти (с загруженным пакетом) | ≤ 100 МБ |
| Размер APK | ≤ 15 МБ (с нативными .so) |

### 8.2 Безопасность

- Все DNS-запросы — только UDP, без шифрования (как в библиотеке `dnslab`).
- Пользователь явно видит, на какой сервер идёт запрос.
- Никакие данные не отправляются на сторонние сервера, кроме указанного DNS-сервера.
- История хранится локально в Room DB, не синхронизируется.

### 8.3 Доступность (Accessibility)

- Все кнопки и поля имеют `contentDescription`.
- Достаточный контраст текста (WCAG AA).
- Поддержка `TalkBack`.
- Минимальный размер touch-target: 48dp.

### 8.4 Локализация

- v1: русский и английский (`strings.xml` + Compose `stringResource`).
- v2: дополнительные языки по необходимости.

---

## 9. Технологический стек

| Слой | Технология | Версия |
|------|-----------|--------|
| Язык UI | Kotlin | 1.9+ |
| UI Framework | Jetpack Compose | 1.5+ (BOM) |
| Архитектура | MVVM + Clean Architecture | — |
| Навигация | Compose Navigation | 2.7+ |
| DI | Hilt (Dagger) | 2.48+ |
| Локальная БД | Room | 2.6+ |
| Асинхронность | Kotlin Coroutines + Flow | 1.7+ |
| Нативный код | C17, Android NDK | r26+ |
| Сборка натива | CMake | 3.22+ |
| Сборка проекта | Gradle KTS | 8.2+ |
| Min SDK | API 26 (Android 8.0) | — |
| Target SDK | API 34 (Android 14) | — |

---

## 10. Структура Android-проекта

```
android-dns-resolver/
├── app/
│   ├── build.gradle.kts
│   ├── src/
│   │   ├── main/
│   │   │   ├── AndroidManifest.xml
│   │   │   ├── java/com/dnslab/app/
│   │   │   │   ├── DnsResolverApp.kt          // Application-класс (Hilt)
│   │   │   │   ├── MainActivity.kt             // single-activity
│   │   │   │   ├── di/                          // Hilt-модули
│   │   │   │   │   └── AppModule.kt
│   │   │   │   ├── data/
│   │   │   │   │   ├── bridge/
│   │   │   │   │   │   └── DnsNativeBridge.kt   // JNI-обёртка
│   │   │   │   │   ├── repository/
│   │   │   │   │   │   ├── DnsRepositoryImpl.kt
│   │   │   │   │   │   └── HistoryRepositoryImpl.kt
│   │   │   │   │   └── local/
│   │   │   │   │       ├── HistoryDao.kt
│   │   │   │   │       ├── HistoryDatabase.kt
│   │   │   │   │       └── HistoryEntryEntity.kt
│   │   │   │   ├── domain/
│   │   │   │   │   ├── model/
│   │   │   │   │   │   ├── DnsRecordType.kt
│   │   │   │   │   │   ├── DnsResultState.kt
│   │   │   │   │   │   ├── DnsErrorCode.kt
│   │   │   │   │   │   └── PacketPresentation.kt
│   │   │   │   │   └── repository/
│   │   │   │   │       ├── DnsRepository.kt      // интерфейс
│   │   │   │   │       └── HistoryRepository.kt   // интерфейс
│   │   │   │   └── ui/
│   │   │   │       ├── navigation/
│   │   │   │       │   └── NavGraph.kt
│   │   │   │       ├── theme/
│   │   │   │       │   ├── Theme.kt
│   │   │   │       │   ├── Color.kt
│   │   │   │       │   └── Type.kt
│   │   │   │       ├── main/
│   │   │   │       │   ├── MainScreen.kt
│   │   │   │       │   └── MainViewModel.kt
│   │   │   │       ├── result/
│   │   │   │       │   ├── ResultScreen.kt
│   │   │   │       │   └── ResultViewModel.kt
│   │   │   │       ├── packet/
│   │   │   │       │   ├── PacketViewScreen.kt
│   │   │   │       │   └── PacketViewViewModel.kt
│   │   │   │       ├── details/
│   │   │   │       │   ├── DetailsScreen.kt
│   │   │   │       │   └── DetailsViewModel.kt
│   │   │   │       └── settings/
│   │   │   │           ├── SettingsScreen.kt
│   │   │   │           └── SettingsViewModel.kt
│   │   │   ├── cpp/                               // Нативный код
│   │   │   │   ├── CMakeLists.txt
│   │   │   │   ├── dnslab_jni.c                   // JNI-обвязка
│   │   │   │   └── dnslab/                        // Симлинк/копия библиотеки
│   │   │   │       ├── engine/dns.c, dns.h
│   │   │   │       ├── decor/print.c, print.h
│   │   │   │       ├── net/net.c, net.h
│   │   │   │       ├── command/compose.c, lookup.c, details.c, resolv.c
│   │   │   │       ├── command/command.h
│   │   │   │       └── jni/dnslab_bridge.c, dnslab_bridge.h
│   │   │   └── res/
│   │   │       ├── values/strings.xml
│   │   │       └── values-ru/strings.xml
│   │   └── test/                                   // Unit-тесты
│   │       └── java/com/dnslab/app/
│   │           ├── viewmodel/
│   │           └── repository/
├── build.gradle.kts                               // Корневой
├── settings.gradle.kts
└── gradle/
```

---

## 11. План разработки (Roadmap)

### Этап 0: Подготовка C-библиотеки (1–2 дня)

- [ ] Рефакторинг `decor/print.c`: замена `printf` на `snprintf`-варианты.
- [ ] Рефакторинг `command/*.c`: возврат строк вместо печати в stdout.
- [ ] Создание `src/jni/dnslab_bridge.c` с плоским C-интерфейсом.
- [ ] Тестирование bridge на десктопе (существующий `make test`).

### Этап 1: Android-проект и JNI (2–3 дня)

- [ ] Создание Android-проекта (Gradle KTS + AGP).
- [ ] Настройка NDK + CMake для сборки `libdnslab.so`.
- [ ] Написание JNI-обвязки (`dnslab_jni.c`).
- [ ] Kotlin-обёртка `DnsNativeBridge`.
- [ ] Интеграционный тест: lookup `example.com` → список IP.

### Этап 2: UI — Главный экран (2–3 дня)

- [ ] Тема, цветовая схема, типографика.
- [ ] `MainScreen` с формой ввода.
- [ ] `MainViewModel` с валидацией.
- [ ] Переход на экран результата / пакета.

### Этап 3: UI — Результаты и пакеты (2–3 дня)

- [ ] `ResultScreen` — отображение IP-адресов и ошибок.
- [ ] `PacketViewScreen` — hex dump, structured, 3 ASCII-схемы с табами.
- [ ] `DetailsScreen` — полный трассировочный вывод.

### Этап 4: История и настройки (1–2 дня)

- [ ] Room DB: `HistoryEntry`, DAO, миграции.
- [ ] `HistoryRepository` + отображение на главном экране.
- [ ] `SettingsScreen`: тема, дефолтные параметры.

### Этап 5: Полировка (1–2 дня)

- [ ] Тёмная тема.
- [ ] Accessibility (TalkBack, контраст).
- [ ] Обработка поворота экрана и process death.
- [ ] Русская локализация.

---

## 12. Критерии приёмки MVP

- [x] Пользователь вводит домен → нажимает «Разрешить» → видит IP-адреса.
- [x] Пользователь вводит домен → нажимает «Собрать» → видит hex dump и 3 схемы.
- [x] Ошибки (NXDOMAIN, таймаут) отображаются понятным текстом.
- [x] Можно сменить DNS-сервер, таймаут, число повторных попыток.
- [x] История запросов сохраняется и отображается.
- [x] Приложение не крашится при смене ориентации экрана.
- [x] APK собирается одной командой `./gradlew assembleRelease`.
- [x] Размер APK ≤ 15 МБ.

---

## 13. Открытые вопросы (на будущее)

| Вопрос | Статус |
|--------|--------|
| Поддержка DNS-over-TLS / DNS-over-HTTPS | Не в scope v1 |
| Поддержка TCP-fallback при TC-флаге | Не в scope v1 |
| Автоопределение DNS-сервера через `LinkProperties` | v2 |
| Виджет на рабочий стол | Не планируется |
| Поддержка mDNS (Multicast DNS) | Не планируется |
| Интеграция с VPN-сервисом Android (перехват всех DNS-запросов) | Не планируется |
| Поддержка дополнительных типов записей (MX, NS, TXT, SOA, CNAME) | v2 (после доработки `dnslab`) |

---

> **Статус документа**: требует ревью и утверждения перед началом реализации.
