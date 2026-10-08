# Пошаговый разбор: `dns_build_query()` в Android через JNI

> Это учебный документ. На примере **самой простой** функции библиотеки
> показано, как Kotlin-приложение шаг за шагом вызывает C-код через JNI,
> и как при этом соблюдается принцип **«ядро простое и не знает о
> потребителях»**. Код C НЕ меняем — только описываем будущую интеграцию.

---

## 0. Почему именно `dns_build_query()`

Из всей библиотеки это самая «чистая» функция:

```c
/* engine/dns.h — уже сегодняшний, готовый код */
int dns_build_query(uint16_t id, const char *domain,
                    uint16_t qtype, uint8_t *buf, size_t bufsize);
```

Свойства, которые делают её идеальным первым примером:

| Свойство | Значение для JNI |
|----------|------------------|
| Слой | `engine/dns.c` — самый нижний, «протокольный» |
| Ввод | строка `domain` + число `qtype` |
| Вывод | байты пакета в буфере + длина (`int`) |
| Печать в stdout | **нет** |
| Сеть | **нет** |
| Глобальное состояние | **нет** |
| Зависимости | только `dns_encode_name()` (тоже чистая) |

То есть уже сегодня `dns_build_query` — это ядро, которое **ничего не знает о
потребителях**. JNI-обвязка к нему — тончайшая прослойка. Если мы поймём её,
остальные функции (`lookup`, `details`) пойдут по той же схеме, просто с
дополнительными шагами (сеть, парсинг ответа).

### Что даёт эта функция в Android

Это базис для режима **Compose** (FR-02 из ТЗ): пользователь вводит домен и
тип записи → приложение собирает DNS-запрос «в уме» (без отправки) →
показывает hex-dump, структурированный разбор и ASCII-схемы.

---

## 1. Принцип ядра и потребителей (ваши пункты 1 и 2)

### 1.1 Текущее смешение

В `decor/print.c` сейчас смешаны две вещи:

- **Логика** — например, `parse_question()` уже внутри `print.c` обходит
  пакет и находит позиции лейблов/QTYPE/QCLASS. Это знание о структуре DNS,
  оно «логическое», а не «печатающее».
- **Потребитель (вывод)** — то же место содержит `printf`/`putchar`, т.е.
  привязку к конкретному потребителю (CLI -> stdout).

Для Android stdout не существует, поэтому вторую часть приходится менять.
Первая часть (логика) — ценная и повторно используемая.

### 1.2 Целевое расслоение

```
┌──────────────────────────────────────────────────────────┐
│  ЯДРО (не знает потребителей)                            │
│  engine/dns.c  ── dns_build_query(), dns_parse_response()│
│                  возвращают ДАННЫЕ (байты / структуры)    │
│  net/net.c     ── dns_send_udp() (транспорт)             │
└───────────────────────────┬──────────────────────────────┘
                            │ только ДАННЫЕ
          ┌─────────────────┴──────────────────┐
          ▼                                     ▼
┌─────────────────────┐               ┌─────────────────────┐
│ Потребитель №1: CLI │               │ Потребитель №2:      │
│  decor/print.c      │               │ Android (Kotlin)     │
│  берёт данные ->    │               │  берёт ByteArray ->  │
│  printf в stdout    │               │  рисует Compose      │
└─────────────────────┘               └─────────────────────┘
```

### 1.3 Следствие для JNI (ваш пункт 2)

В исходном ТЗ (раздел 5.2) предполагалось, что C сам готовит **три готовых
строки** представлений (hex / structured / 3 схемы) и шлёт их в Kotlin. Это
нарушает принцип: ядро «знаёт», как рисовать схему.

**Новый подход:** через JNI уходит **только сырое содержимое пакета** —
`ByteArray`. Все три представления (hex, structured, схемы) строятся
**в Kotlin-потребителе**, как чистые функции от `ByteArray`. Ядро остаётся
минимальным и не осведомлённым о UI.

```
   dnslab (C)                │  JNI (граница)   │   Kotlin
─────────────────────────────┼──────────────────┼──────────────────────
   dns_build_query() ────────┼──> ByteArray ────┼─> hexDump(bytes)
   (только байты)            │   сырые байты    │   structured(bytes)
                             │                  │   schema0/1/2(bytes)
                             │                  │   — чистый Kotlin
```

Сравните объём того, что пересекает границу:

| Подход | Что идёт через JNI | «Знание» ядра о потребителе |
|--------|--------------------|------------------------------|
| Старый (ТЗ 5.2) | 5 строк по 64 КБ (hex+struct+3 схемы) | ядро знает про форматы вывода |
| **Новый (этот док)** | **1 `ByteArray` до 512 байт** | **ядро не знает ничего о выводе** |

Чистая победа по обоим принципам, плюс меньше трафика через JNI.

---

## 2. Полный путь вызова — карта шагов

```
 ┌────────────┐   1. external fun     ┌──────────────┐
 │  Compose UI│ ──────────────────►   │DnsNativeBridge│
 │  (Kotlin)  │   buildQuery(...)      │   .kt         │
 └────────────┘                        └──────┬───────┘
        ▲                                     │ 2. System.loadLibrary("dnslab")
        │ 8. ByteArray ->                     ▼     загружает libdnslab.so
        │    hexDump/structured/         ┌────────────┐
        │    schema (чистый Kotlin)      │  Android    │
        │                                │  ClassLoader│
        └──────────────────────────────  └─────┬──────┘
                                              │ 3. ищет символ
                                              ▼   Java_..._buildQuery
                                       ┌──────────────────┐
                                       │ dnslab_jni.c     │
                                       │ (маршалинг)      │
                                       └────────┬─────────┘
                                                │ 4. jstring -> const char*
                                                │ 5. вызов dns_build_query()
                                                ▼
                                       ┌──────────────────┐
                                       │ engine/dns.c     │
                                       │ dns_build_query()│  ← ЯДРО, не трогаем
                                       └────────┬─────────┘
                                                │ 6. заполняет buf, возвращает len
                                                ▼
                                       ┌──────────────────┐
                                       │ dnslab_jni.c     │
                                       │ 7. NewByteArray +│
                                       │    SetByteArray… │
                                       │    ReleaseString │
                                       └──────────────────┘
```

Шаги ниже пронумерованы K = Kotlin, JNI = C-мост, C = ядро.

---

## 3. Шаг за шагом

### Шаг K0. Модель данных в Kotlin

Сначала решаем, что именно пересекает границу. Согласно пункту 2 — **только
сырые байты пакета**. Никаких готовых строк-представлений.

```kotlin
// domain/model/DnsRecordType.kt
enum class DnsRecordType(val code: Int, val label: String) {
    A(1, "A (IPv4)"),
    AAAA(28, "AAAA (IPv6)")
}
```

`code` здесь — это ровно те `DNS_TYPE_A`/`DNS_TYPE_AAAA` из `engine/dns.h`.
Числовые константы на двух сторонах должны совпадать — это единственный
«контракт» между языками.

### Шаг K1. Объявление `external`-функции

```kotlin
// data/bridge/DnsNativeBridge.kt
package com.dnslab.app.data.bridge

object DnsNativeBridge {
    init { System.loadLibrary("dnslab") }   // грузит libdnslab.so

    /**
     * Собрать DNS-запрос (без сети). Возвращает СЫРЫЕ байты пакета.
     * Никаких представлений сюда не попадает — это задача потребителя.
     *
     * @return ByteArray длиной от 12 до 512 байт
     * @throws DnsBuildException если домен невалиден/слишком длинный
     */
    external fun buildQuery(
        domain: String,
        qtype: Int
    ): ByteArray
}
```

Три наблюдения:
- `external` = «тело функции не здесь, ищи в нативной библиотеке».
- Класс — Kotlin `object` (синглтон). Для JNI это обычный Java-класс с
  единственным экземпляром; мы работаем с ним как с instance-методом.
- **Подписавшись на этот `package`/`object`/`method`, вы ФИКСИРУЕТЕ имя
  C-функции на следующем шаге.** Менять их потом — значит переписывать C.

### Шаг JNI1. Имя C-функции (самое важное правило)

JVM находит нативный метод по имени, склеенному из пакета, класса и метода.
Каждая точка пакета становится подчёркиванием:

```
Java_  com_dnslab_app_data_bridge_DnsNativeBridge  _  buildQuery
       \_________________________________________/     \_______/
                       пакет + класс                  метод
```

Поэтому сигнатура C-функции жёстко привязана к:
- пакету Kotlin-класса (`com.dnslab.app.data.bridge`),
- имени класса (`DnsNativeBridge`),
- имени метода (`buildQuery`).

Поменяете пакет в Kotlin → изменится имя → `loadLibrary` упадёт с
`UnsatisfiedLinkError`. Также R8/ProGuard могут переименовать класс при
релизе — нужен keep-rule (см. раздел 5).

Каждый JNI-метод ВСЕГДА получает два первых аргумента: `JNIEnv *env` и
`jobject thiz` (для метода `object`/instance). Дальше — реальные параметры
в C-типах:

```c
JNIEXPORT jbyteArray JNICALL
Java_com_dnslab_app_data_bridge_DnsNativeBridge_buildQuery(
        JNIEnv *env, jobject thiz,    /* всегда первыми */
        jstring jdomain, jint qtype   /* отображение Kotlin-параметров */
);
```

Метки `JNIEXPORT` и `JNICALL` — обязательные макросы (видимость символa +
соглашение о вызове).

### Шаг JNI2. Таблица типов (маршалинг)

Для нашего вызова нужны всего 3 строки таблицы:

| Kotlin | C (JNI) | Конвертация |
|--------|---------|-------------|
| `String` | `jstring` | `GetStringUTFChars` → `const char*` |
| `Int` | `jint` (= `int32_t`) | используется напрямую |
| `ByteArray` (return) | `jbyteArray` | `NewByteArray` + `SetByteArrayRegion` |

`jint` пересекает границу как обычный `int32_t` — никаких вызовов
API. Это «бесплатный» тип.

### Шаг JNI3. Полная C-функция моста

```c
/* app/src/main/cpp/dnslab_jni.c */
#include <jni.h>
#include <string.h>
#include <stdlib.h>

#include "dnslab/engine/dns.h"   /* dns_build_query, DNS_MAX_PACKET, ... */

/* Идентификатор транзакции. В CLI это DEFAULT_QUERY_ID (0x1234) из
 * command/command.h. Для Android нам не нужна зависимость от слоя command,
 * поэтому константу кладём рядом (ядро про идентификатор не знает — он
 * просто echo'ится сервером). */
#define BRIDGE_QUERY_ID 0x1234

JNIEXPORT jbyteArray JNICALL
Java_com_dnslab_app_data_bridge_DnsNativeBridge_buildQuery(
        JNIEnv *env, jobject thiz,
        jstring jdomain, jint qtype)
{
    /* --- 1. Marshaling строк: jstring -> const char* --- */
    const char *domain = (*env)->GetStringUTFChars(env, jdomain, NULL);
    if (domain == NULL) {
        return NULL;   /* GetStringUTFChars уже бросил OutOfMemoryError */
    }

    jbyteArray result = NULL;     /* чтобы один return в конце */
    uint8_t   *buf    = NULL;

    /* --- 2. Буфер под пакет (ядро требует >= DNS_MAX_PACKET) --- */
    buf = (uint8_t *)malloc(DNS_MAX_PACKET);
    if (buf == NULL) {
        (*env)->ReleaseStringUTFChars(env, jdomain, domain);
        jclass oom = (*env)->FindClass(env, "java/lang/OutOfMemoryError");
        (*env)->ThrowNew(env, oom, "cannot alloc query buffer");
        return NULL;
    }

    /* --- 3. ВЫЗОВ ЯДРА. Ничего не знаем про вывод. --- */
    int pkt_len = dns_build_query(
        BRIDGE_QUERY_ID,
        domain,
        (uint16_t)qtype,
        buf,
        DNS_MAX_PACKET);

    if (pkt_len < 0) {
        /* Ошибка сборки (невалидный домен / слишком длинный).
         * Кидаем исключение в стиле Kotlin. */
        jclass exc = (*env)->FindClass(env, "com/dnslab/app/domain/exception/DnsBuildException");
        (*env)->ThrowNew(env, exc, "failed to build DNS query (bad domain?)");
        goto cleanup;
    }

    /* --- 4. Сборка jbyteArray из C-буфера --- */
    result = (*env)->NewByteArray(env, (jsize)pkt_len);
    if (result == NULL) {
        goto cleanup;   /* OOM уже брошен */
    }
    (*env)->SetByteArrayRegion(env, result, 0, (jsize)pkt_len, (const jbyte *)buf);

cleanup:
    /* --- 5. ВСЕГДА освобождаем строки, даже на путях ошибок --- */
    free(buf);
    (*env)->ReleaseStringUTFChars(env, jdomain, domain);
    return result;
}
```

Что важно здесь увидеть:

1. **Ядро вызвано как обычная C-функция** — `dns_build_query(...)`. Мы не
   правим `engine/dns.c`, вообще его не касаемся.
2. **Через границу ушёл только `ByteArray`** — никакой hex-строки, никакого
   «структурированного» текста. Потребитель (Kotlin) сам разберётся.
3. `ReleaseStringUTFChars` гарантированно вызывается через `goto cleanup`
   на всех путях, включая ошибку. Это главный источник утечек у новичков:
   `Get` без парного `Release`.
4. Ошибка из C передаётся в Kotlin через `ThrowNew`/исключение, **а не через
   код возврата**. После `ThrowNew` нужно немедленно `goto cleanup; return`,
   иначе C продолжит выполняться «под» pending-исключением.

### Шаг C5. Что ядро делает внутри (контекст — не меняем!)

Для понимания: при `domain = "example.com"`, `qtype = 1` (A) ядро
`dns_build_query` в `engine/dns.c`:

1. Пишет 12-байтный заголовок (`id=0x1234`, `flags=0x0100`, `qdcount=1`).
2. Кодирует имя: `\x07example\x03com\x00` (через `dns_encode_name`).
3. Дописывает `QTYPE=0x0001`, `QCLASS=0x0001` (IN).

Результат — ~29 байт в `buf`, `pkt_len = 29`. Эти 29 байт и есть
`ByteArray`, который вернётся в Kotlin. Никакого stdout, никакой схемы.

### Шаг K2. Kotlin получает `ByteArray`

```kotlin
// domain/repository/DnsRepository.kt
class DnsRepositoryImpl(private val bridge: DnsNativeBridge) : DnsRepository {

    override suspend fun compose(domain: String, type: DnsRecordType): ByteArray {
        // курсорUTDOWN: можно добавить валидацию домена до JNI,
        // чтобы не ходить в C ради заведомо плохого ввода (см. ТЗ 7.3)
        return bridge.buildQuery(domain, type.code)
    }
}
```

На этом уровне мы уже в «потребителе». У нас есть «сырьё» — байты, и мы
можем делать с ним что угодно.

### Шаг K3. Потребитель: чистый Kotlin-рендеринг

Здесь реализуются три представления из FR-02 как **чистые функции от
`ByteArray`**. Это и есть «универсальная логика потребителя», отделённая
от ядра. Приведу `hexDump` целиком (он короткий) и наметки остальных:

```kotlin
// ui/packet/render/PacketRenderer.kt
package com.dnslab.app.ui.packet.render

/** Все три представления строятся笔下 ОТ БАЙТ, без JNI и без C. */

fun hexDump(bytes: ByteArray): String = buildString {
    for (i in bytes.indices step 16) {
        append("%04X  ".format(i))
        for (j in 0 until 16) {
            if (i + j < bytes.size) append("%02X ".format(bytes[i + j]))
            else append("   ")
            if (j == 7) append(' ')
        }
        append(' ')
        for (j in 0 until 16 && i + j < bytes.size) {
            val b = bytes[i + j]
            append(if (b in 32..126) b.toInt().toChar() else '.')
        }
        append('\n')
    }
}

/** Структурированный разбор: первые 12 байт — заголовок, далее имя и QTYPE/QCLASS. */
fun structuredView(bytes: ByteArray): String = buildString {
    val id      = u16(bytes, 0)
    val flags   = u16(bytes, 2)
    val qdcount = u16(bytes, 4)
    append("--- DNS Header ---\n")
    append("  ID:      0x%04X (%d)\n".format(id, id))
    append("  Flags:   0x%04X\n".format(flags))
    append("    QR:    %d\n".format(flags shr 15))
    append("    OPCODE:%d\n".format((flags shr 11) and 0x0F))
    append("    RD:    %d\n".format((flags shr 8) and 1))
    append("    RCODE: %d\n".format(flags and 0x0F))
    append("  QDCOUNT: %d\n".format(qdcount))
    // ... decode question name, QTYPE, QCLASS по тому же алгоритму, что в decor/print.c
    // (перенесли логику в ПОТРЕБИТЕЛЬ — ядро её не содержит)
}

/** Первая ASCII-схема (RFC-диаграмма). */
fun schemaRFC(bytes: ByteArray): String = buildString {
    // ... рисуем |ID|, |QR|OPCODE|...|RCODE| на основе bytes[0..11]
}

private fun u16(b: ByteArray, o: Int) =
    ((b[o].toInt() and 0xFF) shl 8) or (b[o + 1].toInt() and 0xFF)
```

Заметьте: эти функции — **перенос логики из `decor/print.c` в Kotlin**.
Логика `parse_question()` (которая сегодня static-функция внутри `print.c`)
здесь реализуется заново на Kotlin. Это нормально: у нас два разных
потребителя (CLI и Android), и каждый владеет своим способом рисовать. Ядро
(DNS-структура) у них общее — как `ByteArray`.

> Если позже окажется, что дублировать парсер DNS-имени в Kotlin
> некрасиво, можно вынести общий «описатель» в C и положить рядом с ядром:
> вируальную чистую функцию `dns_describe(buf,len)->struct data`, которая
> возвращает **данные** (offset'ы лейблов, декодированные поля), а не
> строки. Тогда CLI и Android оба берут `data` и сами форматируют. Но для
> простого `buildQuery` это излишне — пакет маленький, и Kotlin-парсер
> тривиален.

### Шаг UI. Compose потребляет

```kotlin
// ui/packet/PacketViewScreen.kt
@Composable
fun PacketViewScreen(
    bytes: ByteArray,                 // из buildQuery через ViewModel
    selectedTab: PacketTab,
    ...
) {
    val view = remember(bytes, selectedTab) {
        when (selectedTab) {
            PacketTab.HEX         -> hexDump(bytes)
            PacketTab.STRUCTURED  -> structuredView(bytes)
            PacketTab.SCHEME_RFC  -> schemaRFC(bytes)
            // ...
        }
    }
    // Material 3: Monospace-text карточка с прокруткой
    Card {
        Text(
            text = view,
            fontFamily = FontFamily.Monospace,
            modifier = Modifier.verticalScroll(rememberScrollState())
        )
    }
}
```

`ViewModel` дёргает `DnsRepository.compose(...)` в `viewModelScope.launch`,
кладёт `ByteArray` в `StateFlow`, экран её перерисовывает. Никакого знания о
C/JNI за пределами `DnsNativeBridge` — чистое расслоение.

---

## 4. Полный итог вызова «от тапа до пикселя»

```
тап «Собрать» в Compose UI
   │
   ▼
MainViewModel.compose("example.com", A)
   │ viewModelScope.launch { repository.compose(...) }
   ▼
DnsRepositoryImpl.compose(domain, type)
   │ bridge.buildQuery(domain, type.code)
   ▼
DnsNativeBridge.buildQuery(domain, qtype)        [Kotlin external]
   │ System.loadLibrary("dnslab") уже выполнен в init
   ▼
Java_..._DnsNativeBridge_buildQuery(env, thiz, jdomain, jqtype)   [dnslab_jni.c]
   │ GetStringUTFChars -> domain
   │ malloc(DNS_MAX_PACKET)
   ▼
dns_build_query(0x1234, domain, 1, buf, 512)   [engine/dns.c — ЯДРО]
   │ возвращает 29
   ▼
NewByteArray(29) + SetByteArrayRegion(buf)      [dnslab_jni.c]
   │ ReleaseStringUTFChars(jdomain, domain); free(buf)
   ▼
ByteArray(29) возвращается в Kotlin
   │ repository передает в StateFlow
   ▼
PacketViewScreen(bytes, HEX)
   │ hexDump(bytes)  — чистый Kotlin-потребитель
   ▼
«0000  12 34 01 00 00 01 ...» в Card
```

---

## 5. Сборка и проводка (что понадобится в Android-проекте)

### 5.1 `CMakeLists.txt` под NDK

Для пути `buildQuery` нам нужен **только** `engine/dns.c` + мост. Ни
`decor/print.c`, ни `command/*.c`, ни `main.c` не требуются — подчёркнуто
показывает, что ядро минимально.

```cmake
# app/src/main/cpp/CMakeLists.txt
cmake_minimum_required(VERSION 3.22)
project(dnslab C)

set(CMAKE_C_STANDARD 17)
add_compile_options(-Wall -Wextra -Werror)

add_library(dnslab SHARED
    dnslab_jni.c
    dnslab/engine/dns.c          # <-- только ядро сборки запроса
    # dnslab/net/net.c           # добавим, когда дойдём до lookup
    # dnslab/decor/print.c       # НЕ добавляем: вывод теперь в Kotlin
    # src/main.c                 # НЕТ: точка входа — MainActivity
)

target_include_directories(dnslab PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/dnslab     # чтобы #include "engine/dns.h"
)

find_library(log-lib log)
target_link_libraries(dnslab ${log-lib})   # для __android_log_print
```

В `app/build.gradle.kts`:
```kotlin
android {
    defaultConfig {
        externalNativeBuild { cmake { cppFlags += "" } }
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt") } }
}
```

### 5.2 ProGuard keep-rule (обязательно!)

R8 может переименовать `DnsNativeBridge` в релизе → C-символ не найдётся.

```proguard
-keep class com.dnslab.app.data.bridge.DnsNativeBridge { *; }
-keepclassmembers class com.dnslab.app.data.bridge.DnsNativeBridge { *; }
```

### 5.3 Разрешение сети (когда дойдём до `lookup`)

Для `buildQuery` разрешение не нужно (нет сети). Но `dns_send_udp` на Android
упадёт с `EACCES` без:
```xml
<uses-permission android:name="android.permission.INTERNET" />
```

---

## 6. Как принципы 1 и 2 отражены в этом примере

| Принцип | Где виден | Как |
|---------|-----------|-----|
| **1. Универсальная логика + отдельные потребители** | `engine/dns.c` vs Kotlin-рендерер | Ядро `dns_build_query` отдайт только байты; `hexDump`/`structuredView`/`schemaRFC` — потребитель, живёт в `ui/.../render`, ничего не знает про C |
| **1. Логика print не привязана к stdout** | перенос `parse_question`-логики в Kotlin | В новом мире CLI и Android имеют свои рендереры; общее — только данные (`ByteArray`) |
| **2. Шлём «сырьё», а не готовые представления** | JNI возвращает `ByteArray` | Никакой hex-строки/схемы не пересекает границу. 512 байт вместо пятерых строк по 64 КБ |
| **Ядро не знает о потребителе** | `dns_build_query` не менялся ни строкой | Функция та же, что и для CLI; Android — ещё один потребитель сверху |

---

## 7. Грабли, которых избегает (или нет) этот пример

✅ **Утечка строк** — нет: один `Get`/один `Release`, и `goto cleanup`
гарантирует освобождение на всех путях.

✅ **Переполнение локальных ссылок** — нет: создаём ровно одну `jbyteArray`.

✅ **Краш при возврате NULL** — есть `if (result == NULL) goto cleanup;`.

⚠️ **Pending-исключение** — если бы мы забыли `return`/`goto` после
`ThrowNew`, C продолжил бы работать в некорректном состоянии. В примере это
учтено.

⚠️ **Несовпадение id имени функции** — если package `DnsNativeBridge`
поменяется, сборка пройдёт, аRuntime упадёт с `UnsatisfiedLinkError` только
при первом вызове `buildQuery`. Keep-rule (5.2) защищает от R8.

> Новый повод для ошибки появится вместе с `lookup`: там JNI будет
> строить `List<DnsIp>` (много `NewObject`/`DeleteLocalRef`), и там
> локальные ссылки уже могут переполниться. В этом простом примере такой
> угрозы нет — он и выбран первым.

---

## 8. Что добавится в следующих примерах (превью)

| Функция | Доп. шаги сверх `buildQuery` | Новые JNI-типы |
|---------|------------------------------|---------------|
| `dns_parse_response` | парсинг ответа -> `List<DnsIp>` | `jobject`, `NewObject`, `GetMethodID`, `DeleteLocalRef` |
| `dns_send_udp` (внутри `lookup`) | таймер, обработка `dns_net_status_t` | сборка объекта-ошибки из enum-кода |
| `compose_request` (если пойдём через command-слой) | может печатать — нужно `print_all=0` | (обходной путь: дёргать ядро напрямую, минуя command) |
| `details` | compose + сеть + ответ | агрегация всех типов сразу |

Главное: каркас из этого документа (именование, marshaling строк,
`Get`/`Release`, проверка `NULL`, throw-на-ошибке) **не меняется** — только
прирастают шаги. Освоив `buildQuery`, вы освоили половину JNI.
