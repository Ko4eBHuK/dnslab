# dnslab — План: сетевой этап (UDP request / response)

> Задание (`week-task.md`): отправить DNS-запрос по UDP и получить ответ.
> Доп. цели: timeout, простая CLI-команда, сравнить с `dig`/`nslookup`.

---

## Цель этапа

Превратить `dnslab` из «конструктора запроса» в «настоящий мини-resolver»:
собрать пакет → отправить по UDP на DNS-сервер → получить сырый ответ →
распарсить его → показать IP и визуализацию ответа.

Что **уже готово** (повторно использовать, не переписывать):

| Файл | Что есть | Сетевой этап |
|------|----------|---------------|
| `src/engine/dns.{c,h}` | `dns_encode_name`, `dns_build_query` (12 B header + question) | добавить **парсер ответа** |
| `src/decor/print.{c,h}` | `dns_print_hex/structured/scheme`, парсинг Question (`parse_question`) | добавить **визуализацию Answer** |
| `src/command/compose.c` | `cmd_compose` — сборка + 3 визуализации, без сети | разбираем на части и переиспользуем |
| `src/command/lookup.c` | **стаб** `cmd_lookup` (MODE_DEFAULT, без флага) | реализовать: **2 режима** — краткий `domain → IP` (без флага) и подробный (`--verbose`) |
| `src/command/details.c` | **стаб** `cmd_details` (`--details`) | реализовать: compose + сеть + полный decode |
| `src/cli.c` | диспетчер уже зовёт `cmd_lookup`/`cmd_details` | добавить флаги `--server`, `--timeout`, `--retries`, `--verbose` |
| `src/test/test_dns.c` | тесты на encode/build (7/7) | (optional) тест на парсер ответа |

---

## Теория

### 1. UDP socket (Berkeley sockets)

DNS по умолчанию работает поверх **UDP**: нет соединения, нет гарантии
доставки, пакет = одна дейтаграмма. В C это `socket(AF_INET, SOCK_DGRAM, 0)`.

Шаги клиента:
1. `getaddrinfo(server, "53", hints{SOCK_DGRAM})` → кандидатный адрес.
2. `socket(...)` → fd.
3. (опц.) `setsockopt(SO_RCVTIMEO)` — таймаут на приём.
4. `sendto(fd, query, query_len, 0, addr, addrlen)` — отправляем запрос.
5. `recvfrom(fd, resp, cap, 0, ...)` — читаем ответ.
6. `close(fd)`.

UDP — без состояния: один `sendto` + один `recvfrom`, без `connect`.
Хотя для UDP можно сделать «connected socket» (`connect` на дейтаграммном
сокете фильтрует входящие пакеты только от этого адреса) — удобнее для
одного сервера, но ограничивает гибкость. На этом этапе используем
**unconnected** `sendto`/`recvfrom`.

### 2. Destination address / port

- **Порт:** 53 (`domain` в `/etc/services`) — и для UDP, и для TCP.
- **Адрес сервера** берём в таком порядке:
  1. флаг `--server <ip>` (явно заданный пользователем);
  2. разбор `/etc/resolv.conf` (первая строка `nameserver`);
  3. fallback на публичный `8.8.8.8` (Google), чтобы утилита работала
     «из коробки» даже без резолвера в системе.

Парсить `/etc/resolv.conf` проще всего построчно: ищем строку, первый
токен которой `nameserver`, второй — IP. Поддерживаются как IPv4, так и
IPv6 адреса серверов (через `AF_UNSPEC` в `getaddrinfo`).

### 3. Timeout

UDP не подтверждает доставку — `recvfrom` может висеть вечно. Поэтому
**обязателен** приёмный таймаут:

```c
struct timeval tv = { .tv_sec = timeout_ms / 1000,
                      .tv_usec = (timeout_ms % 1000) * 1000 };
setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
```

После таймаута `recvfrom` вернёт `-1` с `errno == EAGAIN`/`EWOULDBLOCK`.
Значит — пакета нет (или потерялся). Это **не фатальная** ошибка: делаем
retry (см. ниже), и только после N попыток рапортуем «no response».

### 4. Packet loss

UDP ненадёжен: запрос или ответ могут не дойти. Классический DNS-клиент
реагирует **повтором запроса** через таймаут. На этом этапе:

- `--retries N` (по умолчанию 2): при таймауте повторяем `sendto`+`recvfrom`,
  каждый раз с тем же `ID` и тем же телом запроса (idempotent для DNS).
- счётчик попыток выводится в `--details` («retry 1/2…»).

Идентификатор транзакции (`ID` в заголовке) нужен, чтобы сопоставить
ответ с запросом — особенно при ретраях. Проверяем: `resp.ID == req.ID`,
иначе пакет чужой (игнорируем/продолжаем ждать).

### 5. Network error

Коды ошибок, которые надо различать:

| Где | `errno` / код | Смысл | Реакция |
|-----|---------------|-------|---------|
| `getaddrinfo` | ненулевой код | неизвестный сервер/имя | «cannot resolve server address» |
| `socket` | `-1` | нет ресурсов | «socket creation failed» |
| `sendto` | `-1`, `EACCES`/`ENETUNREACH` | нет сети/прав | «send failed: …» |
| `recvfrom` | `-1`, `EAGAIN` | таймаут | retry |
| `recvfrom` | `-1`, `ECONNREFUSED` | ICMP port unreachable (сервер не отвечает на UDP) | «server unreachable» |
| `recvfrom` | `n>0`, но `n < 12` | урезанный пакет | «response too short» |

Конечный пользователь видит понятные сообщения, не системные `perror`.

### 6. Ответ DNS (response packet)

Структура ответа: `header (12 B) + question + answer + authority + additional`.

Поле `flags.QR = 1` (это ответ), `flags.RCODE` — код результата
(`0` = NOERROR, `3` = NXDOMAIN — домена нет, и т.д.).

**Answer RR** (Resource Record) идёт сразу после question:

```
+----------+--------+--------+--------+----------+--------+
| NAME (переменная) | TYPE(2)| CLASS(2)| TTL(4)   | RDLEN(2)| RDATA(RDLEN) |
+----------+--------+--------+--------+----------+--------+
```

Для A-записи: `TYPE=1`, `CLASS=1`, `RDATA` = ровно 4 байта IPv4.

**Message compression (важно!):** поле `NAME` (и любое доменное имя в
ответе) может быть **указателем**: 2 байта, старшие 2 бита = `11`, а
младшие 14 бит — смещение от начала пакета. Пример: `0xC00C` → указатель
на байт 12 (повтор question name). Парсер ответа обязан уметь идти по
указателям (с защитой от зацикливания: лимит на число переходов).

---

## Архитектура: куда что класть

Проект уже разбит по «слоям»: `engine` (протокол), `decor`
(представление), `command` (оркестрация), `cli` (разбор аргументов).
Сеть — это новый **транспортный** слой. Чтобы не смешивать сокеты с
протоколом (engine «принципиально не знает про сеть», см. комментарий в
`dns.h`), заводим отдельный модуль `src/net/`.

Новые/изменяемые файлы:

```
src/
  net/                      ← НОВЫЙ слой (transport)
    net.h                   ← объявление dns_send_udp()
    net.c                   ← sockets: getaddrinfo/socket/sendto/recvfrom + timeout + retries
  engine/
    dns.h                   ← ДОБАВИТЬ: константы ответа + прототип dns_parse_response()
    dns.c                   ← ДОБАВИТЬ: dns_parse_response() (header flag/RCODE, answer A records)
    parse.h / parse.c       ← (альтернатива) вынести парсинг ответа сюда, если engine разрастётся
  decor/
    print.h                 ← ДОБАВИТЬ: dns_print_response() / визуализация Answer RR
    print.c                 ← ДОБАВИТЬ: parse_answer(), отрисовка answer-секций (schema/binary/human)
  command/
    resolv.c                ← НОВЫЙ: чтение /etc/resolv.conf → адрес сервера (+ fallback)
    lookup.c               ← РЕАЛИЗОВАТЬ cmd_lookup()
    details.c               ← РЕАЛИЗОВАТЬ cmd_details() (compose + сеть + decode ответа)
    compose.c               ← РЕФАКТОРИТЬ: вынести «сборка+визуализация» в общую функцию
  cli.c                    ← ДОБАВИТЬ флаги --server / --timeout / --retries
Makefile                   ← добавить src/net/net.c и src/command/resolv.c в сборку
```

### API транспортного слоя (`src/net/net.h`)

```c
#define DNS_PORT        53
#define DNS_DEFAULT_TIMEOUT_MS  2000
#define DNS_DEFAULT_RETRIES     2

/*
 * Отправить DNS-запрос по UDP и получить сырой ответ.
 *
 *   req        — собранный запрос (из dns_build_query)
 *   req_len    — длина запроса
 *   resp       — буфер под ответ (>= DNS_MAX_PACKET)
 *   resp_cap   — ёмкость буфера
 *   server     — IP-адрес сервера строкой ("8.8.8.8"), NULL = автодетект
 *   port       — порт (обычно DNS_PORT)
 *   timeout_ms  — приёмный таймаут на одну попытку
 *   retries    — число повторов при таймауте (0 = одна попытка)
 *
 * Возвращает длину полученного ответа (>=0) или код ошибки из enum ниже.
 */
typedef enum {
    DNS_NET_OK = 0,
    DNS_NET_BAD_SERVER,     /* не смогли разобрать/разрешить адрес сервера */
    DNS_NET_SOCKET,         /* не удалось создать сокет */
    DNS_NET_SEND,           /* sendto провалился */
    DNS_NET_TIMEOUT,        /* все попытки истекли (packet loss / сервер молчит) */
    DNS_NET_REFUSED,        /* ECONNREFUSED: сервер недостижим */
    DNS_NET_TRUNCATED       /* ответ урезан (< 12 байт) */
} dns_net_status_t;

int dns_send_udp(const uint8_t *req, size_t req_len,
                 uint8_t *resp, size_t resp_cap,
                 const char *server, uint16_t port,
                 int timeout_ms, int retries,
                 dns_net_status_t *status);
```

Возврат `int` (длина ответа) + out-параметр `status` — чтобы отличать
«нет ответа» (нужно вернуть код ошибки вызывающему) от «ответ = 0 байт»
(не бывает, но тип позволяет). Команды мапят `status` в человекочитаемые
сообщения.

### API парсера ответа (`engine/dns.h`)

```c
/* Код ответа RCODE из flags. */
#define DNS_RCODE_NOERROR 0
#define DNS_RCODE_NXDOMAIN 3

/* Одна answer-запись (только A на этом этапе). */
typedef struct {
    uint16_t type;        /* ожидаем DNS_TYPE_A */
    uint16_t class_;      /* ожидаем DNS_CLASS_IN */
    uint32_t ttl;
    uint8_t  rdata[16];   /* 4 байта для A */
    uint8_t  rdlength;
} dns_answer_rr_t;

typedef struct {
    uint16_t id;
    uint16_t flags;
    int      rcode;
    uint16_t qdcount, ancount, nscount, arcount;
    dns_answer_rr_t answers[16];   /* первые несколько A-записей */
    size_t          nanswers;
} dns_response_t;

/*
 * Распарсить сырой DNS-ответ в структуру. Идёт по указателям
 * (compression) безопасно, не читает за пределами len.
 * Возвращает 0 = OK, -1 = битый/короткий пакет.
 */
int dns_parse_response(const uint8_t *buf, size_t len, dns_response_t *out);
```

---

## Шаги реализации (по файлам)

### Шаг 1 — `src/net/net.{c,h}`: транспорт
- `getaddrinfo` с `SOCK_DGRAM`; `socket`; `SO_RCVTIMEO`.
- Цикл retry: на каждой итерации `sendto` + `recvfrom`.
- Проверка `resp.ID == req.ID` (ответ мог прийти от прошлого ретрая или
  чужой — игнорируем и ждём дальше, не расходуя попытку зря).
- Маппинг `errno` → `dns_net_status_t`.
- `close(fd)` в **always**-ветке (даже при ошибке).
- Сборка: добавить `src/net/net.c` в `LIB_SRC` (или новый `NET_SRC`)
  в `Makefile`.

### Шаг 2 — `src/command/resolv.c`: выбор сервера
- Прочитать `/etc/resolv.conf`, найти первую `nameserver <ip>`.
- Если нет резолвера / файла — вернуть fallback `"8.8.8.8"`.
- Дать переопределить через `--server` (логика в `cli.c`, сюда строку
  передаёт команда).

### Шаг 3 — `engine/dns.{c,h}`: парсер ответа
- Читать header (использовать `dns_header_t` + `ntohs` — уже есть).
- Декодировать `flags`: `QR`, `RCODE` (битовое поле поMask как в
  `dns_print_structured`).
- Пропустить question (повторно пройти `dns_encode_name`-логику,
  вынести пропуск имени в общую `skip_name()` — используется и тут, и в
  парсере answer-имён).
- `parse_answer()`: имя (с поддержкой pointer-сжатия + лимит переходов),
  `TYPE/CLASS/TTL/RDLENGTH`, для `TYPE==A && CLASS==IN` сохранить RDATA.
- Вернуть `dns_response_t` с несколькими A-записями.

### Шаг 4 — `decor/print.{c,h}`: визуализация ответа
- Переиспользовать `dns_print_hex` и `dns_print_scheme` для **сырого
  ответного пакета** (они уже generic по `buf/len`).
- Новый `dns_print_response(resp)`: человекочитаемая сводка ответа —
  RCODE, флаги, список answer RR (`name → A → ip → TTL`).
- Для mode 0 (schema): нарисовать row для answer RR
  (`NAME | TYPE | CLASS | TTL | RDLEN | RDATA`) — это естественное
  продолжение сетки. Если answer > 16 байт — несколько строк (как сейчас
  Question занимает несколько строк).

### Шаг 5 — `src/command/compose.c`: рефакторинг общего кода
- Вынести «сборка запроса + hex + structured + 3 схемы» в
  `compose_request(const char *domain, uint8_t *buf, size_t cap)`
  (в `command/command.h` или новом `command/common.{c,h}`), чтобы
  `cmd_compose` и `cmd_details` делили этот код, без дублирования.

### Шаг 6 — `src/command/lookup.c`: краткий режим (`domain → IP`) и подробный режим (`--verbose`)
У команды **два режима вывода**:
- **Краткий (без флага, по умолчанию)** — аналог `dig +short`: только IP-адреса,
  по одному на строку (по одному на каждую A-запись). Без шапки, без визуализации пакета.
- **Подробный (`--verbose` / `-v`)** — человекочитаемая сводка: использованный сервер
  и порт, число попыток (если были ретраи), код ответа (RCODE), список answer-записей
  в виде `name → A → ip → TTL`.

Общий flow для обоих режимов (различается только печать результата):
- `compose_request` (без печати 3 схем) → `dns_send_udp` → `dns_parse_response`.
- Краткий режим: печатаем только `ip` (RDATA из каждой A-записи).
- Подробный режим: печатаем сервер/попытки + сводку ответа через `dns_print_response`
  (RCODE, флаги, answer RR — см. Шаг 4); вывод короче и «человечнее», чем у `--details`.
- Обработка ошибок едина для обоих режимов: NXDOMAIN («domain does not exist»),
  no A record («no A record in response»), timeout («no response after N retries»).

### Шаг 7 — `src/command/details.c`: полный flow
- Вывод `compose_request` (как `--compose-request`).
- Лог шагов: «sending to <server>:53 (attempt 1/2)».
- Сырой ответ: hex + structured + 3 схемы (как для запроса, но для ответа).
- `dns_print_response`: декод флагов/RCODE/answer RR.
- Итог: `domain -> ip`.

### Шаг 8 — `src/cli.c`: флаги
- `--server <ip>` → передаётся в команду.
- `--timeout <ms>` → `dns_send_udp`.
- `--retries <n>` → `dns_send_udp`.
- `--verbose` (`-v`) → переключает `cmd_lookup` в подробный режим (Шаг 6).
  Не конфликтует с `--details`: `--verbose` = сводка ответа (RCODE + answers + сервер),
  `--details` = полный дамп сырого пакета (hex + structured + 3 схемы, Шаг 7).
- Без флага (`MODE_DEFAULT`) остаётся `cmd_lookup` в кратком режиме.

### Шаг 9 — (optional) тесты
- Тест на `dns_parse_response` с захардкоженным известным ответным
  пакетом (как `example.com` → `93.184.216.34`) — проверка распаковки
  pointer-сжатия имени и извлечения RDATA.
- Сетевые тесты не добавляем (флакки в CI).

### Шаг 10 — сравнение с `dig`/`nslookup`
- Запустить `dnslab example.com` и `dig +short example.com A`,
  сравнить набор IP.
- В `--details` можно добавить строку
  «hint: compare with `dig +short <domain>`».

---

## Чек-листы

### Теория (из `week-task.md`)
- [x] UDP socket — `socket(SOCK_DGRAM)`, `sendto`/`recvfrom`
- [x] destination address — `--server` / `/etc/resolv.conf` / fallback `8.8.8.8`
- [x] destination port — 53 (`DNS_PORT`)
- [x] timeout — `SO_RCVTIMEO`, `EAGAIN` → retry
- [x] packet loss — retry N раз с тем же ID
- [x] network error — маппинг `errno` → понятные сообщения

### Практика
- [ ] (optional) unit-тесты на query builder — уже есть (7/7)
- [ ] отправить DNS query — `net/net.c::dns_send_udp`
- [ ] получить raw response — тот же API возвращает буфер

### Доп. цели
- [ ] timeout — `--timeout`, `SO_RCVTIMEO`
- [ ] простая CLI-команда — `dnslab <domain>` (узловой режим)
- [ ] сравнить с `dig`/`nslookup` — вручную

---

## Подводные камни / заметки

- **Message compression** — главная неочевидность ответа. Имена в answer
  почти всегда указатели (`0xC0 0x0C`). Парсер обязан их разыменовывать
  с лимитом на глубину (защита от зацикленных указателей).
- **ID matching** — при `--retries > 0` может прийти «старый» ответ от
  прошлой попытки. Проверяем `ID` и при несовпадении ждём дальше, не
  считая это ошибкой.
- **TC flag** — если ответ не влез в 512 байт UDP, сервер ставит
  `flags.TC = 1` (truncation). На этом этапе просто отмечаем это в
  `--details` и не делаем TCP-fallback (это отдельная задача).
- **Случайный ID** — `DEFAULT_QUERY_ID 0x1234` удобен для демо
  (`--compose-request`), но в сети лучше генерить случайный (защита от
  спуфинга ответов). На этом этапе можно оставить константу, отметив
  как TODO.
- **DNS_MAX_PACKET = 512** — лимит UDP. Буфер ответа этого размера
  достаточно; при TC ответ будет обрезан ровно до 512.
- **IPv6-серверы** — `getaddrinfo` умеет, но `sockaddr` разного размера;
  на этом этапе ограничимся IPv4-серверами (8.8.8.8 / 1.1.1.1), счёт
  расширения до IPv6-резолверов — отдельной задачей.
