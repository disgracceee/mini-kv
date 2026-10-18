



# mini-kv



Асинхронный TCP key-value сервер на C++20 и Windows IOCP.



Пул воркеров поверх I/O Completion Port, пре-постинг `AcceptEx`,

подсчёт ссылок на соединения, собственный length-prefixed протокол,

graceful shutdown и рантайм-статистика.



Проект — финал серии \[winsock-playground](https://github.com/disgracceee/winsock-playground),

где я последовательно прошёл путь от блокирующего эхо-сервера через `select()`

и пул потоков к полноценной асинхронной модели.



---



## Возможности



- \*\*IOCP + пул воркеров.\*\* Число воркеров = `std::thread::hardware\_concurrency()`.

- \*\*Пре-постинг `AcceptEx`.\*\* В очереди всегда висит `BACKLOG\_POSTED` готовых

 accept-операций, функции `AcceptEx` / `GetAcceptExSockaddrs` загружаются

 через `WSAIoctl` + `SIO\_GET\_EXTENSION\_FUNCTION\_POINTER`.

- **Безопасное время жизни соединений.** Интрузивный refcount (`addRef` / `release`)

 плюс флаг `closing` через `exchange(true)`: гарантирует, что `Conn` не будет

 освобождён, пока на него есть незавершённые I/O-операции, и что закрытие

 произойдёт ровно один раз.

\- \*\*Length-prefixed протокол\*\* поверх TCP: корректная обработка склеенных

 и разорванных пакетов, лимит на размер кадра.

\- \*\*Graceful shutdown\*\* по Ctrl-C / закрытию консоли: закрытие слушающего сокета,

 разрыв живых соединений, ожидание дренажа in-flight операций с таймаутом,

 пробуждение воркеров через `PostQueuedCompletionStatus`, join всех потоков.

\- \*\*Шардированное хранилище.\*\* 16 независимых таблиц со своими мьютексами,

 ключ распределяется по хешу; потоки, работающие с разными ключами,

 не блокируют друг друга.

- **Reaper-поток** закрывает соединения, простаивающие дольше таймаута.

- **Monitor-поток** раз в секунду печатает агрегированную статистику.

- **Per-thread статистика** с выравниванием по кеш-линии (`alignas(64)`),

 агрегация без атомиков в hot path.



---



## Архитектура



```

  клиенты  ───▶   |   listening socket        |

                 |   + N pre-posted AcceptEx |

                             │

                              ▼

               |     I/O Completion Port   |

                               │

      ┌──────────────┬───────┴───────┬──────────────┐

         ▼              ▼               ▼              ▼

    | worker 0 |   | worker 1 |    | worker 2 |  ..  | worker N |

         └──────────────┴───────┬───────┴──────────────┘

                                ▼

                         |    Store (KV)   |



  reaper  — закрывает idle-соединения

  monitor — печатает статистику

```



Каждый воркер крутит `GetQueuedCompletionStatus`, определяет тип завершившейся

операции по `OVERLAPPED`-контексту (`AcceptCtx` / `IoRecv` / `IoSend`),

обрабатывает её и снова ставит операцию в очередь. Состояние соединения живёт

в `Conn`, доступ к нему защищён refcount'ом.



---



## Структура



```

include/

 config.h        константы и глобальное состояние рантайма

 conn.h          Conn, AcceptCtx, addRef/release

 ctrlHandler.h   обработчик Ctrl-C

 io.h            postRecv/postSend/postAccept/onAccept, loadExtensions

 platform.h      winsock-заголовки и прагмы

 protocol.h      фрейминг и разбор команд

 registry.h      реестр живых соединений

 server.h        worker/monitor/reaper

 stats.h         Stats, per-thread счётчики

 store.h         шардированное key-value хранилище

src/              реализации

tests/            тесты

```



---



## Протокол



Каждое сообщение — 4-байтовый префикс длины (big-endian, `htonl`) и тело:



```

+--------+--------+--------+--------+----------------------------+

|          length (uint32 BE)       |  payload (length байт)     |

+--------+--------+--------+--------+----------------------------+

```





Тело — текстовая команда, токены разделены пробелами. Последний токен может

содержать пробелы (значение не нужно экранировать).



| Команда           | Ответ                     |

|-------------------|---------------------------|

| `SET key value`   | `OK`                      |

| `GET key`         | значение или `NOT\_FOUND`  |

| `DEL key`         | `OK` или `NOT\_FOUND`      |

| `PING`            | `PONG`                    |

| `DBSIZE`          | число ключей              |

| неизвестная       | `ERR unknown command`     |



Ошибки аргументов: `ERR usage: SET key value`, `ERR empty` и т.п.

Кадры длиннее `MAX\_PAYLOAD` приводят к разрыву соединения.



---



## Сборка



Требуется Windows, CMake 3.20+ и компилятор с поддержкой C++20

(MSVC 19.3x+ — используется `std::format`).



```powershell

git clone https://github.com/disgracceee/mini-kv.git

cd mini-kv

cmake -B build

cmake --build build --config Release

.\\build\\Release\\mini-kv.exe

```



Либо просто открыть папку в Visual Studio 2022 — проект подхватится

как CMake-проект.



---



## Пример использования



Сервер не работает с «голым» текстом, кадры нужно упаковывать, поэтому

проще всего проверить его так:



```python

import socket, struct



def send(sock, msg: str):

    body = msg.encode()

    sock.sendall(struct.pack("!I", len(body)) + body)



def recv(sock) -> str:

   n = struct.unpack("!I", sock.recv(4))\[0]

    return sock.recv(n).decode()



s = socket.create\_connection(("127.0.0.1", 9000))



send(s, "PING");              print(recv(s))   # PONG

send(s, "SET name mini-kv");  print(recv(s))   # OK

send(s, "GET name");          print(recv(s))   # mini-kv

send(s, "DBSIZE");            print(recv(s))   # 1

send(s, "DEL name");          print(recv(s))   # OK

send(s, "GET name");          print(recv(s))   # NOT\_FOUND

```



Вывод сервера:



```

\[main] listening on port 9000, workers = 8, accepts posted = 64

\[stat] live = 1, peak = 1, acc/s = 1, MB/s in = 0.00, MB/s out = 0.00

\[main] shutdown requested

\[main] closing live connections...

\[main] totals: accepts=1 recv=6 send=6 in=78 B out=34 B closes=1 errors=0 peak=1

```



---



## Конфигурация



Параметры в `include/config.h`:



| Параметр            | Назначение                                             |

|---------------------|--------------------------------------------------------|

| `PORT`              | порт                                                    |

| `BACKLOG\_POSTED`    | сколько `AcceptEx` держать в очереди                    |

| `MAX\_PAYLOAD`       | максимальный размер кадра                               |

| `MAX\_INBUF`         | лимит буфера накопления на соединение                   |

| `IDLE\_TIMEOUT\_MS`   | через сколько reaper убивает простаивающее соединение    |

| `DRAIN\_TIMEOUT\_MS`  | сколько ждать дренажа in-flight операций при выходе      |

| `VERBOSE`           | подробный лог соединений и команд                       |



---



## Известные ограничения и планы



Проект писался как упражнение по IOCP, а не как production-хранилище.

Что сознательно не сделано:



- **Нет персистентности\*\* — данные живут только в памяти.

- **Текстовый протокол внутри бинарного кадра\*\* — парсинг простой,

  но без экранирования и без поддержки бинарных ключей.

- **Только Windows** — код завязан на IOCP/Winsock2 by design.

- **Нет батчинга отправки** — каждый ответ уходит отдельным `WSASend`.

- **Логирование в `VERBOSE`** идёт синхронно в `std::cerr` и под нагрузкой

  само становится узким местом.



---



## Чему я научился



- Устройство IOCP и proactor-модели, отличие от reactor (`select`/`epoll`).

- Почему `AcceptEx` нужно пре-постить и как доставать адреса

  через `GetAcceptExSockaddrs`.

- Главная сложность асинхронных серверов — не I/O, а время жизни объектов:

  refcount, однократное закрытие, отмена операций через `CancelIoEx`.

- Как корректно гасить сервер: порядок закрытия сокетов, дренаж in-flight

 операций, пробуждение воркеров.

- Почему счётчики статистики лучше держать per-thread с выравниванием

  по кеш-линии, а не в общих атомиках.

- Фрейминг поверх TCP: пакеты склеиваются и рвутся, граница сообщения —

  забота приложения.

- Шардирование как первый шаг к масштабированию доступа к общим данным.



