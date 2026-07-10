# RESUMEN — Sistema de Monitorización Ambiental y Seguridad Perimetral (v2)

> Documento de comprensión del proyecto: **qué hace el código, qué decisiones se
> tomaron y qué partes corresponden a Sistemas en Tiempo Real** (hilos, mutex,
> semáforos, prioridades, latencia). Incluye también el detalle de qué se
> corrigió respecto de la versión original (`../`, la v1).

---

## 1. Qué hace el sistema

Es un monitor para un rack/gabinete corriendo en una **Raspberry Pi 5**, que
combina dos funciones concurrentes:

1. **Monitoreo ambiental** — mide temperatura y humedad (DHT11) y temperatura y
   presión (BMP280) cada 5 s, las registra y alerta si las dos temperaturas
   difieren demasiado o la presión sale de rango.
2. **Seguridad perimetral** — vigila una barrera láser (LM393), un sensor Hall
   (puerta) y un ultrasonido (HC-SR04). Si la alarma está **armada** y se
   detecta una intrusión, dispara la alarma con latencia **< 500 ms**.

La alarma se **arma/desarma** ingresando un código **Morse** con un sensor
táctil (TTP223B): pulsación corta = punto, pulsación larga = raya. Todo lo
relevante se escribe en 3 canales de log independientes.

---

## 2. Arquitectura de tiempo real: los 5 hilos

El corazón "de tiempo real" del sistema es un diseño **multihilo POSIX
(pthreads)** donde cada responsabilidad vive en su propio hilo con su propia
cadencia. `main` inicializa el hardware, crea los hilos y espera la señal de
terminación (Ctrl+C).

| Hilo | Archivo | Cadencia | Qué hace | Naturaleza RT |
|------|---------|----------|----------|---------------|
| `env_monitor_thread` | `thread_manager.c` | Periódico, **5 s** | Lee DHT11 + BMP280, compara, encola logs | Tarea periódica de baja frecuencia |
| `security_thread` | `thread_manager.c` | **Polling 50 ms** | Lee láser/Hall/ultrasonido, dispara alarma | **Tarea crítica de latencia (< 500 ms)** |
| `morse_auth_thread` | `thread_manager.c` | Poll **10 ms** | Máquina de estados Morse, arma/desarma | Dirigida por eventos + medición de tiempo |
| `alarm_thread` | `thread_manager.c` | Poll **50 ms** | Parpadeo de LEDs según estado | Actuador, sin bloqueos largos |
| `logger_thread` | `thread_manager.c` | **Bloqueante** en semáforo | Consume la cola y escribe a disco | Consumidor productor/consumidor |

**Prioridades (`main.c`):** el `security_thread` se eleva a `SCHED_FIFO` con
prioridad 80 y el `env_monitor_thread` a 50, porque la latencia de seguridad es
el requisito temporal duro. `SCHED_FIFO` exige privilegios (`sudo`); si falla,
se avisa y el sistema sigue con planificación normal (degradación elegante).

---

## 3. Sincronización: mutex y semáforos

Todo el estado compartido vive en `system_context_t` (`thread_manager.h`) y se
protege con **5 mutex** y **3 semáforos**.

### 3.1 Mutex (exclusión mutua sobre datos compartidos)

| Mutex | Protege | Hilos que lo toman |
|-------|---------|--------------------|
| `mutex_env` | `env_data` (lecturas ambientales) | env → (lectores) |
| `mutex_security` | `security_status` | security |
| `mutex_alarm` | `alarm_state` (estado de la alarma) | security, morse |
| `mutex_morse` | `morse_ctx` (máquina de estados) | morse |
| `mutex_log_queue` | cola circular de logs | **todos** (productores) + logger |

Además, el módulo `alarm.c` tiene un **mutex interno propio** (`gpio_mtx`) que
serializa las escrituras a los pines de los LEDs, porque es un actuador que
tocan tres hilos a la vez (armar, disparar, parpadear).

### 3.2 Semáforos (señalización entre hilos)

| Semáforo | Tipo | Propósito |
|----------|------|-----------|
| `sem_log` | Contador | Un productor lo incrementa al encolar un log; `logger_thread` se bloquea en él (`sem_wait`) hasta que haya trabajo. **Es lo que evita el busy-waiting del logger.** |
| `sem_morse_complete` | Binario | Señala que se completó una autenticación de admin válida. |
| `sem_alarm` | Binario | Reservado para señalización de cambios de alarma (extensible). |

### 3.3 Jerarquía de bloqueos (prevención de deadlocks)

Regla que sigue el código para evitar interbloqueos: **nunca se anidan dos mutex
de estado**. Cuando un hilo necesita más de un recurso, primero copia lo que
necesita bajo un mutex, lo libera, y recién entonces toma el siguiente. Ejemplos
concretos:

- `morse_auth_thread` actualiza la máquina bajo `mutex_morse`, **copia la
  secuencia a una variable local, libera `mutex_morse`** y sólo después toma
  `mutex_alarm`. Nunca los tiene ambos a la vez.
- `security_thread` actualiza `security_status` bajo `mutex_security`, lo
  libera, y toma `mutex_alarm` por separado.
- `log_enqueue` toma únicamente `mutex_log_queue` y nada más.

---

## 4. Los patrones de tiempo real, en detalle

### 4.1 Patrón productor/consumidor para el logging (lo más importante)

El logging usa una **cola circular acotada** (`log_queue[64]`) con el patrón
productor/consumidor clásico:

```
env/security/morse  ──log_enqueue()──►  [cola circular]  ──sem_post(sem_log)──►
                                                                                │
                                             logger_thread  ◄──sem_wait(sem_log)┘
                                                    │
                                       logger_log()  (escribe a disco FUERA del mutex)
                                                    ▼
                        environmental.log / user_events.log / alerts.log
```

**Por qué es correcto para tiempo real:**

- Los hilos productores (incluido el crítico de seguridad) **nunca hacen E/S a
  disco**: sólo copian un mensaje a la cola (operación O(1), microsegundos) y
  siguen. La E/S lenta (`fprintf`/`fflush`) ocurre en `logger_thread`, que no
  tiene requisitos de latencia.
- `logger_thread` se **bloquea en `sem_wait`** en lugar de encuestar: consumo de
  CPU nulo cuando no hay logs.
- La escritura a disco se hace **fuera** de `mutex_log_queue` (se copia la
  entrada bajo el mutex y se libera antes de escribir), así la sección crítica
  es mínima.

### 4.2 Latencia de seguridad < 500 ms

- El `security_thread` encuesta cada **50 ms** → la detección más lenta posible
  es ~50 ms, muy por debajo de los 500 ms exigidos.
- La sección crítica sobre `mutex_alarm` es **cortísima**: sólo lee/cambia un
  enum y sale. No hay `usleep` ni E/S dentro de ningún mutex de estado.
- El parpadeo de LEDs (que sí duerme) se hizo migrar al `alarm_thread`, que lo
  ejecuta **fuera de toda sección crítica**.

### 4.3 Máquina de estados Morse (dirigida por tiempo)

`morse_auth.c` implementa una FSM no bloqueante que **mide el tiempo real** de
cada pulsación con un reloj monótono:

```
IDLE ──(flanco ↑)──► PRESSED ──(flanco ↓, held ≤350ms)──► '.'  ┐
                        │      ──(flanco ↓, held ≥250ms)──► '-'  ├─► GAP
                        └──(held >3s)──► STUCK_FINGER             ┘
GAP ──(flanco ↑)──► PRESSED (siguiente símbolo)
GAP ──(silencio ≥500ms con símbolos)──► SEQUENCE_COMPLETE ──► validar
```

Timeouts implementados de verdad: `MORSE_DOT_MAX_MS` (punto), `MORSE_DASH_MIN_MS`
(raya), `INTER_SYMBOL_TIMEOUT_MS` (cierre de secuencia) y
`STUCK_FINGER_TIMEOUT_MS` (dedo trabado → descarta por seguridad).

---

## 5. Cambios respecto de la v1 (qué estaba mal y qué se corrigió)

La v2 nació de una auditoría de la v1. Los defectos encontrados (verificados
empíricamente) y su corrección:

| # | Problema en v1 | Evidencia | Corrección en v2 |
|---|----------------|-----------|-------------------|
| 1 | `bmp280.c` **no compilaba**: `adc_T` fuera de alcance en la compensación de presión | `error: 'adc_T' undeclared` | Se comparte `t_fine` como variable de módulo y se implementa la compensación de presión completa del datasheet |
| 2 | **API de lgpio inventada** (`lgpio_open/write/read/...`) que no existe en la librería real | No enlaza con `liblgpio` | **Capa HAL `gpio_hal`** con backend real (`lgGpiochipOpen`, `lgGpioClaimOutput`, …) y backend de **simulación** |
| 3 | **Pipeline de logging muerto**: nadie hacía `sem_post(sem_log)` ni encolaba; `logger_thread` quedaba bloqueado para siempre y los 3 logs quedaban vacíos | Los `printf` iban a stdout, no a los archivos | `log_enqueue()` + `sem_post`; el logger consume y escribe. **Verificado: `environmental.log` se escribe cada 5 s** |
| 4 | **Morse roto**: el hilo pasaba siempre `duración = 0` → jamás se podía generar una raya | — | La FSM recibe el instante monótono y mide la duración real |
| 5 | **Morse no acumulaba símbolos**: tras el 1er símbolo saltaba a `SEQUENCE_COMPLETE` sin retorno → `..--` era inalcanzable | **El test unitario fallaba (12/13)** | FSM con estado `GAP` que acumula hasta el silencio de cierre. **Test: 17/17** |
| 6 | **Secciones críticas larguísimas**: `alarm_thread` dormía ~1.2 s y `security_thread` parpadeaba ~1 s **reteniendo `mutex_alarm`** | Rompía el poll de 50 ms y la latencia | Copiar estado bajo mutex y actuar/dormir **fuera**; parpadeo sólo en `alarm_thread` |
| 7 | `hcsr04` podía **colgarse**: el primer `while` de espera de eco no tenía timeout | — | Timeout de 30 ms en ambos bucles |
| 8 | `alarm.c` con estado global **sin protección**, tocado por 3 hilos | Carrera sobre el handle GPIO | Mutex interno `gpio_mtx` que serializa las escrituras |
| 9 | `logger.c`: se asignaba un `enum` al campo `const char* name` | Warnings int→puntero | Struct rediseñada e indexada por canal |
| 10 | `config.h` redefinía `__USE_POSIX199309` | Warning de redefinición | `_POSIX_C_SOURCE` + `-D_DEFAULT_SOURCE` |
| 11 | `test_integration.c` usaba `bool` sin `<stdbool.h>` | No compilaba en `-std=c11` | Se agrega el include |

Resultado: **compila sin warnings ni errores** y **todos los tests pasan
(17 + 10)**.

---

## 6. La capa HAL y los modos de compilación

Para que el sistema sea **compilable y verificable fuera de la Raspberry Pi**,
todo el acceso a GPIO pasa por `gpio_hal.h`, con dos backends elegidos en tiempo
de compilación:

- `make` → **simulación** (por defecto): estado de pines en memoria, sensores en
  reposo, DHT11/BMP280 devuelven lecturas fijas válidas. Permite ejercitar
  hilos, mutex, semáforos, Morse y logging en cualquier PC.
- `make PLATFORM=pi` → **hardware real**: define `-DUSE_LGPIO`, mapea a la API
  real de `lgpio` y enlaza `-llgpio -li2c`.

Los drivers `bmp280` y `dht11` también tienen su rama de simulación (I²C y
bit-banging no existen fuera de la Pi).

---

## 7. Cómo compilar, ejecutar y probar

```bash
cd v2

# Simulación (cualquier PC) — compila, corre y verifica la lógica
make                 # -> binario ./rack_monitor
make test            # 17 tests Morse + 10 de integración
./rack_monitor       # Ctrl+C para salir; mirar logs/*.log

# Raspberry Pi 5 (hardware real)
make PLATFORM=pi
sudo ./rack_monitor  # sudo -> necesario para lgpio y SCHED_FIFO
```

---

## 8. Mapa de archivos

```
v2/
├── include/
│   ├── config.h          Constantes, umbrales, structs y enums compartidos
│   ├── gpio_hal.h        [NUEVO] Interfaz HAL de GPIO (sim / lgpio real)
│   ├── thread_manager.h  Contexto compartido, mutex/sem, prototipos de hilos
│   ├── morse_auth.h      API de la FSM Morse
│   ├── alarm.h  logger.h  dht11.h  bmp280.h  hcsr04.h ...   drivers
│
├── src/
│   ├── main.c            Init, creación de hilos, prioridades RT, apagado
│   ├── thread_manager.c  Los 5 hilos + pipeline de logging (núcleo RT)
│   ├── gpio_hal.c        [NUEVO] Backends de simulación y lgpio real
│   ├── morse_auth.c      FSM Morse dirigida por tiempo
│   ├── alarm.c           Actuadores (LEDs) con mutex interno
│   ├── logger.c          Escritura a los 3 canales de log
│   ├── bmp280.c dht11.c hcsr04.c laser_barrier.c hall_sensor.c ttp223b.c
│
├── tests/                test_morse (17), test_integration (10), ...
├── logs/                 environmental.log, user_events.log, alerts.log
├── docs/hardware_wiring.md
├── Makefile              Modo simulación (def.) / PLATFORM=pi
└── RESUMEN.md            Este documento
```

---

## 9. Resumen de conceptos de Sistemas en Tiempo Real aplicados

- **Hilos POSIX** (`pthread_create/join`) con una tarea por responsabilidad.
- **Tareas periódicas** (5 s, 50 ms, 10 ms) mediante `sleep`/`usleep`, sin
  consumir 100 % de CPU.
- **Exclusión mutua** con 5 `pthread_mutex` sobre estado compartido +
  1 mutex de actuador.
- **Señalización** con 3 `sem_t` (semáforos POSIX), destacando el patrón
  **productor/consumidor** para desacoplar la E/S lenta del camino crítico.
- **Prioridades de planificación** `SCHED_FIFO` para el hilo crítico.
- **Latencia acotada** (< 500 ms) garantizada por polling de 50 ms y
  **secciones críticas mínimas** (nada de E/S ni esperas dentro de un mutex).
- **Prevención de deadlocks** por diseño: no se anidan mutex de estado; se copia
  y libera antes de tomar el siguiente.
- **Determinismo/robustez**: timeouts en todas las esperas de hardware para que
  ningún fallo de sensor cuelgue un hilo.
