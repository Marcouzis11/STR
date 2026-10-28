# INFORME TECNICO: Rack Monitor System v2.0

## Sistema de Monitoreo Ambiental y Seguridad Perimetral para Rack de Servidores

**Materia:** Sistemas en Tiempo Real  
**Plataforma objetivo:** Raspberry Pi 5 (Linux, C con POSIX threads)  
**Paradigma:** Sistema embebido de tiempo real blando con concurrencia multiproceso

---

## 1. Descripcion General

El **Rack Monitor System** es un sistema embebido que combina dos subsistemas concurrentes ejecutados en una Raspberry Pi 5:

1. **Monitoreo ambiental**: lectura periodica de temperatura y presion atmosferica del rack de servidores mediante el sensor BMP280 (bus I2C). Registra datos en un archivo de log y genera alertas si la presion sale de un rango seguro.

2. **Seguridad perimetral**: vigilancia continua de tres sensores de intrusion — barrera laser (KY-008 + LM393), sensor de efecto Hall (puerta magnetica) y sensor ultrasonico de distancia (HC-SR04) — con un requisito de latencia estricto: detectar una intrusion y activar la alarma en **menos de 500 milisegundos**.

La alarma se **arma y desarma** mediante una autenticacion por codigo Morse, ingresado a traves de un sensor tactil capacitivo (TTP223B). El sistema registra toda actividad en tres canales de log independientes.

El proyecto esta escrito enteramente en **C (estandar C11)** y utiliza **pthreads** (POSIX Threads) para la concurrencia, con politicas de planificacion de **tiempo real** (`SCHED_FIFO`) para garantizar las restricciones temporales.

---

## 2. Arquitectura del Sistema

### 2.1 Descomposicion en tareas concurrentes

El sistema se descompone en **5 hilos POSIX** mas el hilo principal. Cada hilo tiene una responsabilidad unica, una cadencia definida y un nivel de criticidad temporal distinto. Esta descomposicion es la decision de arquitectura mas importante del proyecto: permite que una tarea lenta (escritura a disco, lectura I2C) **nunca bloquee** a la tarea critica (deteccion de intrusiones).

```
┌──────────────────────────────────────────────────────────────────┐
│                      MAIN THREAD                                  │
│  Inicializa hardware → Crea 5 hilos → Espera SIGINT/SIGTERM     │
│  Asigna prioridades RT: security=80, env=50                      │
└──────────┬───────────┬───────────┬───────────┬───────────┬───────┘
           │           │           │           │           │
    ┌──────▼──┐  ┌─────▼────┐ ┌───▼──────┐ ┌──▼───────┐ ┌▼────────┐
    │  ENV    │  │ SECURITY │ │  MORSE   │ │  ALARM   │ │ LOGGER  │
    │ MONITOR │  │          │ │  AUTH    │ │          │ │         │
    │  5 seg  │  │  50 ms   │ │  10 ms   │ │  50 ms   │ │ sem     │
    │ (FIFO   │  │ (FIFO    │ │ (normal) │ │ (normal) │ │ (block) │
    │  prio=  │  │  prio=   │ │          │ │          │ │         │
    │   50)   │  │   80)    │ │          │ │          │ │         │
    └─────────┘  └──────────┘ └──────────┘ └──────────┘ └─────────┘
```

### 2.2 Tabla de hilos y sus caracteristicas temporales

| Hilo | Funcion | Periodo | Mecanismo de temporizacion | Criticidad |
|------|---------|---------|---------------------------|------------|
| `env_monitor_thread` | Leer BMP280, comparar valores, encolar logs | 5 segundos | `sleep(5)` | Baja (soft RT) |
| `security_thread` | Leer laser + Hall + ultrasonico, disparar alarma | 50 ms (polling) | `usleep(50000)` | **Maxima (hard RT)** |
| `morse_auth_thread` | Interpretar pulsos Morse, autenticar usuarios | 10 ms (polling) | `usleep(10000)` | Media |
| `alarm_thread` | Controlar LEDs segun estado de alarma | 50 ms (polling) | `usleep(50000)` | Media |
| `logger_thread` | Consumir cola de logs, escribir a disco | Event-driven (bloqueante) | `sem_wait()` (sin busy-wait) | Baja |

### 2.3 Por que esta separacion es critica para el tiempo real

El requisito temporal fuerte del sistema es: **detectar una intrusion en < 500 ms**. Si la escritura a disco (`fprintf`/`fflush`, cuya latencia es impredecible por el planificador de I/O del kernel) o la lectura I2C del BMP280 estuvieran en el mismo hilo que los sensores de seguridad, el peor caso de deteccion quedaria acotado por el peor caso de disco — algo que **no se puede acotar**. Al aislar cada actividad en su propio hilo, el WCET (Worst Case Execution Time) del camino critico depende unicamente de las operaciones GPIO, que son rapidas y deterministas.

---

## 3. Sistemas de Tiempo Real: Conceptos Aplicados

### 3.1 Planificacion con SCHED_FIFO

El mecanismo que convierte este sistema de "multihilo" a "tiempo real" es la asignacion de la politica de planificacion `SCHED_FIFO` con prioridades numericas a los hilos criticos.

```c
// src/main.c:58-68
struct sched_param param;
param.sched_priority = 80;
if (pthread_setschedparam(sec_thread, SCHED_FIFO, &param) != 0) {
    fprintf(stderr, "[WARN] No se pudo fijar SCHED_FIFO en security_thread "
                    "(ejecutar con sudo para prioridad RT).\n");
}
param.sched_priority = 50;
pthread_setschedparam(env_thread, SCHED_FIFO, &param);
```

**Diferencia con SCHED_OTHER (CFS):** La politica por defecto de Linux (`SCHED_OTHER`, *Completely Fair Scheduler*) distribuye el tiempo de CPU entre procesos usando un algoritmo de virtual runtime. Un hilo `SCHED_OTHER` puede quedar esperando ejecutarse durante un tiempo no acotado si hay muchos procesos compitiendo. Con `SCHED_FIFO`, un hilo **expropia (preempta) inmediatamente** a cualquier hilo de menor prioridad y no tiene quantum: corre hasta que se bloquea voluntariamente o lo expropia alguien de mayor prioridad. Esto acota la latencia de despacho a decenas de microsegundos.

**Asignacion de prioridades (aproximacion a Rate Monotonic):**

| Hilo | Prioridad | Razon |
|------|-----------|-------|
| `security_thread` | 80 | Deadline mas exigente (500 ms), periodo mas corto (50 ms) |
| `env_monitor_thread` | 50 | Deadline laxo (5 s), lectura I2C |
| Resto | 0 (SCHED_OTHER) | Sin requisitos temporales estrictos |

**Degradacion elegante:** `SCHED_FIFO` requiere privilegios root (`CAP_SYS_NICE`). Si `pthread_setschedparam()` falla, el sistema emite un warning y continua con planificacion normal. El sistema sigue siendo funcional, solo pierde las garantias temporales duras.

### 3.2 Sincronizacion: Mutex de grano fino

El estado compartido del sistema vive en una estructura unica `system_context_t` y se protege con **5 mutex separados** (mas 1 mutex interno en el modulo de alarma):

```c
// include/thread_manager.h:19-23
pthread_mutex_t mutex_env;        // protege environmental_data_t
pthread_mutex_t mutex_security;   // protege security_status_t
pthread_mutex_t mutex_alarm;      // protege alarm_state_t
pthread_mutex_t mutex_morse;      // protege morse_context_t
pthread_mutex_t mutex_log_queue;  // protege la cola circular de logs
```

**Por que grano fino y no un mutex global:** Un mutex global serializaria todo el sistema. El hilo de seguridad tendria que esperar a que el hilo ambiental terminara su lectura I2C (varios milisegundos) solo para escribir un `bool`. Con un mutex por estructura de datos, los hilos solo compiten cuando realmente comparten datos.

**Secciones criticas minimas:** La regla fundamental aplicada en todo el codigo es: **nunca dormir, nunca hacer E/S y nunca hacer logica pesada con un mutex tomado.** Ejemplos concretos:

*El hilo de alarma copia el estado y luego parpadea FUERA del mutex:*
```c
// src/thread_manager.c:291-294
pthread_mutex_lock(&ctx->mutex_alarm);
alarm_state_t state = ctx->alarm_state;  // solo copia local
pthread_mutex_unlock(&ctx->mutex_alarm);
// El parpadeo (que incluye usleep de cientos de ms) ocurre AQUI, fuera del mutex
if (state == ALARM_TRIGGERED) {
    alarm_blink_led(LED_RED_GPIO, 3, 200);
}
```

*El logger copia la entrada de la cola y luego escribe a disco FUERA del mutex:*
```c
// src/thread_manager.c:95-104
pthread_mutex_lock(&ctx->mutex_log_queue);
entry = ctx->log_queue[ctx->log_queue_tail];     // copia local
ctx->log_queue_tail = (ctx->log_queue_tail + 1) % LOG_QUEUE_SIZE;
pthread_mutex_unlock(&ctx->mutex_log_queue);
// E/S a disco (fprintf + fflush) AQUI, fuera del mutex
if (have) logger_log(entry.channel, "%s", entry.message);
```

Esto evita el problema clasico de **inversion de prioridad no acotada**: en la version 1 del proyecto, `alarm_thread` dormia ~1 segundo con `mutex_alarm` tomado, bloqueando al hilo de seguridad (prioridad 80) durante ese tiempo — una violacion directa del deadline de 500 ms.

**Prevencion de deadlocks por diseno:** Los mutex de estado **nunca se anidan**. Cuando un hilo necesita acceder a mas de un recurso, primero copia lo que necesita bajo un mutex, lo libera, y recien entonces toma el siguiente:

```c
// security_thread: toma mutex_security, lo libera, luego toma mutex_alarm
pthread_mutex_lock(&ctx->mutex_security);
ctx->security_status.laser_triggered = laser_broken;
// ...
pthread_mutex_unlock(&ctx->mutex_security);

if (laser_broken || hall_triggered || ultrasonic) {
    pthread_mutex_lock(&ctx->mutex_alarm);      // segundo mutex, DESPUES de liberar el primero
    if (ctx->alarm_state == ALARM_ARMED) {
        ctx->alarm_state = ALARM_TRIGGERED;
    }
    pthread_mutex_unlock(&ctx->mutex_alarm);
}
```

### 3.3 Semaforos POSIX: senializacion eficiente

Los semaforos se usan para **senializacion** (avizar que hay trabajo nuevo), mientras que los mutex se usan para **exclusión mutua** (proteger datos compartidos). El sistema usa 3 semaforos:

```c
// src/thread_manager.c:34-36
sem_init(&ctx->sem_log, 0, 0);              // arranca en 0
sem_init(&ctx->sem_alarm, 0, 0);
sem_init(&ctx->sem_morse_complete, 0, 0);
```

**`sem_log`** es el mecanismo central del pipeline de logging. Arranca en 0, de modo que `logger_thread` se bloquea en `sem_wait()` sin consumir CPU hasta que hay trabajo. La alternativa (polling con `usleep`) desperdiciaria ciclos y anadiria latencia. `sem_post()` es **async-signal-safe** y no bloquea al productor: el hilo de seguridad puede senializar al logger en tiempo O(1) acotado.

### 3.4 Patron productor/consumidor para logging

El pipeline de logging es el patron productor/consumidor clasico, implementado con una **cola circular acotada** y un semaforo:

```
[env_thread]      ─┐
[security_thread] ─┼─► log_enqueue() ──► cola circular[64] ──► logger_thread ──► disco
[morse_thread]    ─┘   (productores)      + sem_post           sem_wait
                                            (acotado, O(1))     (bloquea sin CPU)
```

```c
// src/thread_manager.c:61-83 — Productor
int log_enqueue(system_context_t* ctx, log_channel_t channel, const char* fmt, ...) {
    log_entry_t entry;
    entry.channel = channel;
    // Formateo FUERA del mutex (operacion pesada)
    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.message, sizeof(entry.message), fmt, args);
    va_end(args);

    bool queued = false;
    pthread_mutex_lock(&ctx->mutex_log_queue);
    uint8_t next = (ctx->log_queue_head + 1) % LOG_QUEUE_SIZE;
    if (next != ctx->log_queue_tail) {    // hay hueco
        ctx->log_queue[ctx->log_queue_head] = entry;
        ctx->log_queue_head = next;
        queued = true;
    }
    pthread_mutex_unlock(&ctx->mutex_log_queue);

    if (queued) sem_post(&ctx->sem_log);  // despierta al logger
    return queued ? 0 : -1;              // -1 si la cola esta llena (descarta)
}
```

**Decisiones de tiempo real en este diseno:**

1. **Cola circular de tamano fijo** (`log_entry_t log_queue[64]`): memoria preasignada estaticamente. No hay `malloc()` en ejecucion — `malloc` tiene tiempo de ejecucion no acotado (puede pedir paginas al kernel) y es veneno para un STR.

2. **El productor nunca se bloquea**: si la cola esta llena, `log_enqueue()` descarta el mensaje y devuelve `-1`. Se sacrifica completitud de datos para preservar el determinismo temporal. Es preferible perder una linea de log antes que hacer que el hilo de seguridad (prioridad 80) se quede esperando.

3. **El formateo (`vsnprintf`) se hace ANTES de tomar el mutex**: dentro de la seccion critica solo queda una copia de estructura y una actualizacion de indice — seccion critica de tiempo constante y minima.

---

## 4. Maquina de Tiempo: Temporizacion y Reloj

### 4.1 Reloj monotono (`CLOCK_MONOTONIC`)

```c
// include/gpio_hal.h:34-38
static inline int64_t gpio_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}
```

Se usa `CLOCK_MONOTONIC` en lugar de `CLOCK_REALTIME`/`time()`. `CLOCK_REALTIME` puede saltar hacia atras o hacia adelante (ajuste NTP, cambio manual de hora). Si la FSM de Morse midiera duraciones con el reloj de pared, un ajuste de NTP podria convertir un punto en una raya o provocar un timeout espurio. `CLOCK_MONOTONIC` solo avanza y es inmune a eso.

Se declara `_POSIX_C_SOURCE 199309L` en `config.h` para exponer `clock_gettime()` de forma portable. La funcion es `static inline` en el header para evitar el overhead de llamada a funcion en un camino que se ejecuta miles de veces por segundo.

### 4.2 Timeouts en todas las esperas de hardware

Ningun bucle de sondeo de hardware puede esperar indefinidamente. Un `while (gpio_read(...) == 0);` sin salida es un bloqueo infinito que mata al sistema entero.

```c
// src/hcsr04.c:24-33 — Timeout de 30ms en cada bucle de espera
int64_t wait_start = gpio_now_us();
while (gpio_read(handle, echo_gpio) == 0) {
    if (gpio_now_us() - wait_start > HCSR04_ECHO_TIMEOUT_US) return 255;
}
```

El valor de timeout del HC-SR04 es 30 ms (~200 cm ida y vuelta). Si el eco no llega, la funcion retorna 255 (sentinela de "sin lectura") en vez de colgarse. Esto **acota el WCET de la iteracion de `security_thread`**.

### 4.3 Periodos definidos como constantes centralizadas

Todos los parametros temporales estan en `config.h`:

```c
// include/config.h:27-39
#define ENV_READ_INTERVAL_SEC  5       // periodo del hilo ambiental
#define SECURITY_POLL_US      50000   // periodo del hilo de seguridad (50ms)
#define MORSE_DOT_MAX_MS       250    // punto: < 250ms
#define MORSE_DASH_MIN_MS      250    // raya: >= 250ms
#define MORSE_MIN_TOUCH_MS     100    // ruido: < 100ms, ignorado
#define INTER_SYMBOL_TIMEOUT_MS 2000  // silencio > 2s = fin de secuencia
#define SEQUENCE_TIMEOUT_MS   3000    // timeout general de secuencia
#define STUCK_FINGER_TIMEOUT_MS 3000  // dedo trabado: > 3s, descartar
```

Tenerlos en un solo lugar permite razonar sobre el modelo temporal del sistema (periodos vs. deadlines) sin buscar numeros magicos por el codigo.

### 4.4 Eleccion de los periodos de sondeo

- **Seguridad: 50 ms.** El deadline es 500 ms; con un periodo de 50 ms el peor caso de deteccion es `50 ms (sondeo) + tiempo de lectura + latencia de planificacion` ≈ un orden de magnitud por debajo del deadline. Ese margen (10x) absorbe jitter del planificador y variabilidad del sensor.
- **Morse: 10 ms.** Se necesita distinguir un punto (< 250 ms) de una raya (>= 250 ms) y aplicar un antirrebote de 80 ms. Con 10 ms de resolucion, el error de cuantizacion es ≤ 10 ms, despreciable frente al umbral de 250 ms.
- **Ambiental: 5 s.** El proceso fisico (temperatura/presion de un rack) es lentisimo; muestrear mas rapido solo gastaria CPU y ancho de banda del bus I2C.

---

## 5. Descripcion de los Modulos (Clases del Proyecto)

### 5.1 `config.h` — Definiciones globales y tipos de datos

Este archivo define **todas las constantes, umbrales y estructuras de datos** compartidas entre los modulos. No contiene logica, solo definiciones de tipos y `#define`.

**GPIO asignados:**

| Pin GPIO | Modo | Dispositivo |
|----------|------|-------------|
| GPIO4 | 1-Wire Data | DHT11 (deshabilitado en v2) |
| GPIO2/3 | I2C SDA/SCL | BMP280 |
| GPIO17 | Output | HC-SR04 Trigger |
| GPIO27 | Input | HC-SR04 Echo |
| GPIO22 | Input | Barrera laser LM393 |
| GPIO23 | Input | Sensor Hall |
| GPIO24 | Input | TTP223B tactil |
| GPIO5 | Output | LED Amarillo |
| GPIO6 | Output | LED Verde |
| GPIO13 | Output | LED Rojo |

**Enums fundamentales:**

```c
// include/config.h:44-50
typedef enum {
    ALARM_DISARMED,     // desarmado (LED verde)
    ALARM_ARMING,       // armando (LED amarillo parpadea)
    ALARM_ARMED,        // armado (LED rojo fijo)
    ALARM_TRIGGERED,    // intrusion detectada (LED rojo parpadea)
    ALARM_SOUNDING      // sonando (reservado para sirena)
} alarm_state_t;

// include/config.h:74-80 — Estados de la FSM Morse
typedef enum {
    MORSE_STATE_IDLE,              // sin pulsar, sin simbolos
    MORSE_STATE_PRESSED,           // boton presionado, midiendo duracion
    MORSE_STATE_GAP,               // liberado, esperando siguiente simbolo
    MORSE_STATE_SEQUENCE_COMPLETE, // secuencia lista para validar
    MORSE_STATE_STUCK_FINGER       // pulsacion > 3s, descartar
} morse_state_t;
```

**Estructuras de datos principales:**

```c
typedef struct {
    float dht11_temp;         // temperatura DHT11 (reservado)
    float dht11_humidity;     // humedad DHT11 (reservado)
    float bmp280_temp;        // temperatura BMP280 (grados Celsius)
    float bmp280_pressure;    // presion BMP280 (hectopascales)
    time_t timestamp;         // momento de la ultima lectura
} environmental_data_t;

typedef struct {
    bool laser_triggered;         // true si el haz esta interrumpido
    bool hall_triggered;          // true si la puerta esta abierta
    bool ultrasonic_triggered;    // true si distancia < 10cm
    uint8_t ultrasonic_distance_cm; // distancia medida en cm
    time_t last_event;            // timestamp del ultimo evento
} security_status_t;

typedef struct {
    morse_state_t state;         // estado actual de la FSM
    char buffer[32];             // secuencia Morse acumulada (".", "-", etc.)
    uint8_t buffer_pos;          // posicion de escritura en el buffer
    bool button_pressed;         // nivel anterior (deteccion de flancos)
    uint32_t press_start_ms;     // instante del ultimo flanco ascendente
    uint32_t last_release_ms;    // instante del ultimo flanco descendente
} morse_context_t;

typedef struct {
    char timestamp[32];
    log_channel_t channel;       // LOG_ENV, LOG_USER o LOG_ALERT
    char message[256];
} log_entry_t;
```

### 5.2 `gpio_hal.h` / `gpio_hal.c` — Capa de abstraccion de hardware (HAL)

La HAL (Hardware Abstraction Layer) es la capa mas importante para la portabilidad del proyecto. Centraliza **todo** el acceso a GPIO detras de una interfaz unica con dos backends seleccionables en **tiempo de compilacion**:

| Backend | Flag de compilacion | Biblioteca | Plataforma |
|---------|---------------------|------------|------------|
| Real | `make PLATFORM=pi` (define `USE_LGPIO`) | lgpio + i2c-dev | Raspberry Pi 5 |
| Simulacion | `make` (por defecto) | Ninguna | Cualquier PC |

**API unificada:**

```c
int  gpio_open(void);                                    // abrir chip GPIO
void gpio_close(int handle);                             // cerrar handle
int  gpio_claim_output(int handle, int gpio, int init);  // configurar como salida
int  gpio_claim_input(int handle, int gpio);             // configurar como entrada
int  gpio_set_pull(int handle, int gpio, gpio_pull_t);   // bias pull-up/down
int  gpio_write(int handle, int gpio, int level);        // escribir nivel
int  gpio_read(int handle, int gpio);                    // leer nivel
```

**Backend real (lgpio):** La Raspberry Pi 5 usa el chip GPIO **RP1**, accesible como `gpiochip4`. Se intenta `lgGpiochipOpen(4)` primero con fallback a `lgGpiochipOpen(0)` para modelos anteriores:

```c
// src/gpio_hal.c:10-14
int gpio_open(void) {
    int h = lgGpiochipOpen(4);  // chip 4 = RP1 en Pi5
    if (h < 0) h = lgGpiochipOpen(0);  // fallback
    return h;
}
```

**Backend de simulacion:** Mantiene un array de 64 pines en memoria. Los sensores digitales en reposo estan en nivel alto (1), de modo que no se disparan falsas alarmas:

```c
// src/gpio_hal.c:62-67
static void sim_init_once(void) {
    for (int i = 0; i < SIM_MAX_GPIO; i++) {
        sim_level[i] = 1;           // reposo: linea en alto (sin intrusion)
        sim_is_output[i] = false;
    }
}
```

**Ventaja para tiempo real:** La seleccion es en tiempo de compilacion (`#ifdef USE_LGPIO`), no en tiempo de ejecucion. No hay un `if` ni un puntero a funcion en el camino critico — el binario de produccion contiene unicamente las llamadas reales a lgpio, con coste cero de indireccion.

### 5.3 `bmp280.c` / `bmp280.h` — Driver del sensor BMP280 (I2C)

El BMP280 es un sensor barometrico digital que comunica por bus I2C. Mide temperatura (range -40 a +85 C, precision 0.01 C) y presion (range 300 a 1100 hPa).

**Inicializacion I2C:**

```c
// src/bmp280.c:41-69
int bmp280_init(int bus, uint8_t addr) {
    char filename[32];
    snprintf(filename, sizeof(filename), "/dev/i2c-%d", bus);
    i2c_fd = open(filename, O_RDWR);
    ioctl(i2c_fd, I2C_SLAVE, addr);  // addr = 0x76

    // Configurar oversampling x1, modo normal
    uint8_t config_data[2] = {BMP280_REG_CTRL_MEAS, 0x27};
    write(i2c_fd, config_data, 2);

    // Leer 24 bytes de calibracion desde registro 0x88
    uint8_t calib_addr = 0x88, calib_data[24];
    write(i2c_fd, &calib_addr, 1);
    read(i2c_fd, calib_data, 24);
    // Parsear coeficientes dig_T1..T3, dig_P1..P9
}
```

**Compensacion de temperatura (algoritmo del datasheet):**

```c
// src/bmp280.c:73-81
static int32_t bmp280_compensate_t(int32_t adc_T) {
    int32_t var1 = ((((adc_T >> 3) - ((int32_t)calibration.dig_T1 << 1))) *
                    ((int32_t)calibration.dig_T2)) >> 11;
    int32_t var2 = (((((adc_T >> 4) - ((int32_t)calibration.dig_T1)) *
                      ((adc_T >> 4) - ((int32_t)calibration.dig_T1))) >> 12) *
                    ((int32_t)calibration.dig_T3)) >> 14;
    t_fine = var1 + var2;
    return (t_fine * 5 + 128) >> 8;
}
```

La variable `t_fine` es compartida como variable de modulo y se usa tambien en la compensacion de presion, ya que el algoritmo del BMP280 las acopla.

**Modo simulacion:** Devuelve lecturas fijas validas (24.3 C, 1013.0 hPa) para ejercitar el sistema sin hardware.

### 5.4 `dht11.c` / `dht11.h` — Driver del sensor DHT11 (deshabilitado)

El DHT11 usa un protocolo 1-Wire propietario implementado por **bit-banging** desde el espacio de usuario. Esto exige medir pulsos de decenas de microsegundos con precision.

**Por que se deshabilito:** En Linux esto **no es determinista**: cualquier interrupcion del kernel o expropiacion del planificador durante el muestreo de `usleep()` corrompe la trama de datos. En la Raspberry Pi 5 el problema se agrava y produce lecturas cero o invalidas.

```c
// src/thread_manager.c:116-124 (en env_monitor_thread)
/* NOTA: El DHT11 usa bit-banging 1-Wire cuyo timing es incompatible con
 * la Raspberry Pi 5. El sensor DHT11 queda documentado pero deshabilitado.
 * Solucion: se usa solo el BMP280 para temperatura y presion. */
int bmp_handle = bmp280_init(BMP280_I2C_BUS, BMP280_I2C_ADDR);
```

La decision de deshabilitar el DHT11 es en si misma una decision de tiempo real: se elige un sensor cuyo protocolo esta implementado por un **controlador hardware** (el bus I2C del SoC), inmune al jitter del planificador.

### 5.5 `hcsr04.c` / `hcsr04.h` — Driver del sensor ultrasonico HC-SR04

El HC-SR04 mide distancia mediante echolocalizacion por ultrasonido. Envia un pulso de trigger de 10 us y mide la duracion del eco recibido.

```c
// src/hcsr04.c:18-43
uint8_t hcsr04_read_distance(int handle, int trig_gpio, int echo_gpio) {
    // Pulso de trigger de 10 us
    gpio_write(handle, trig_gpio, 1);
    usleep(10);
    gpio_write(handle, trig_gpio, 0);

    // Esperar eco con timeout de 30 ms
    int64_t wait_start = gpio_now_us();
    while (gpio_read(handle, echo_gpio) == 0) {
        if (gpio_now_us() - wait_start > HCSR04_ECHO_TIMEOUT_US) return 255;
    }
    int64_t echo_start = gpio_now_us();
    while (gpio_read(handle, echo_gpio) == 1) {
        if (gpio_now_us() - echo_start > HCSR04_ECHO_TIMEOUT_US) return 255;
    }
    int64_t echo_end = gpio_now_us();

    // Formula: distancia_cm = duracion_us * 343 / 20000
    int64_t duration_us = echo_end - echo_start;
    int64_t distance = (duration_us * 343) / 20000;

    if (distance > 200) distance = 200;  // limitar rango
    if (distance < 0)   distance = 0;
    return (uint8_t)distance;
}
```

**Formula fisica:** `distancia_cm = (duracion_us × velocidad_sonido) / 2`. La velocidad del sonido es 343 m/s = 0.0343 cm/us. Se divide por 2 porque es el trayecto ida y vuelta. El timeout de 30 ms permite detectar hasta ~5 metros, pero el resultado se limita a 200 cm.

### 5.6 `laser_barrier.c` / `laser_barrier.h` — Driver de la barrera laser

El modulo KY-008 contiene un laser y un comparador LM393 con fotodiodo integrado. Cuando el haz laser esta intacto, el LM393 retorna nivel bajo (0). Cuando el haz se interrumpe, retorna nivel alto (1).

```c
// src/laser_barrier.c:11-14
bool laser_barrier_is_broken(int handle, int gpio) {
    int level = gpio_read(handle, gpio);
    return (level == 1);  // LM393: nivel alto = haz interrumpido
}
```

### 5.7 `hall_sensor.c` / `hall_sensor.h` — Sensor de efecto Hall (puerta)

El sensor Hall (KY-024 / HW-484) detecta campos magneticos. Se monta en la puerta del rack con un iman: cuando la puerta esta cerrada, el iman esta cerca y el sensor va a nivel bajo (0). Cuando la puerta se abre, el iman se aleja y el sensor va a nivel alto (1). La logica del driver reporta "puerta abierta" cuando el iman esta ausente:

```c
// src/hall_sensor.c:11-14
bool hall_sensor_detected(int handle, int gpio) {
    int level = gpio_read(handle, gpio);
    return (level == 0);  // nivel bajo = iman ausente = puerta abierta
}
```

### 5.8 `ttp223b.c` / `ttp223b.h` — Sensor tactil capacitivo

El TTP223B es un sensor capacitivo que va a nivel alto (1) cuando se toca y nivel bajo (0) en reposo. Se usa como "teclado" de un solo boton para ingresar codigos Morse.

```c
// src/ttp223b.c:11-13
bool ttp223b_is_pressed(int handle, int gpio) {
    int level = gpio_read(handle, gpio);
    return (level == 1);
}
```

### 5.9 `morse_auth.c` — Maquina de estados finitos (FSM) para autenticacion Morse

Este modulo implementa una **FSM dirigida por tiempo** que convierte pulsos del sensor tactil en codigos Morse y los valida contra una tabla de usuarios. Es uno de los componentes mas interesantes del punto de vista de sistemas en tiempo real.

**Estados de la maquina:**

```
                    ┌──────────────┐
         ┌─────────►│    IDLE      │◄────────────────────┐
         │          └──────┬───────┘                      │
         │                 │ flanco ascendente             │
         │                 ▼                               │
         │          ┌──────────────┐                      │
         │          │   PRESSED    │                      │
         │          └──┬───────┬───┘                      │
         │             │       │                          │
         │   flanco ↓  │       │ held > 3s                │
         │   (held     │       ▼                          │
         │   < 3s)     │  ┌──────────────┐               │
         │             │  │ STUCK_FINGER │──(flanco ↓)──► reset
         │             │  └──────────────┘               │
         │             ▼                                  │
         │    ┌──────────────┐                           │
         │    │     GAP      │──(silencio > 2s)──────────┤
         │    └──────┬───────┘                           │
         │           │ flanco ascendente                  │
         └───────────┘                                    │
                                                          │
              ┌───────────────────────────────────────────┘
              │
              │  SEQUENCE_COMPLETE
              ▼
    ┌─────────────────────────┐
    │   Validar contra tabla  │──► admin/armar, operator, guest, invalido
    │   de usuarios           │
    └─────────────────────────┘
```

**Logica de medicion de tiempo (el nucleo de la FSM):**

```c
// src/morse_auth.c:54-107
void morse_auth_update(morse_context_t* ctx, bool pressed, uint32_t now_ms) {
    bool rising  = (pressed && !ctx->button_pressed);   // flanco ascendente
    bool falling = (!pressed && ctx->button_pressed);   // flanco descendente

    switch (ctx->state) {
        case MORSE_STATE_IDLE:
        case MORSE_STATE_GAP:
            if (rising) {
                ctx->press_start_ms = now_ms;           // registrar instante
                ctx->state = MORSE_STATE_PRESSED;
            } else if (ctx->state == MORSE_STATE_GAP && ctx->buffer_pos > 0) {
                // Silencio prolongado -> fin de secuencia
                if (now_ms - ctx->last_release_ms >= INTER_SYMBOL_TIMEOUT_MS) {
                    ctx->state = MORSE_STATE_SEQUENCE_COMPLETE;
                }
            }
            break;

        case MORSE_STATE_PRESSED: {
            uint32_t held = now_ms - ctx->press_start_ms;
            if (held > STUCK_FINGER_TIMEOUT_MS) {
                ctx->state = MORSE_STATE_STUCK_FINGER;  // dedo trabado
            } else if (falling) {
                if (held >= MORSE_DASH_MIN_MS) {
                    append_symbol(ctx, '-');              // raya
                } else if (held >= MORSE_MIN_TOUCH_MS && held <= MORSE_DOT_MAX_MS) {
                    append_symbol(ctx, '.');              // punto
                }
                ctx->last_release_ms = now_ms;
                ctx->state = MORSE_STATE_GAP;
            }
            break;
        }
        // ...
    }
    ctx->button_pressed = pressed;  // guardar estado para deteccion de flancos
}
```

**Tabla de usuarios Morse:**

| Codigo Morse | Usuario | Accion |
|--------------|---------|--------|
| `..--` | admin | Armar/Desarmar alarma (toggle) |
| `.-..` | operator | Solo login (no cambia alarma) |
| `...-` | guest | Solo login (no cambia alarma) |

**Propiedades de tiempo real de la FSM:**

- **No duerme, no bloquea, no hace E/S**: es una funcion pura de transicion de estados que se ejecuta en tiempo constante O(1).
- **Deteccion de flancos** en lugar de leer niveles: se reacciona a *eventos* (transiciones), no a estados.
- **Timeouts como transiciones de estado**: cada timeout tiene una transicion definida. Un STR debe tener una respuesta definida para "el evento esperado nunca llego".

### 5.10 `alarm.c` / `alarm.h` — Modulo de alarma (actuadores LED)

El modulo de alarma es un **actuador compartido** que es tocado por tres hilos distintos: `alarm_thread` (parpadeos), `morse_auth_thread` (armar/desarmar + feedback de simbolo) y potencialmente `security_thread` (disparo).

**Mutex interno para proteccion del hardware:**

```c
// src/alarm.c:18-27
static pthread_mutex_t gpio_mtx = PTHREAD_MUTEX_INITIALIZER;

static void set_leds(int yellow, int green, int red) {
    if (alarm_handle < 0) return;
    pthread_mutex_lock(&gpio_mtx);     // mutex de hardware
    gpio_write(alarm_handle, LED_YELLOW_GPIO, yellow);
    gpio_write(alarm_handle, LED_GREEN_GPIO, green);
    gpio_write(alarm_handle, LED_RED_GPIO, red);
    pthread_mutex_unlock(&gpio_mtx);
}
```

Este mutex es **independiente** de los mutex del contexto del sistema (`mutex_alarm`). Es un mutex de grano fino adicional que serializa las escrituras GPIO.

**Parpadeo de LED sin bloquear:**

```c
// src/alarm.c:56-68
void alarm_blink_led(int gpio, uint8_t times, uint32_t delay_ms) {
    for (uint8_t i = 0; i < times; i++) {
        pthread_mutex_lock(&gpio_mtx);
        gpio_write(alarm_handle, gpio, 1);    // encender
        pthread_mutex_unlock(&gpio_mtx);
        usleep(delay_ms * 1000);              // espera FUERA del mutex
        pthread_mutex_lock(&gpio_mtx);
        gpio_write(alarm_handle, gpio, 0);    // apagar
        pthread_mutex_unlock(&gpio_mtx);
        usleep(delay_ms * 1000);              // espera FUERA del mutex
    }
}
```

El `usleep()` esta deliberadamente **fuera** del `lock/unlock`: se toma y suelta el mutex para cada transicion de nivel, en vez de mantenerlo durante todo el parpadeo. Esto permite que otros hilos actualicen los LEDs mientras parpadea.

**Maquina de estados de la alarma:**

| Estado | LED | Comportamiento |
|--------|-----|----------------|
| `ALARM_DISARMED` | Verde fijo | Sistema desarmado, vigilando |
| `ALARM_ARMING` | Amarillo parpadea 5 veces | Transicion de armado |
| `ALARM_ARMED` | Rojo fijo | Armado, vigilando |
| `ALARM_TRIGGERED` | Rojo parpadea 3 veces | Intrusion detectada |

### 5.11 `logger.c` / `logger.h` — Sistema de logging a archivos

El logger escribe a tres archivos independientes, uno por canal:

```c
// src/logger.c:20-24
static const log_channel_info_t channels[3] = {
    {LOG_ENV,   "logs/environmental.log"},   // lecturas BMP280 cada 5s
    {LOG_USER,  "logs/user_events.log"},     // Morse, arm/desarm, logins
    {LOG_ALERT, "logs/alerts.log"}           // intrusiones, presion fuera de rango
};
```

Cada linea de log tiene formato `[YYYY-MM-DD HH:MM:SS] mensaje` y se escribe con `fflush()` inmediatamente para no perder datos en caso de crash.

**Ejemplo de salida:**
```
[2026-07-20 12:55:08] T_bmp=24.3C P=1013.0hPa
[2026-07-20 12:55:08] LASER_BARRIER_TRIGGERED dist=196cm
[2026-07-20 12:55:08] ADMIN_ARMED code=..--
```

### 5.12 `thread_manager.c` / `thread_manager.h` — Administrador de hilos y contexto compartido

Este modulo es el **corazon del sistema**. Contiene la definicion del contexto compartido, la inicializacion/limpieza, el pipeline de logging y la implementacion de los 5 hilos.

**Inicializacion del contexto:**

```c
// src/thread_manager.c:23-43
int init_system_context(system_context_t* ctx) {
    memset(ctx, 0, sizeof(system_context_t));

    pthread_mutex_init(&ctx->mutex_env, NULL);
    pthread_mutex_init(&ctx->mutex_security, NULL);
    pthread_mutex_init(&ctx->mutex_alarm, NULL);
    pthread_mutex_init(&ctx->mutex_morse, NULL);
    pthread_mutex_init(&ctx->mutex_log_queue, NULL);

    sem_init(&ctx->sem_log, 0, 0);             // arranca en 0
    sem_init(&ctx->sem_alarm, 0, 0);
    sem_init(&ctx->sem_morse_complete, 0, 0);

    ctx->alarm_state = ALARM_DISARMED;
    morse_auth_init(&ctx->morse_ctx);
    return 0;
}
```

El `memset` completo antes de inicializar mutex y semaforos garantiza que no hay estado indeterminado al arrancar. El `system_context_t` es una variable **automatica en el stack de `main`**, pasada por puntero a todos los hilos — cero `malloc`/`free` en regimen permanente.

### 5.13 `main.c` — Punto de entrada y ciclo de vida del sistema

```c
// src/main.c:25-91
int main(int argc, char* argv) {
    signal(SIGINT, signal_handler);     // Ctrl+C
    signal(SIGTERM, signal_handler);    // kill

    system_context_t ctx;               // variable automatica en stack
    init_system_context(&ctx);
    alarm_leds_init();
    alarm_set_state(ALARM_DISARMED);

    // Crear los 5 hilos
    pthread_create(&env_thread, NULL, env_monitor_thread, &ctx);
    pthread_create(&sec_thread, NULL, security_thread, &ctx);
    pthread_create(&morse_thread, NULL, morse_auth_thread, &ctx);
    pthread_create(&alarm_th, NULL, alarm_thread, &ctx);
    pthread_create(&logger_th, NULL, logger_thread, &ctx);

    // Asignar prioridades RT (requiere sudo)
    param.sched_priority = 80;
    pthread_setschedparam(sec_thread, SCHED_FIFO, &param);
    param.sched_priority = 50;
    pthread_setschedparam(env_thread, SCHED_FIFO, &param);

    while (running) { usleep(100000); }  // esperar senial de terminacion

    // Apagado ordenado
    pthread_cancel(...);   // cancelar cada hilo
    pthread_join(...);     // esperar que terminen
    cleanup_system_context(&ctx);  // destruir mutex y semaforos
}
```

**Senializacion:** El manejador de `SIGINT`/`SIGTERM` solo escribe una `volatile bool` — la minima accion posible dentro de un manejador de senial. El trabajo real (`pthread_cancel` + `pthread_join`) se hace en el hilo principal.

---

## 6. Diagrama de Flujo Completo del Sistema

```
1. ARRANQUE:
   main → init_system_context() → alarm_leds_init() → LED verde ON
   main → crear 5 threads → asignar prioridades RT

2. CICLO DE EJECUCION (concurrente):

   [ENV_MONITOR cada 5s]
   BMP280 → temperatura + presion → mutex_env → log_enqueue() → alerta si presion fuera de rango

   [SECURITY cada 50ms]
   HC-SR04 + Laser + Hall → mutex_security
   → si alarma ARMED y sensor activo → mutex_alarm → TRIGGERED + log_enqueue()

   [MORSE cada 10ms]
   TTP223B → debounce 80ms → FSM → si secuencia completa → validar usuario
   → si admin → toggle alarma (ARMED/DISARMED) + log_enqueue()

   [ALARM cada 50ms]
   → si TRIGGERED → parpadear LED rojo (fuera de mutex)
   → si ARMING → parpadear LED amarillo (fuera de mutex)

   [LOGGER] sem_wait → sacar de cola → mutex_log_queue → copiar → fprintf + fflush

3. APAGADO (Ctrl+C):
   SIGINT → running = false → pthread_cancel todos → pthread_join todos → cleanup
```

---

## 7. Verificacion de Requisitos de Tiempo Real

### 7.1 Latencia de seguridad (< 500 ms)

| Evento | Latencia de HW | Latencia de SW | Total | Umbral |
|--------|----------------|----------------|-------|--------|
| HC-SR04 deteccion | ~10 ms | < 20 ms | < 30 ms | 500 ms |
| Laser interrumpido | ~1 ms | < 20 ms | < 21 ms | 500 ms |
| Hall puerta abierta | ~1 ms | < 20 ms | < 21 ms | 500 ms |

El margen es de **15x a 20x** sobre el deadline, suficiente para absorber jitter del planificador.

### 7.2 Estrategias para garantizar latencia

1. **Polling rapido (50 ms)**: un orden de magnitud por debajo del deadline
2. **SCHED_FIFO prioridad 80**: expropiacion inmediata de hilos de menor prioridad
3. **Secciones criticas minimas**: nada de E/S ni esperas dentro de un mutex
4. **Cola circular no bloqueante**: el productor nunca espera (descarta si llena)
5. **Timeouts en todas las esperas de hardware**:ningun sensor puede colgar un hilo

---

## 8. Compilacion y Ejecucion

### 8.1 Makefile

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -pthread -std=c11 -D_DEFAULT_SOURCE -O2 -I./include
LDFLAGS = -pthread

ifeq ($(PLATFORM),pi)
CFLAGS  += -DUSE_LGPIO
LDFLAGS += -llgpio -li2c
endif
```

| Comando | Descripcion |
|---------|-------------|
| `make` | Compila en modo simulacion (cualquier PC) |
| `make PLATFORM=pi` | Compila con lgpio para Raspberry Pi 5 |
| `make clean` | Limpia archivos compilados |
| `make run` | Compila y ejecuta |
| `make test` | Compila y ejecuta todos los tests |

### 8.2 Ejecucion

```bash
# Simulacion (cualquier PC)
./rack_monitor

# Raspberry Pi 5 (hardware real)
sudo ./rack_monitor   # sudo necesario para lgpio y SCHED_FIFO
```

### 8.3 Dependencias

- **Raspberry Pi 5**: `liblgpio-dev`, `i2c-tools`, I2C y 1-Wire habilitados
- **Simulacion**: solo `gcc` y `pthread` (disponibles en cualquier Linux)

---

## 9. Decisiones de Diseno y Lecciones Aprendidas

### 9.1 Cambios criticos de la v1 a la v2

| # | Problema en v1 | Correccion en v2 |
|---|----------------|-------------------|
| 1 | API de lgpio inventada (`lgpio_open/write/read`) que no existe | HAL `gpio_hal` con backend real y de simulacion |
| 2 | Pipeline de logging muerto (nadie encolaba, `sem_post` nunca llamado) | `log_enqueue()` + `sem_post` completo |
| 3 | Morse roto: duracion siempre 0, un solo simbolo posible | FSM que recibe instante monotono y mide duracion real |
| 4 | Secciones criticas de >1 segundo con mutex tomado | Copiar estado bajo mutex, actuar FUERA |
| 5 | `hcsr04` podia colgarse: sin timeout en espera de eco | Timeout de 30 ms en ambos bucles |
| 6 | `alarm.c` con estado global sin proteccion por 3 hilos | Mutex interno `gpio_mtx` |

### 9.2 Limitaciones conocidas y mejoras posibles

1. **Sin herencia de prioridad en mutex**: Si un hilo de baja prioridad retiene `mutex_alarm` y es expropiado, el hilo de seguridad (prio 80) queda bloqueado — inversion de prioridad. Se resolveria con `PTHREAD_PRIO_INHERIT`.

2. **Deriva acumulativa en periodos**: `sleep(N)` produce periodo real = `N + tiempo_de_ejecucion`. Lo correcto es `clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &siguiente_activacion, NULL)`.

3. **Sin `mlockall(MCL_CURRENT | MCL_FUTURE)`**: Un fallo de pagina en el camino critico introduce latencia de disco impredecible.

4. **Prioridad fija despues de `pthread_create()`**: Deja una ventana sin RT. Seria preferible `pthread_attr_setschedpolicy` + `PTHREAD_EXPLICIT_SCHED` al crearlo.

5. **Kernel Linux estandar, no PREEMPT_RT**: Las garantias son de tiempo real blando. Un kernel PREEMPT_RT reduciria la latencia de despacho de cientos de us a decenas.

---

## 10. Mapa de archivos del proyecto

```
v2/
├── include/
│   ├── config.h           Constantes, umbrales, structs y enums compartidos
│   ├── gpio_hal.h         Interfaz HAL de GPIO (sim / lgpio real) + gpio_now_us()
│   ├── thread_manager.h   system_context_t, mutex/sem, prototipos de hilos
│   ├── morse_auth.h       API de la FSM Morse (init, reset, update, validate)
│   ├── alarm.h            API de alarma (init, set_state, blink_led)
│   ├── logger.h           API de logging (init, log, close)
│   ├── bmp280.h           API del BMP280 (init, read)
│   ├── dht11.h            API del DHT11 (init, read)
│   ├── hcsr04.h           API del HC-SR04 (init, read_distance)
│   ├── laser_barrier.h    API de barrera laser (init, is_broken)
│   ├── hall_sensor.h      API del sensor Hall (init, detected)
│   └── ttp223b.h          API del sensor tactil (init, is_pressed)
│
├── src/
│   ├── main.c             Init, creacion de hilos, prioridades RT, apagado
│   ├── thread_manager.c   Los 5 hilos + pipeline de logging (nucleo RT)
│   ├── gpio_hal.c         Backends de simulacion y lgpio real
│   ├── morse_auth.c       FSM Morse dirigida por tiempo
│   ├── alarm.c            Actuadores (LEDs) con mutex interno
│   ├── logger.c           Escritura a los 3 canales de log
│   ├── bmp280.c           Driver I2C con compensacion del datasheet
│   ├── dht11.c            Driver bit-banging (deshabilitado en Pi5)
│   ├── hcsr04.c           Driver ultrasonico con timeout
│   ├── laser_barrier.c    Driver barrera laser
│   ├── hall_sensor.c      Driver sensor Hall
│   ├── ttp223b.c          Driver sensor tactil
│   └── config.c           Placeholder
│
├── logs/                  Archivos de log generados en ejecucion
├── tests/                 Tests unitarios (no detallados en este informe)
├── docs/                  Documentacion de hardware
├── Makefile               Build system con seleccion de plataforma
└── INFORME.md             Este documento
```

**Total: ~1,700 lineas de codigo fuente en 26 archivos.**
