# Сравнение `dnslab` с `dig` и `nslookup`

> Сравнение выполнено 2026-07-09 на macOS.
> Проверочный домен: **example.com** (A-запись).

---

## 1. Сводка: что делает каждый инструмент

| Критерий | `dig` | `nslookup` | `dnslab` |
|----------|-------|------------|----------|
| Тип | Профессиональный DNS-диагност | Интерактивный/простые запросы | **Учебная тулза «снизу вверх»** |
| Протоколы | UDP (с TCP-fallback при TC) | UDP (с TCP-fallback) | **UDP только** |
| Типы записей | Все (A, AAAA, MX, NS, TXT, SOA, ANY, …) | Основные (A, AAAA, MX, NS, …) | **A (IPv4), AAAA (IPv6)** |
| DNSSEC | Да (флаги AD, CD, RRSIG) | Нет | **Нет** |
| EDNS0 | Да (расширения, >512 байт) | Да | **Нет** |
| Пакет как HEX | Есть (`dig +qr`) | Нет | **Да** (`--details` → hex dump) |
| Визуализация схемы пакета | Нет | Нет | **Да** (3 ASCII-представления) |
| Образовательный фокус | Нет | Нет | **Да** — показывает, как устроен DNS «под капотом» |

---

## 2. Вывод результатов (brief mode)

### `dig +short example.com A`
```
172.66.147.243
104.20.23.154
```

### `nslookup example.com`
```
Server:         46.243.231.30
Address:        46.243.231.30#53

Non-authoritative answer:
Name:   example.com
Address: 172.66.147.243
Name:   example.com
Address: 104.20.23.154
```

### `dnslab example.com`
```
172.66.147.243
104.20.23.154
```

**Итог:** все три утилиты возвращают **один и тот же набор IP-адресов**.
`dnslab` в кратком режиме повторяет вывод `dig +short`.

---

## 3. CLI-интерфейс

### `dig`

```text
Usage: dig [@global-server] [domain] [q-type] [q-class] {q-opt}

Options: -4, -6, -b, -c, -f, -p, -t, -x, -y, …
Opts:    +short, +noall, +answer, +dnssec, +bufsize=, …
```

- 100+ опций: от выбора сервера (`@server`) до DNSSEC, TCP-fallback, batch-режим
- Позиционный синтаксис: `dig @server domain type`
- Может выполнять несколько запросов за один вызов
- Настраивается через `~/.digrc`

### `nslookup`

```text
Usage: nslookup [-option] [name | -] [server]
```

- Два режима: **интерактивный** (без аргументов или с `-`) и **неинтерактивный**
- В интерактивном можно менять сервер, тип запроса, timeout
- Опции: `-query=`, `-type=`, `-timeout=`, `-port=`

### `dnslab`

```text
Usage: dnslab [FLAG] <domain>

Modes:  (default) | --compose-request | --details
Options: --server, --timeout, --retries, --verbose/-v, --ipv6/-6, -h/--help
```

- **Три режима** работы: короткий (IP), компоновка (без сети), детальный (сеть + дамп)
- Единый флаг для сервера (`--server`), в dig это `@server`, в `nslookup` — позиционный аргумент
- Таймаут и ретраи — явные именованные флаги (у dig — `+time=` и `+tries=`, у nslookup — `-timeout=`)
- `--verbose` — уникальная фича: сводка RCODE, флагов, TTL (нет ни в dig +short, ни в nslookup)

---

## 4. Поведение при ошибках

| Ситуация | `dig` | `nslookup` | `dnslab` |
|----------|-------|------------|----------|
| NXDOMAIN | `status: NXDOMAIN` | `** server can't find ...: NXDOMAIN` | `Error: domain does not exist (NXDOMAIN)` |
| Таймаут | `;; connection timed out; no servers could be reached` | `;; connection timed out; no servers could be reached` | `Error: no response after all retries` |
| Неизвестный сервер | `;; couldn't get address for ...: not found` | `** server not found: ...` | `Error: cannot resolve server address` |
| Нет A-записей | Пустой вывод (exit 0) | `** server can't find ...: No answer` | `Error: no A records in response` |

Все три утилиты корректно обрабатывают одни и те же ошибки, но `dig`
использует `stderr` для диагностики, `nslookup` — смешанный вывод,
`dnslab` — **строго `stderr` для ошибок** (в кратком режиме) и
`stdout` для нормального результата.

---

## 5. Уникальные возможности `dnslab`

Чего нет ни в `dig`, ни в `nslookup`:

1. **Визуализация сырого пакета** — hex dump, structured breakdown, 3 ASCII-схемы
   (`--compose-request` и `--details`)
2. **Детальный лог попыток** — `"Attempt 2/3: sending to server:53"` + причина ретрая
3. **Образовательная направленность** — показывает ID, флаги (QR/OPCODE/AA/TC/RD/RA/RCODE),
   кодировку доменного имени побайтово
4. **Подсказка** — `"Hint: compare with \`dig +short <domain> A\`"` в `--details`
5. **Понимание compression pointer** — парсер ответа умеет разыменовывать
   `0xC00C`-указатели (это не видно в dig/nslookup, но критично для понимания DNS)

---

## 6. Пример: разница в объёме вывода для одного запроса

| Режим | Строк вывода |
|-------|-------------|
| `dig +short example.com` | 2 |
| `nslookup example.com` | 6 |
| `dnslab example.com` (brief) | **2** |
| `dnslab --verbose example.com` | **15** |
| `dnslab --details example.com` | **269** |

`dnslab --details` существенно подробнее, так как показывает каждый байт пакета
в трёх представлениях — это не нужно для повседневной работы, но **полезно для
изучения протокола**.

---

## 7. Сравнение man / --help (объём документации)

| Параметр | `dig` | `nslookup` | `dnslab` |
|----------|-------|------------|----------|
| Страниц man | ~150 строк | ~120 строк | **Нет** |
| Строк `--help` | ~50 | ~10 (неформальный) | **26** |
| Примеров в help | 0 (в man есть) | 1 | **6** |

Документация `dnslab` минималистична — это осознанный выбор для учебного
инструмента. Все концепции описаны в `plan.md` и `net-plan.md`.

---

## Вывод

`dnslab` **не заменяет** `dig` или `nslookup` — он решает другую задачу:
показать, как работает DNS на уровне байтов и сокетов. Все три утилиты
возвращают одинаковые IP-адреса для одного домена, но `dnslab` делает
этот процесс прозрачным для изучения.
