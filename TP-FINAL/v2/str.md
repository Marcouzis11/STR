# Decisiones de diseño de Tiempo Real

Documento de análisis del código de `v2/`: qué elementos del sistema lo convierten
en un Sistema de Tiempo Real (STR), dónde están implementados y por qué se
tomó cada decisión.

---

## 1. Arquitectura general: concurrencia con hilos POSIX

El sistema **no** es un bucle secuencial único. Se descompone en **5 tareas
concurrentes**, cada una con su propio período y su propia criticidad temporal.
Esto es la decisión estructural más importante: permite que una tarea lenta
(leer el BMP280 por I2C, escribir a disco) **no bloquee** a la tarea crítica
(detectar una intrusión).

Tecnología: **pthreads (POSIX Threads)**, `-pthread` en `Makefile:2-3`.

Creación en `src/main.c:50-56`:

| Hilo | Función | Período | Criticidad |
|---|---|---|---|
| `env_monitor_thread` | Temperatura/presión (BMP280 I2C) | 5 s | Baja (soft) |
| `security_thread` | Ultrasonido + láser + Hall | 50 ms | **Crítica (hard-ish)** |
| `morse_auth_thread` | Botón táctil TTP223B + FSM Morse | 10 ms | Media |
| `alarm_thread` | Actuadores (LEDs) | 50 ms | Media |
| `logger_thread` | Escritura a disco | Event-driven | Baja (no RT) |

### Por qué esta separación es la clave del tiempo real

El requisito temporal fuerte del sistema es: **detectar una intrusión en
< 500 ms**. Si el logging a disco (`fflush()`, latencia impredecible por el
planificador de I/O del kernel) o la lectura I2C del BMP280 estuvieran en el
mismo hilo que los sensores de seguridad, el peor caso de la detección quedaría
acotado por el peor caso del disco — algo que **no se puede acotar**. Al aislar
cada actividad en su propio hilo, el WCET del camino crítico depende únicamente
de las operaciones GPIO.

---

## 2. Planificación en tiempo real: `SCHED_FIFO`

`src/main.c:58-68`

```c
struct sched_param param;
param.sched_priority = 80;
pthread_setschedparam(sec_thread, SCHED_FIFO, &param);   /* seguridad: prio 80 */
param.sched_priority = 50;
pthread_setschedparam(env_thread, SCHED_FIFO, &param);   /* ambiental: prio 50 */
```

Esta es **la** decisión que hace al sistema "de tiempo real" en sentido estricto
y no simplemente "multihilo".

- **`SCHED_FIFO`** es una política de planificación de tiempo real de Linux
  (`sched(7)`). A diferencia de la política por defecto `SCHED_OTHER` (CFS,
  *Completely Fair Scheduler*), un hilo `SCHED_FIFO` **expropia (preempta)
  inmediatamente** a cualquier hilo de prioridad menor y **no tiene quantum**:
  corre hasta que se bloquea voluntariamente o lo expropia alguien de mayor
  prioridad.
- Con CFS, la latencia de despacho de un hilo listo es **no acotada**: depende
  de cuántos procesos compitan en el sistema (`vruntime`). Con `SCHED_FIFO` la
  latencia queda acotada por el *scheduling latency* del kernel (~decenas/cientos
  de µs en un kernel estándar; µs en PREEMPT_RT).
- **Asignación de prioridades por criticidad** (aproximación a *Rate Monotonic /
  Deadline Monotonic*): seguridad = 80 (deadline 500 ms, el más exigente),
  ambiental = 50, el resto queda en `SCHED_OTHER`. El hilo con el deadline más
  ajustado recibe la prioridad más alta.
- **Degradación elegante**: `SCHED_FIFO` requiere `CAP_SYS_NICE` (root). Si
  `pthread_setschedparam()` falla se emite un warning y el sistema **sigue
  funcionando** con planificación normal, en vez de abortar. Decisión de
  robustez: el sistema es útil aunque no se ejecute con `sudo`, solo pierde
  las garantías temporales duras.

---

## 3. Sincronización: mutex de grano fino

`include/thread_manager.h:19-23`, inicializados en `src/thread_manager.c:27-31`.

Se usan **5 mutex separados** en lugar de un único "big lock" global:

```c
pthread_mutex_t mutex_env;        /* environmental_data_t  */
pthread_mutex_t mutex_security;   /* security_status_t     */
pthread_mutex_t mutex_alarm;      /* alarm_state_t         */
pthread_mutex_t mutex_morse;      /* morse_context_t       */
pthread_mutex_t mutex_log_queue;  /* cola circular de logs */
```

### Por qué grano fino y no un mutex global

Un mutex global serializaría todo el sistema: el hilo de seguridad tendría que
esperar a que el hilo ambiental terminara su lectura I2C (varios ms) solo para
escribir un `bool`. Eso introduce **bloqueo innecesario** y degrada la latencia
del camino crítico. Con un mutex por estructura de datos, los hilos solo
compiten cuando realmente comparten datos.

El único punto donde dos mutex podrían anidarse es en `security_thread`, que
toma `mutex_security` y luego `mutex_alarm` — pero **secuencialmente, no
anidados** (`src/thread_manager.c:169-186`): se libera uno antes de tomar el
otro. Esto **elimina por construcción la posibilidad de deadlock** por
ordenamiento de locks.

### Secciones críticas mínimas (la decisión más importante sobre mutex)

Regla aplicada en todo el código: **nunca dormir, nunca hacer E/S y nunca hacer
lógica pesada con un mutex tomado.**

Ejemplos concretos:

1. **`alarm_thread`** (`src/thread_manager.c:291-303`): copia el estado bajo
   mutex, lo libera, y **después** parpadea el LED (que implica `usleep()` de
   cientos de ms).
   ```c
   pthread_mutex_lock(&ctx->mutex_alarm);
   alarm_state_t state = ctx->alarm_state;   /* copia local */
   pthread_mutex_unlock(&ctx->mutex_alarm);
   if (state == ALARM_TRIGGERED) alarm_blink_led(...);  /* FUERA del mutex */
   ```
   El comentario del código señala que la v1 dormía ~1 s **con `mutex_alarm`
   tomado**: durante ese segundo, el hilo de seguridad quedaba bloqueado al
   intentar disparar la alarma → violación directa del deadline de 500 ms. Esto
   es un caso de libro de **inversión de prioridad no acotada**.

2. **`logger_thread`** (`src/thread_manager.c:95-104`): saca la entrada de la
   cola bajo `mutex_log_queue`, libera, y **luego** hace `logger_log()`
   (`fprintf` + `fflush`, es decir syscall a disco). Si el `fflush` se hiciera
   dentro del mutex, cualquier productor (incluido el hilo de seguridad, prio 80)
   quedaría bloqueado esperando al disco.

3. **`morse_auth_thread`** (`src/thread_manager.c:237-276`): actualiza la FSM
   bajo `mutex_morse`, copia la secuencia a un buffer local, libera, y valida el
   usuario / imprime / encola logs fuera del mutex, para no anidar `mutex_morse`
   con `mutex_alarm`.

4. **`security_thread`** (`src/thread_manager.c:179-196`): dentro de
   `mutex_alarm` solo se lee y escribe un `enum` y se setea un flag `fire`.
   Los `log_enqueue()` se hacen fuera.

### Mutex adicional en el driver de alarma

`src/alarm.c:18` — `static pthread_mutex_t gpio_mtx = PTHREAD_MUTEX_INITIALIZER;`

Los LEDs son un **recurso compartido de hardware** al que acceden tres hilos:
`alarm_thread` (parpadeo), `morse_auth_thread` (armar/desarmar + feedback de
símbolo) y potencialmente el de seguridad. Este mutex serializa las escrituras
GPIO para evitar carreras sobre el handle y sobre los pines. Es **independiente**
de los mutex de estado del sistema (grano fino nuevamente) y su sección crítica
son solo llamadas `gpio_write()`.

Nótese que en `alarm_blink_led()` (`src/alarm.c:56-68`) el `usleep()` está
deliberadamente **fuera** del `lock/unlock`: se toma y suelta el mutex para cada
transición de nivel, en vez de mantenerlo durante todo el parpadeo.

---

## 4. Semáforos: sincronización sin espera activa

`include/thread_manager.h:25-27`, inicializados en `src/thread_manager.c:34-36`.

```c
sem_init(&ctx->sem_log, 0, 0);            /* arranca en 0 */
sem_init(&ctx->sem_alarm, 0, 0);
sem_init(&ctx->sem_morse_complete, 0, 0);
```

- **`sem_log`** es el mecanismo de señalización del pipeline de logging. Arranca
  en **0**, de modo que `logger_thread` se bloquea en `sem_wait()`
  (`src/thread_manager.c:91`) y **no consume CPU** hasta que hay trabajo.
  La alternativa (polling con `usleep`) desperdiciaría ciclos y añadiría
  latencia de hasta un período de sondeo.
- **`sem_post()`** es **async-signal-safe** y no bloquea al productor: el hilo de
  seguridad puede señalizar al logger en tiempo O(1) acotado, sin riesgo de
  quedarse esperando.
- **`sem_morse_complete`** (`src/thread_manager.c:270`) notifica que un admin
  armó/desarmó el sistema, permitiendo que otros hilos reaccionen a eventos en
  lugar de sondear el estado.

**Diferencia de rol frente a los mutex:** mutex = *exclusión mutua* sobre datos
compartidos; semáforo = *señalización* de eventos entre hilos. El código respeta
esa separación en vez de abusar de uno para hacer lo del otro.

---

## 5. Patrón productor/consumidor con cola circular no bloqueante

`src/thread_manager.c:61-107` + `include/thread_manager.h:33-36`

El logging es el punto donde una tarea de tiempo real se encuentra con una
operación de latencia impredecible (el disco). La solución es desacoplarlos:

```
[env]      ─┐
[security] ─┼─► log_enqueue() ──► cola circular (64) ──► logger_thread ──► disco
[morse]    ─┘   (rápido, acotado)      + sem_post          sem_wait
```

Decisiones de tiempo real en este diseño:

- **Cola circular de tamaño fijo** (`log_entry_t log_queue[64]`, `LOG_QUEUE_SIZE
  64`): memoria **preasignada estáticamente** dentro de `system_context_t`. No
  hay `malloc()` en el camino de ejecución — `malloc` tiene tiempo de ejecución
  no acotado (puede pedir páginas al kernel) y es veneno para un STR.
- **El productor nunca se bloquea**: si la cola está llena, `log_enqueue()`
  **descarta el mensaje** y devuelve `-1` (`src/thread_manager.c:74-82`). Esta es
  una decisión de diseño explícita: es preferible **perder una línea de log**
  antes que hacer que el hilo de seguridad (prioridad 80) se quede esperando
  espacio. Se sacrifica completitud de datos para preservar el determinismo
  temporal.
- **Una ranura reservada** (`next != tail` en vez de contador) para distinguir
  cola llena de cola vacía sin necesidad de una variable extra.
- **El formateo (`vsnprintf`) se hace ANTES de tomar el mutex**
  (`src/thread_manager.c:66-69`): dentro de la sección crítica solo queda una
  copia de estructura y una actualización de índice → sección crítica de tiempo
  constante y mínima.

---

## 6. Temporización: reloj monótono y esperas acotadas

### `CLOCK_MONOTONIC`

`include/gpio_hal.h:34-38`

```c
static inline int64_t gpio_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}
```

Decisión crítica: se usa **`CLOCK_MONOTONIC`, no `CLOCK_REALTIME`/`time()`**.
`CLOCK_REALTIME` puede **saltar hacia atrás o hacia adelante** (NTP, cambio
manual de hora, horario de verano). Si la FSM de Morse midiera duraciones con
el reloj de pared, un ajuste de NTP podría convertir un punto en una raya o
provocar un timeout espurio. `CLOCK_MONOTONIC` solo avanza y es inmune a eso.

Se declara `_POSIX_C_SOURCE 199309L` (`include/config.h:5-7`) precisamente para
exponer `clock_gettime()` de forma portable.

Es `static inline` en el header: evita el overhead de llamada a función en un
camino que se ejecuta miles de veces por segundo (bucles de medición del HC-SR04).

### Timeouts en todos los bucles de espera de hardware

Ningún bucle de sondeo de hardware puede esperar indefinidamente. Un
`while (gpio_read(...) == 0);` sin salida es un **bloqueo infinito** que mata al
sistema entero.

- **HC-SR04** (`src/hcsr04.c:6, 25-33`): `HCSR04_ECHO_TIMEOUT_US 30000`. Si el
  eco no llega en 30 ms (~5 m ida y vuelta), la función retorna `255` (valor
  centinela de "sin lectura") en vez de colgarse. El comentario indica que la
  v1 **podía colgarse aquí** — un solo sensor desconectado habría congelado el
  hilo de máxima prioridad para siempre.
  **Esto acota el WCET de la iteración de `security_thread`.**
- **DHT11** (`src/dht11.c:51-74`): timeout de 100 ms en cada uno de los bucles
  de bit-banging.

### Períodos definidos como constantes

`include/config.h:27-39` centraliza todos los parámetros temporales:
`ENV_READ_INTERVAL_SEC 5`, `SECURITY_POLL_US 50000`, `MORSE_DOT_MAX_MS`,
`MORSE_DASH_MIN_MS`, `INTER_SYMBOL_TIMEOUT_MS`, `STUCK_FINGER_TIMEOUT_MS`, etc.
Tenerlos en un solo lugar permite razonar sobre el modelo temporal del sistema
(períodos vs. deadlines) sin buscar números mágicos por el código.

### Elección de los períodos de sondeo

- **Seguridad: 50 ms** (`src/thread_manager.c:198`). El deadline es 500 ms; con
  un período de sondeo de 50 ms el peor caso de detección es
  `50 ms (sondeo) + tiempo de lectura + latencia de planificación` ≈ **un orden
  de magnitud por debajo del deadline**. Ese margen (10×) absorbe jitter del
  planificador y variabilidad del sensor.
- **Morse: 10 ms** (`src/thread_manager.c:278`). Se necesita distinguir un punto
  (< 250 ms) de una raya (≥ 250 ms) y aplicar un antirrebote de 80 ms. Con 10 ms
  de resolución, el error de cuantización en la medición de la pulsación es
  ≤ 10 ms → despreciable frente al umbral de 250 ms.
- **Ambiental: 5 s**. El proceso físico (temperatura/presión de un rack) es
  lentísimo comparado con eso; muestrear más rápido solo gastaría CPU y ancho de
  banda del bus I2C.

---

## 7. Máquina de estados dirigida por tiempo (Morse)

`src/morse_auth.c:102-155`, estados en `include/config.h:74-80`.

`morse_auth_update(ctx, pressed, now_ms)` recibe el **instante monótono actual**
como parámetro. Consecuencias de tiempo real:

- La FSM **no duerme, no bloquea, no hace E/S**: es una función pura de
  transición de estados que se ejecuta en tiempo constante. Todo el "esperar"
  lo hace el hilo llamador. Esto la hace **determinista y testeable** sin
  hardware (`tests/test_morse.c` puede inyectar tiempos simulados).
- **Detección de flancos** (`rising`/`falling`, líneas 105-106) en lugar de leer
  niveles: se reacciona a *eventos* (transiciones), no a estados.
- **Timeouts como transiciones de estado**: `INTER_SYMBOL_TIMEOUT_MS` cierra la
  secuencia, `STUCK_FINGER_TIMEOUT_MS` (3 s) detecta un botón trabado y descarta
  la secuencia. Un STR debe tener una respuesta definida para "el evento
  esperado nunca llegó"; aquí se implementa explícitamente
  (`MORSE_STATE_STUCK_FINGER`).

### Antirrebote (debouncing) por software

`src/thread_manager.c:206, 214-230` — `MORSE_DEBOUNCE_MS 80`.

Un contacto mecánico/capacitivo genera múltiples transiciones espurias en pocos
ms. Se exige que el nivel se mantenga estable 80 ms antes de aceptarlo como
cambio real. Se implementa **sin bloquear**: se registra el instante del primer
cambio y se compara contra `now_ms` en iteraciones sucesivas, en lugar de hacer
`usleep(80000)` — que congelaría el hilo y desperdiciaría 8 períodos de sondeo.

---

## 8. HAL: abstracción de hardware con dos backends

`include/gpio_hal.h`, `src/gpio_hal.c`, selección en `Makefile:10-13`.

```
make                 → backend de SIMULACIÓN (cualquier PC)
make PLATFORM=pi     → backend REAL (lgpio + i2c, Raspberry Pi 5)
```

Relevancia para tiempo real:

- La selección es en **tiempo de compilación** (`#ifdef USE_LGPIO`), no en
  tiempo de ejecución. **No hay un `if` ni un puntero a función en el camino
  crítico**: el binario de producción contiene únicamente las llamadas reales a
  lgpio, con coste cero de indirección.
- Permite **verificar toda la lógica concurrente** (hilos, mutex, semáforos,
  productor/consumidor, FSM Morse) sin hardware. Los errores de concurrencia son
  los más difíciles de reproducir; poder ejecutarlos en PC habilita herramientas
  como Helgrind/TSan.
- El backend de simulación devuelve los niveles "de reposo" (línea alta) para no
  disparar falsas alarmas durante una demo (`src/gpio_hal.c:64-67`).

### Tecnología GPIO: `lgpio`, no `wiringPi` ni `sysfs`

`src/gpio_hal.c:8-43`. En la Raspberry Pi 5 el GPIO lo maneja el chip **RP1**, y
la librería `lgpio` accede vía el **interfaz `gpiochip` de carácter del kernel
(`/dev/gpiochipN`)**, que es la API moderna de Linux. Se intenta `lgGpiochipOpen(4)`
(RP1 en la Pi 5) con fallback a `0` (modelos anteriores). Esto es sensiblemente
más rápido y determinista que el viejo `/sys/class/gpio` (sysfs), que requería
`open`/`read`/`close` de ficheros de texto por cada operación.

---

## 9. Decisión explícita de descartar el DHT11

`src/thread_manager.c:116-124` y `src/dht11.c:6-15`.

El DHT11 usa un protocolo 1-Wire propietario implementado por **bit-banging**,
que exige medir pulsos de decenas de microsegundos desde espacio de usuario.

**En Linux esto no es determinista**: cualquier interrupción del kernel o
expropiación del planificador durante el muestreo corrompe la trama. En la
Raspberry Pi 5 el problema se agrava y produce lecturas cero o inválidas.

La decisión tomada — **deshabilitar el sensor y usar solo el BMP280 por I2C** —
es en sí misma una decisión de tiempo real: se elige un sensor cuyo protocolo
está implementado por un **controlador hardware** (el bus I2C del SoC) y por
tanto es inmune al jitter del planificador, en lugar de uno que depende de que
el software cumpla timings de µs que el SO no puede garantizar. La alternativa
correcta, documentada en el código, sería un **overlay de kernel** (`/sys/bus/iio`)
o un microcontrolador auxiliar.

---

## 10. Otras decisiones que favorecen el determinismo

- **Sin asignación dinámica de memoria en ejecución**: `system_context_t` (con
  la cola de logs de 64 entradas incluida) es una variable **automática en el
  stack de `main`** (`src/main.c:35`), pasada por puntero a todos los hilos.
  Cero `malloc`/`free` en régimen permanente → sin fragmentación ni latencia
  impredecible del asignador.
- **Buffers de tamaño fijo**: `MORSE_BUFFER_SIZE 32`, `MAX_LOG_LINE_LENGTH 256`
  (`include/config.h:41-42`), con `vsnprintf`/`strncpy` acotados. El uso de
  memoria del sistema es conocido y constante en tiempo de compilación.
- **`memset` completo del contexto** antes de inicializar mutex y semáforos
  (`src/thread_manager.c:25`): sin estado indeterminado al arrancar.
- **Apagado ordenado**: manejador de `SIGINT`/`SIGTERM` (`src/main.c:20-23, 32-33`)
  que solo escribe una `volatile bool` — la mínima acción posible dentro de un
  manejador de señal. El trabajo real (`pthread_cancel` + `pthread_join`,
  `src/main.c:76-86`) se hace en el hilo principal, y `cleanup_system_context()`
  destruye mutex y semáforos.
- **`volatile bool running`** (`src/main.c:18`) para que el compilador no
  optimice la lectura del flag en el bucle principal.
- **Optimización `-O2`** (`Makefile:2`) más `-Wall -Wextra`: código más rápido
  (menor WCET) con verificación estática agresiva.
- **Canales de log separados** (`src/logger.c:20-24`): `environmental.log`,
  `user_events.log`, `alerts.log`. Las alertas críticas no quedan sepultadas
  entre miles de líneas de telemetría ambiental, y cada canal se puede consumir
  a su propio ritmo.

---

## 11. Resumen: tecnologías de tiempo real empleadas

| Tecnología | Dónde | Para qué |
|---|---|---|
| **pthreads** | `main.c:50-56` | Concurrencia; aislar el camino crítico |
| **`SCHED_FIFO` + prioridades** | `main.c:58-68` | Expropiación y latencia de despacho acotada |
| **`pthread_mutex_t` (×6, grano fino)** | `thread_manager.h:19-23`, `alarm.c:18` | Exclusión mutua con mínimo bloqueo |
| **`sem_t` (POSIX)** | `thread_manager.h:25-27` | Señalización de eventos sin espera activa |
| **Cola circular estática + prod./cons.** | `thread_manager.c:61-107` | Desacoplar RT del disco; productor no bloqueante |
| **`clock_gettime(CLOCK_MONOTONIC)`** | `gpio_hal.h:34-38` | Medición de tiempo inmune a saltos de reloj |
| **Timeouts en toda espera de HW** | `hcsr04.c`, `dht11.c` | Acotar el WCET; evitar bloqueo infinito |
| **FSM dirigida por tiempo** | `morse_auth.c:102-155` | Lógica determinista, sin bloqueo |
| **Antirrebote no bloqueante** | `thread_manager.c:214-230` | Filtrar ruido sin congelar el hilo |
| **HAL con `#ifdef` (coste cero)** | `gpio_hal.c` | Sin indirección en runtime; testeable sin HW |
| **`lgpio` / chardev + I2C** | `gpio_hal.c:8-43`, `bmp280.c` | Acceso a HW moderno y de baja latencia |
| **Memoria 100 % estática** | `main.c:35`, `config.h` | Sin latencia impredecible del asignador |

---

## 12. Limitaciones y posibles mejoras

Para ser honestos sobre el alcance del sistema, estos puntos **no** están
resueltos y serían el siguiente paso hacia un STR duro:

1. **Falta protocolo de herencia de prioridad en los mutex.** Los mutex se
   inicializan con `NULL` como atributo (`thread_manager.c:27-31`), es decir
   `PTHREAD_PRIO_NONE`. Si un hilo de baja prioridad retiene `mutex_alarm` y es
   expropiado, el hilo de seguridad (prio 80) queda bloqueado por tiempo no
   acotado: **inversión de prioridad**. Se resolvería con
   `pthread_mutexattr_setprotocol(&attr, PTHREAD_PRIO_INHERIT)`.
2. **`sleep()`/`usleep()` producen deriva acumulativa.** Los períodos se
   implementan como "trabajo + dormir N", de modo que el período real es
   `N + tiempo_de_ejecución` y se desplaza con el tiempo. Lo correcto es
   `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &siguiente_activacion, NULL)`
   calculando el instante absoluto del próximo despertar.
3. **Sin `mlockall(MCL_CURRENT | MCL_FUTURE)`.** Un fallo de página en el camino
   crítico introduce latencia de disco impredecible; bloquear la memoria en RAM
   lo evita.
4. **La prioridad se fija después de `pthread_create()`**, dejando una ventana
   en la que el hilo corre con planificación normal. Es preferible usar
   `pthread_attr_setschedpolicy`/`setschedparam` + `PTHREAD_EXPLICIT_SCHED` al
   crearlo.
5. **Espera activa (busy-wait) en `hcsr04_read_distance()`** con prioridad
   `SCHED_FIFO` 80: en un sistema monoprocesador podría monopolizar la CPU hasta
   30 ms. Está acotado por el timeout, pero en una máquina con menos núcleos
   convendría usar interrupciones GPIO (`lgGpioSetAlertsFunc`) en lugar de sondeo.
6. **`alarm_thread` sondea cada 50 ms** en vez de bloquearse en `sem_alarm`
   (que está creado pero no se usa para eso): el sistema ya tiene el semáforo
   listo para convertirlo en un hilo puramente dirigido por eventos.
7. **`pthread_cancel` sin puntos de cancelación definidos** puede dejar un mutex
   tomado si el hilo se cancela dentro de una sección crítica. Sería más seguro
   un flag `volatile bool` compartido y salida limpia del bucle.
8. **El kernel es Linux estándar, no PREEMPT_RT.** Las garantías son de tiempo
   real *blando*: la latencia de despacho está acotada en la práctica pero no
   formalmente. Un kernel PREEMPT_RT reduciría el peor caso de cientos de µs a
   decenas.
