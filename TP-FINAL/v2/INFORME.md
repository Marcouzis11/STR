# INFORME TECNICO: Rack Monitor System v1.0

## 1. Descripcion General

El **Rack Monitor System** es un sistema de monitoreo ambiental y seguridad para racks de servidores, implementado en **C con POSIX threads** para **Raspberry Pi 5**. Ejecuta 5 hilos concurrentes que leen sensores, detectan intrusiones, procesan autenticacion Morse, controlan LEDs de alarma y escriben logs a disco.

---

## 2. Arquitectura de Hilos y Tiempo Real

### 2.1 Diagrama de Hilos

```
+---------------------------------------------------------+
|                    main.c (hilo principal)               |
|  - Maneja SIGINT/SIGTERM para shutdown limpio            |
|  - Crea los 5 hilos y espera con while(running)         |
|  - Asigna prioridades RT: sec=80, env=50                 |
+--------+--------+--------+--------+--------+------------+
         |        |        |        |        |
    +----v---+ +--v----+ +v------+ +v------+ +v-------+
    |  ENV   | | SECUR | | MORSE | | ALARM | | LOGGER |
    | MONITOR| |  ITY  | |  AUTH | |       | |        |
    | 5 seg  | | 50 ms | | 10 ms | | 50 ms | | sem    |
    +--------+ +-------+ +-------+ +-------+ +--------+
```

### 2.2 Prioridades de Tiempo Real

El sistema usa **SCHED_FIFO** (planificacion FIFO en tiempo real) para garantizar que los hilos criticos se ejecuten a tiempo. Esto requiere privilegios `sudo`.

```c
// main.c:62-68
struct sched_param param;
param.sched_priority = 80;
if (pthread_setschedparam(sec_thread, SCHED_FIFO, &param) != 0) {
    fprintf(stderr, "[WARN] No se pudo fijar SCHED_FIFO...\n");
}
param.sched_priority = 50;
pthread_setschedparam(env_thread, SCHED_FIFO, &param);
```

| Hilo | Prioridad | Periodo | Criticidad |
|------|-----------|---------|------------|
| `security_thread` | 80 (FIFO) | 50 ms | **Maxima** -- deteccion de intrusiones |
| `env_monitor_thread` | 50 (FIFO) | 5 s | Media -- monitoreo ambiental |
| `morse_auth_thread` | 0 (normal) | 10 ms | Normal -- autenticacion de usuario |
| `alarm_thread` | 0 (normal) | 50 ms | Normal -- parpadeo de LEDs |
| `logger_thread` | 0 (normal) | event-driven | Baja -- escritura a disco |

### 2.3 Por que SCHED_FIFO?

Un sistema de monitoreo de seguridad tiene **requisitos de latencia**. Si el `security_thread` no se ejecuta a tiempo (por ejemplo, porque el planificador le da CPU a otro hilo), una intrusion podria no detectarse. Con `SCHED_FIFO` prioridad 80, el kernel **interrumpe** cualquier hilo de menor prioridad para ejecutar el de seguridad.

---

## 3. Contexto Compartido (`system_context_t`)

Todos los hilos comparten un unico contexto definido en `thread_manager.h`:

```c
// thread_manager.h:25-50
typedef struct {
    pthread_mutex_t mutex_env;        // Protege datos ambientales
    pthread_mutex_t mutex_security;   // Protege estado de seguridad
    pthread_mutex_t mutex_alarm;      // Protege estado de alarma
    pthread_mutex_t mutex_morse;      // Protege maquina Morse
    pthread_mutex_t mutex_log_queue;  // Protege cola de logs

    sem_t sem_log;                    // Senializa al logger_thread
    sem_t sem_alarm;
    sem_t sem_morse_complete;

    environmental_data_t env_data;
    security_status_t security_status;
    alarm_state_t alarm_state;
    morse_context_t morse_ctx;
    log_entry_t log_queue[64];
    uint8_t log_queue_head;
    uint8_t log_queue_tail;
    bool log_queue_full;
} system_context_t;
```

**Regla de oro:** Cada hilo solo toma el mutex que necesita, lo suelta lo antes posible, y NUNCA hace E/S (disco, printf) dentro de un mutex.

---

## 4. Pipeline de Logging (Productor/Consumidor)

El sistema de logging es un **patron productor/consumidor** con cola circular y semaforo.

### 4.1 Flujo

```
ENV_MONITOR --+
SECURITY ----+  log_enqueue()    cola circular[64]    sem_post()    logger_thread -> archivo
MORSE_AUTH --+  (productores)    ------------------> (despierta)   (consumidor)
ALARM -------+
```

### 4.2 Codigo del productor

```c
// thread_manager.c:61-83
int log_enqueue(system_context_t* ctx, log_channel_t channel, const char* fmt, ...) {
    log_entry_t entry;
    entry.channel = channel;
    va_list args;
    va_start(args, fmt);
    vsnprintf(entry.message, sizeof(entry.message), fmt, args);
    va_end(args);

    bool queued = false;
    pthread_mutex_lock(&ctx->mutex_log_queue);
    uint8_t next = (ctx->log_queue_head + 1) % LOG_QUEUE_SIZE;
    if (next != ctx->log_queue_tail) {  // hay hueco
        ctx->log_queue[ctx->log_queue_head] = entry;
        ctx->log_queue_head = next;
        queued = true;
    }
    pthread_mutex_unlock(&ctx->mutex_log_queue);

    if (queued) sem_post(&ctx->sem_log);  // despierta al logger
    return queued ? 0 : -1;
}
```

### 4.3 Codigo del consumidor

```c
// thread_manager.c:85-107
void* logger_thread(void* arg) {
    system_context_t* ctx = (system_context_t*)arg;
    logger_init();

    while (1) {
        sem_wait(&ctx->sem_log);  // bloquea SIN consumir CPU

        log_entry_t entry;
        bool have = false;
        pthread_mutex_lock(&ctx->mutex_log_queue);
        if (ctx->log_queue_head != ctx->log_queue_tail) {
            entry = ctx->log_queue[ctx->log_queue_tail];
            ctx->log_queue_tail = (ctx->log_queue_tail + 1) % LOG_QUEUE_SIZE;
            have = true;
        }
        pthread_mutex_unlock(&ctx->mutex_log_queue);

        // E/S a disco FUERA del mutex
        if (have) logger_log(entry.channel, "%s", entry.message);
    }
}
```

### 4.4 Por que este diseno es correcto?

- **`sem_wait` no gasta CPU:** el logger_thread duerme hasta que hay datos
- **Seccion critica minima:** el mutex se toma solo para sacar un elemento de la cola (~nanosegundos)
- **E/S fuera del mutex:** `fopen/fprintf/fflush` son lentos (~milisegundos), nunca bloquean a los productores
- **Cola circular:** si se llena, el mensaje mas nuevo se descarta (no bloquea productores)

---

## 5. Sensores Digitales (Polling)

### 5.1 Security Thread -- El corazon del sistema de seguridad

```c
// thread_manager.c:155-201
void* security_thread(void* arg) {
    int hcsr_handle = hcsr04_init(HCSR04_TRIG_GPIO, HCSR04_ECHO_GPIO);
    int laser_handle = laser_barrier_init(LASER_GPIO);
    int hall_handle = hall_sensor_init(HALL_GPIO);

    while (1) {
        uint8_t distance = hcsr04_read_distance(hcsr_handle, ...);
        bool laser_broken = laser_barrier_is_broken(laser_handle, LASER_GPIO);
        bool hall_triggered = hall_sensor_detected(hall_handle, HALL_GPIO);
        bool ultrasonic = (distance < 10);

        // Actualizar estado compartido
        pthread_mutex_lock(&ctx->mutex_security);
        ctx->security_status.ultrasonic_distance_cm = distance;
        ctx->security_status.laser_triggered = laser_broken;
        ctx->security_status.hall_triggered = hall_triggered;
        pthread_mutex_unlock(&ctx->mutex_security);

        // Actuar SOLO si alarma armada
        if (laser_broken || hall_triggered || ultrasonic) {
            pthread_mutex_lock(&ctx->mutex_alarm);
            if (ctx->alarm_state == ALARM_ARMED ||
                ctx->alarm_state == ALARM_TRIGGERED) {
                ctx->alarm_state = ALARM_TRIGGERED;
                fire = true;
            }
            pthread_mutex_unlock(&ctx->mutex_alarm);
            // Log FUERA del mutex
            if (fire) {
                if (laser_broken) log_enqueue(..., "LASER_BARRIER_TRIGGERED");
                if (hall_triggered) log_enqueue(..., "HALL_DOOR_OPEN");
                if (ultrasonic) log_enqueue(..., "ULTRASONIC_INTRUSION");
            }
        }
        usleep(SECURITY_POLL_US);  // 50 ms
    }
}
```

### 5.2 Reglas de Negocio -- Sensores de Seguridad

| Sensor | GPIO | Umbral | Logica | Mensaje de Alerta |
|--------|------|--------|--------|-------------------|
| **HC-SR04** (ultrasonido) | TRIG=17, ECHO=27 | < 10 cm | Si distancia < 10cm -> intruso | `ULTRASONIC_INTRUSION dist=Xcm` |
| **KY-008** (laser/LM393) | 22 | Nivel alto = roto | `level == 1` = haz interrumpido | `LASER_BARRIER_TRIGGERED dist=Xcm` |
| **KY-024** (efecto Hall) | 23 | Nivel bajo = puerta abierta | `level == 0` = iman ausente | `HALL_DOOR_OPEN` |

**Los 3 sensores solo generan alerta si la alarma esta ARMED o TRIGGERED.**

### 5.3 HC-SR04 -- Calculo de distancia

```c
// hcsr04.c:24-43
uint8_t hcsr04_read_distance(int handle, int trig_gpio, int echo_gpio) {
    // Pulso de disparo de 10us
    gpio_write(handle, trig_gpio, 1);
    usleep(10);
    gpio_write(handle, trig_gpio, 0);

    // Esperar eco con timeout de 30ms
    int64_t wait_start = gpio_now_us();
    while (gpio_read(handle, echo_gpio) == 0) {
        if (gpio_now_us() - wait_start > HCSR04_ECHO_TIMEOUT_US) return 255;
    }

    int64_t echo_start = gpio_now_us();
    while (gpio_read(handle, echo_gpio) == 1) {
        if (gpio_now_us() - echo_start > HCSR04_ECHO_TIMEOUT_US) return 255;
    }

    // distancia = duracion * velocidad_sonido / 2
    int64_t duration_us = echo_end - echo_start;
    int64_t distance = (duration_us * 343) / 20000;

    if (distance > 200) distance = 200;
    return (uint8_t)distance;
}
```

**Formula:** `distancia_cm = (duracion_us x 343) / 20000`
- Velocidad del sonido: 343 m/s = 0.0343 cm/us
- Dividido por 2 porque es ida y vuelta
- Timeout 30ms -> max ~5 metros detectables, se limita a 200 cm

### 5.4 Laser Barrier (KY-008 + LM393)

```c
// laser_barrier.c:11-14
bool laser_barrier_is_broken(int handle, int gpio) {
    /* LM393: nivel alto cuando el haz laser esta interrumpido (sin luz). */
    int level = gpio_read(handle, gpio);
    return (level == 1);
}
```

El modulo KY-008 contiene un laser (KY-008) y un comparador LM393 con fotodiodo integrado. Cuando el haz laser esta intacto, el LM393 retorna nivel bajo (0). Cuando el haz se interrumpe, retorna nivel alto (1).

### 5.5 Hall Sensor (KY-024 / HW-484)

```c
// hall_sensor.c:11-14
bool hall_sensor_detected(int handle, int gpio) {
    /* Nivel bajo = iman ausente = puerta abierta. */
    int level = gpio_read(handle, gpio);
    return (level == 0);
}
```

El KY-024 contiene un sensor de efecto Hall y un comparador LM393 con potenciometro de ajuste. Cuando se detecta un campo magnetico (iman cercano), el pin D0 va a nivel bajo (0). Sin iman = nivel alto (1). La logica reporta "puerta abierta" cuando el iman ausente (nivel 0).

---

## 6. BMP280 -- Sensor I2C (Temperatura y Presion)

### 6.1 Inicializacion I2C

```c
// bmp280.c:41-75
int bmp280_init(int bus, uint8_t addr) {
    // Abrir /dev/i2c-1
    i2c_fd = open("/dev/i2c-1", O_RDWR);
    ioctl(i2c_fd, I2C_SLAVE, addr);  // addr = 0x76

    // Configurar oversampling x1, modo normal
    uint8_t config_data[2] = {0xF4, 0x27};
    write(i2c_fd, config_data, 2);

    // Leer 24 bytes de calibracion desde registro 0x88
    uint8_t calib_addr = 0x88, calib_data[24];
    write(i2c_fd, &calib_addr, 1);
    read(i2c_fd, calib_data, 24);

    // Parsear coeficientes de calibracion...
    return i2c_fd;
}
```

### 6.2 Compensacion de temperatura

El BMP280 tiene coeficientes de calibracion unicos por chip. La temperatura compensada se calcula con la formula del datasheet:

```c
// bmp280.c:80-90
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

### 6.3 Compensacion de presion

```c
// bmp280.c:93-115
static uint32_t bmp280_compensate_p(int32_t adc_P) {
    int64_t var1 = ((int64_t)t_fine) - 128000;
    int64_t var2 = var1 * var1 * (int64_t)calibration.dig_P6;
    var2 += (var1 * (int64_t)calibration.dig_P5) << 17;
    var2 += ((int64_t)calibration.dig_P4) << 35;
    var1 = ((var1 * var1 * (int64_t)calibration.dig_P3) >> 8) +
           ((var1 * (int64_t)calibration.dig_P2) << 12);
    var1 = (((((int64_t)1) << 47) + var1)) * ((int64_t)calibration.dig_P1) >> 33;
    if (var1 == 0) return 0;

    int64_t p = 1048576 - adc_P;
    p = (((p << 31) - var2) * 3125) / var1;
    var1 = (((int64_t)calibration.dig_P9) * (p >> 13) * (p >> 13)) >> 25;
    var2 = (((int64_t)calibration.dig_P8) * p) >> 19;
    p = ((p + var1 + var2) >> 8) + (((int64_t)calibration.dig_P7) << 4);
    return (uint32_t)p;
}
```

La presion se compensa con los mismos coeficientes y la variable `t_fine` calculada previamente. El resultado es en formato Q24.8 (dividir por 256 para Pa, luego por 100 para hPa).

### 6.4 Reglas de Negocio -- Ambiental

| Parametro | Sensor | Periodo | Rango normal | Alerta |
|-----------|--------|---------|--------------|--------|
| Temperatura | BMP280 | 5 s | Cualquier valor | Ninguna |
| Presion | BMP280 | 5 s | 800-1100 hPa | `PRESSURE_OUT_OF_RANGE` |

---

## 7. Maquina de Estados Morse (Autenticacion)

### 7.1 Estados

```c
// config.h:74-79
typedef enum {
    MORSE_STATE_IDLE,              // sin pulsar, sin simbolos
    MORSE_STATE_PRESSED,           // boton presionado, midiendo duracion
    MORSE_STATE_GAP,               // liberado, esperando siguiente simbolo
    MORSE_STATE_SEQUENCE_COMPLETE,  // secuencia lista para validar
    MORSE_STATE_STUCK_FINGER       // pulsacion > 3s, descartar
} morse_state_t;
```

### 7.2 Diagrama de estados

```
              +---------------------------------------------+
              |           IDLE / GAP                        |
              |  (esperando flanco ascendente)              |
              +----------+----------------------------------+
                         | rising (boton presionado)
                         v
              +---------------------------------------------+
              |           PRESSED                           |
              |  (midiendo duracion de toque)               |
              +------+----------------+---------------------+
                     |                |
            falling  |                | held > 3000ms
                     v                v
              +------------+   +-------------------+
              |    GAP     |   |   STUCK_FINGER    |
              | (esperando |   | (descartar seq)   |
              | simbolo o  |   +--------+----------+
              |  timeout)  |            | falling
              +------+-----+            v
                     |            reset -> IDLE
                     | timeout > 2000ms
                     v
              +---------------------------------------------+
              |      SEQUENCE_COMPLETE                      |
              |  (leer seq, validar usuario)                |
              +---------------------------------------------+
```

### 7.3 Reglas de Negocio -- Morse

| Constante | Valor | Significado |
|-----------|-------|-------------|
| `MORSE_DOT_MAX_MS` | 250 ms | Tocar < 250ms = punto (`.`) |
| `MORSE_DASH_MIN_MS` | 250 ms | Tocar >= 250ms = raya (`-`) |
| `MORSE_MIN_TOUCH_MS` | 100 ms | Tocar < 100ms = ruido, ignorado |
| `INTER_SYMBOL_TIMEOUT_MS` | 2000 ms | Silencio > 2s = fin de secuencia |
| `STUCK_FINGER_TIMEOUT_MS` | 3000 ms | Pulsar > 3s = dedo trabado, descartar |
| `MORSE_DEBOUNCE_MS` | 80 ms | Filtro anti-rebotes del sensor |

### 7.4 Codigos Morse registrados

| Secuencia | Usuario | Accion |
|-----------|---------|--------|
| `..--` | admin | Armar/Desarmar alarma (toggle) |
| `.-..` | operator | Solo login (no cambia alarma) |
| `...-` | guest | Solo login (no cambia alarma) |

### 7.5 Debounce -- Filtrado de rebotes

El sensor TTP223B capacitivo genera rebotes electricos. El debounce verifica que el estado se mantenga estable durante 80ms antes de aceptarlo:

```c
// thread_manager.c:212-224
if (raw != debounced) {
    if (debounce_start_ms == 0) {
        debounce_start_ms = now_ms;       // empezar timer
    } else if (now_ms - debounce_start_ms >= MORSE_DEBOUNCE_MS) {
        debounced = raw;                    // confirmar despues de 80ms
        debounce_start_ms = 0;
    }
} else {
    debounce_start_ms = 0;                 // reset si no cambio
}
```

### 7.6 Feedback visual

Cada simbolo registrado genera un blink rapido del LED amarillo (GPIO5) para confirmar que el sistema detecto la pulsacion:

```c
// thread_manager.c:237-239
if (symbol_added) {
    alarm_blink_led(LED_YELLOW_GPIO, 1, 50);  // blink de 50ms
}
```

### 7.7 Proteccion anti-dedo trabado

Si el usuario mantiene el boton presionado mas de 3 segundos, la secuencia se descarta automaticamente para evitar falsos positivos:

```c
// morse_auth.c:76-78
case MORSE_STATE_PRESSED: {
    uint32_t held = now_ms - ctx->press_start_ms;
    if (held > STUCK_FINGER_TIMEOUT_MS) {
        ctx->state = MORSE_STATE_STUCK_FINGER;
    }
```

---

## 8. Maquina de Estados de Alarma

### 8.1 Estados

```
ALARM_DISARMED --(admin ..--)--> ALARM_ARMING --(5 blinks)--> ALARM_ARMED
     ^                                                                    |
     |                  (admin ..--)                                       |
     +--------------------------------------------- ALARM_TRIGGERED <-----+
                                                      (sensor intrusion)
```

| Estado | LED | Comportamiento |
|--------|-----|----------------|
| `ALARM_DISARMED` | Verde fijo | Sistema desarmado |
| `ALARM_ARMING` | Amarillo parpadea 5 veces | Armando (confirmacion visual) |
| `ALARM_ARMED` | Rojo fijo | Armado, vigilando |
| `ALARM_TRIGGERED` | Rojo parpadea 3 veces | Intrusion detectada |

### 8.2 Proteccion con mutex interno

```c
// alarm.c:18-27
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

El modulo alarma tiene **su propio mutex** (`gpio_mtx`) separado de los mutex del contexto (`mutex_alarm`). Esto evita que el hilo de alarma (parpadeos) y el hilo Morse (armar/desarmar) se pisen al acceder al hardware GPIO.

### 8.3 Blink sin bloquear el mutex de estado

```c
// alarm.c:56-68
void alarm_blink_led(int gpio, uint8_t times, uint32_t delay_ms) {
    if (alarm_handle < 0) return;
    for (uint8_t i = 0; i < times; i++) {
        pthread_mutex_lock(&gpio_mtx);
        gpio_write(alarm_handle, gpio, 1);
        pthread_mutex_unlock(&gpio_mtx);
        usleep(delay_ms * 1000);      // espera FUERA del mutex
        pthread_mutex_lock(&gpio_mtx);
        gpio_write(alarm_handle, gpio, 0);
        pthread_mutex_unlock(&gpio_mtx);
        usleep(delay_ms * 1000);      // espera FUERA del mutex
    }
}
```

Cada parpadeo toma el mutex GPIO solo para escribir el pin (~nanosegundos), y suelta el mutex antes de esperar. Esto permite que otros hilos actualicen los LEDs mientras parpadea.

---

## 9. GPIO HAL -- Capa de Abstraccion de Hardware

### 9.1 Arquitectura dual-backend

El GPIO HAL (`gpio_hal.h` / `gpio_hal.c`) proporciona una API unica con dos implementaciones seleccionables en tiempo de compilacion:

| Backend | Compilacion | Biblioteca | Plataforma |
|---------|-------------|------------|------------|
| Real | `make PLATFORM=pi` (define `USE_LGPIO`) | lgpio + i2c | Raspberry Pi 5 |
| Simulacion | `make` (sin flags) | Ninguna | Cualquier PC Linux/Mac/Win |

### 9.2 Reloj monotono

```c
// gpio_hal.h:33-38
static inline int64_t gpio_now_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000LL + ts.tv_nsec / 1000LL;
}
```

Usado por el HC-SR04 (medicion de tiempo de eco) y por la maquina Morse (medicion de duracion de pulsaciones). `CLOCK_MONOTONIC` no se ve afectado por cambios en la hora del sistema.

### 9.3 Backend real (lgpio)

```c
// gpio_hal.c:10-14
int gpio_open(void) {
    int h = lgGpiochipOpen(4);  // chip 4 = RP1 en Pi5
    if (h < 0) h = lgGpiochipOpen(0);  // fallback a modelos anteriores
    return h;
}
```

La Raspberry Pi 5 usa el chip GPIO **RP1** accessible como `gpiochip4`. Los modelos anteriores usan `gpiochip0`. El backend intenta chip4 primero y cae en chip0 si falla.

### 9.4 Backend de simulacion

Mantiene un array de 64 pines en memoria. Permite compilar y ejecutar toda la logica de hilos, mutex, semaforos, Morse y logging sin hardware:

```c
// gpio_hal.c:62-70
static void sim_init_once(void) {
    for (int i = 0; i < SIM_MAX_GPIO; i++) {
        sim_level[i] = 1;           // reposo: linea en alto
        sim_is_output[i] = false;
    }
}
```

Los sensores digitales en reposo estan en nivel alto (1), de modo que no se disparan falsas alarmas durante la simulacion.

---

## 10. Logger -- Escritura a Archivos

### 10.1 Canales de log

```c
// logger.c:20-24
static const log_channel_info_t channels[3] = {
    {LOG_ENV,   "logs/environmental.log"},  // lecturas BMP280 cada 5s
    {LOG_USER,  "logs/user_events.log"},    // Morse, arm/desarm, logins
    {LOG_ALERT, "logs/alerts.log"}          // intrusiones, presion fuera de rango
};
```

### 10.2 Formato de log

```
[2026-07-19 12:55:08] LASER_BARRIER_TRIGGERED dist=196cm
[2026-07-19 12:55:08] ULTRASONIC_INTRUSION dist=6cm
[2026-07-19 12:55:08] T_bmp=22.1C P=952.9hPa
[2026-07-19 12:55:08] ADMIN_ARMED code=..--
```

Cada linea tiene: `[timestamp] mensaje`. Los archivos se abren en modo append (`"a"`) y se flush despues de cada escritura para no perder datos en caso de crash.

### 10.3 Limpieza de logs

```bash
# Para vaciar los logs sin borrar los archivos:
sudo truncate -s 0 /home/agusraspberry/STR/STR-main/TP-FINAL/v2/logs/*.log
```

---

## 11. Mapa de Pines GPIO

```
GPIO   | Modo     | Sensor/Actuador
-------+----------+---------------------------
 5     | Output   | LED Amarillo (arming)
 6     | Output   | LED Verde (desarmado)
13     | Output   | LED Rojo (armado/disparado)
17     | Output   | HC-SR04 TRIG
22     | Input    | Laser barrier LM393 (KY-008)
23     | Input    | Hall sensor KY-024 (D0)
24     | Input    | TTP223B touch button
27     | Input    | HC-SR04 ECHO
I2C-1  | I2C      | BMP280 (addr 0x76, SDO->GND)
```

---

## 12. Flujo Completo del Sistema

```
1. main.c: init_system_context() -> alarm_leds_init() -> LED verde ON
2. main.c: crear 5 threads -> asignar prioridades RT
3. ENV_MONITOR cada 5s: BMP280 -> temp+presion -> log -> alerta si presion fuera de rango
4. SECURITY cada 50ms: HC-SR04 + Laser + Hall -> si alarma ARMED y sensor activo -> TRIGGERED + log
5. MORSE cada 10ms: TTP223B con debounce -> detectar . y - -> si secuencia completa -> validar usuario
6. ALARM: si TRIGGERED -> parpadear LED rojo; si ARMING -> parpadear LED amarillo
7. LOGGER: sem_wait -> sacar de cola -> escribir a archivo
8. main.c: while(running) usleep(100ms) -> Ctrl+C -> SIGINT -> pthread_cancel todos -> cleanup
```

---

## 13. Semaforos y Mutex -- Resumen

| Recurso | Tipo | Protege |
|---------|------|---------|
| `mutex_env` | mutex | `env_data` (temperatura, presion) |
| `mutex_security` | mutex | `security_status` (distancia, laser, hall) |
| `mutex_alarm` | mutex | `alarm_state` (armed, triggered, etc.) |
| `mutex_morse` | mutex | `morse_ctx` (buffer, estado de Morse) |
| `mutex_log_queue` | mutex | cola circular de logs |
| `gpio_mtx` | mutex (interno de alarm.c) | acceso hardware LEDs |
| `sem_log` | semaforo | despierta logger_thread cuando hay datos |
| `sem_morse_complete` | semaforo | senializa completion de secuencia Morse |

---

## 14. Shutdown Limpio

```c
// main.c:74-89
printf("\nShutting down...\n");

pthread_cancel(env_thread);    // cancelar cada hilo
pthread_cancel(sec_thread);
pthread_cancel(morse_thread);
pthread_cancel(alarm_th);
pthread_cancel(logger_th);

pthread_join(env_thread, NULL); // esperar que terminen
pthread_join(sec_thread, NULL);
pthread_join(morse_thread, NULL);
pthread_join(alarm_th, NULL);
pthread_join(logger_th, NULL);

cleanup_system_context(&ctx);   // destruir mutexes y semaforos
printf("System stopped.\n");
```

Se usa `pthread_cancel` + `pthread_join` para un apagado ordenado. Los threads se cancelan con `SIGINT` (Ctrl+C) o `SIGTERM` (kill). `pthread_join` garantiza que cada hilo termine antes de liberar los recursos compartidos.

---

## 15. DHT11 -- Limitaciones en Raspberry Pi 5

El DHT11 usa el protocolo 1-Wire propietario mediante **bit-banging** (alternar los pines GPIO desde el espacio de usuario con microsegundos de precision). En la Raspberry Pi 5, el planificador del kernel interrumpe estas esperas criticas, causando lecturas incorrectas (temp=0.0, hum=0).

```c
// dht11.c:50-75 (backend real, deshabilitado)
dht11_reading_t dht11_read(int handle) {
    // Senal de arranque: bajar la linea >18ms
    gpio_claim_output(handle, gpio, 0);
    usleep(20000);
    gpio_write(handle, gpio, 1);
    usleep(30);

    // Leer 40 bits midiendo duracion de pulsos...
    // El timing no es determinista en userspace Linux
}
```

**Solucion implementada:** El DHT11 queda documentado pero deshabilitado en `thread_manager.c`. El BMP280 provee temperatura y presion de forma confiable via I2C.

---

## 16. Makefile -- Sistema de Compilacion

```makefile
CC = gcc
CFLAGS = -Wall -Wextra -pthread -std=c11 -D_DEFAULT_SOURCE -O2 -I./include
LDFLAGS = -pthread

ifeq ($(PLATFORM),pi)
CFLAGS  += -DUSE_LGPIO
LDFLAGS += -llgpio -li2c
endif
```

| Comando | Que hace |
|---------|----------|
| `make` | Compila en modo simulacion (sin hardware) |
| `make PLATFORM=pi` | Compila con lgpio para Raspberry Pi 5 |
| `make clean` | Limpia archivos compilados |
| `make run` | Compila y ejecuta |

---

## 17. Estructura de Archivos del Proyecto

```
v2/
+-- Makefile
+-- INFORME.md
+-- include/
|   +-- config.h           # Defines, enums, structs compartidos
|   +-- thread_manager.h   # system_context_t y prototipos de threads
|   +-- gpio_hal.h         # HAL GPIO con gpio_now_us() inline
|   +-- alarm.h            # API de alarma (LEDs)
|   +-- logger.h           # API de logging
|   +-- morse_auth.h       # API de autenticacion Morse
|   +-- bmp280.h           # API del BMP280
|   +-- dht11.h            # API del DHT11
|   +-- hcsr04.h           # API del HC-SR04
|   +-- laser_barrier.h    # API de la barrera laser
|   +-- hall_sensor.h      # API del sensor Hall
|   +-- ttp223b.h          # API del boton touch
+-- src/
|   +-- main.c             # Punto de entrada, signal handling
|   +-- thread_manager.c   # 5 threads + pipeline de logging
|   +-- alarm.c            # Control de LEDs con mutex interno
|   +-- logger.c           # Escritura a archivos (3 canales)
|   +-- morse_auth.c       # Maquina de estados Morse
|   +-- gpio_hal.c         # HAL dual: lgpio (real) + simulacion
|   +-- bmp280.c           # Driver I2C del BMP280
|   +-- dht11.c            # Driver bit-banging del DHT11
|   +-- hcsr04.c           # Driver del HC-SR04
|   +-- laser_barrier.c    # Driver de la barrera laser
|   +-- hall_sensor.c      # Driver del sensor Hall
|   +-- ttp223b.c          # Driver del boton touch
|   +-- config.c           # Placeholder
+-- tests/
|   +-- test_sensors_full.c    # Test completo de sensores
|   +-- test_touch_led.c       # Test touch boton + LED
|   +-- test_hall_monitor.c    # Monitor Hall en tiempo real
|   +-- test_leds.c            # Test de LEDs
|   +-- test_morse.c           # Test unitario Morse
|   +-- test_bmp280.c          # Test del BMP280
|   +-- test_dht11.c           # Test del DHT11
|   +-- test_integration.c     # Test de integracion
+-- logs/
|   +-- environmental.log  # Lecturas ambientales
|   +-- user_events.log    # Eventos de usuario (Morse)
|   +-- alerts.log         # Alertas de seguridad
+-- build/                 # Objetos compilados
```

**Total: 1,677 lineas de codigo fuente en 26 archivos.**
