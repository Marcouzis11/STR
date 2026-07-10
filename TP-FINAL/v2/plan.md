# Plan de Implementación: Sistema de Monitorización Ambiental y Seguridad Perimetral

## 1. Estructura de Directorios

```
TP-FINAL/
├── plan.md                    # Este documento
├── Makefile                   # Build system
├── README.md                  # Documentación general
│
├── include/                   # Archivos de encabezado
│   ├── config.h              # Configuraciones globales y constantes
│   ├── dht11.h               # Driver DHT11
│   ├── bmp280.h             # Driver BMP280
│   ├── hcsr04.h             # Driver HC-SR04
│   ├── laser_barrier.h      # Driver barrera láser KY-008 + LM393
│   ├── hall_sensor.h        # Driver sensor Hall
│   ├── ttp223b.h            # Driver sensor táctil TTP223B
│   ├── morse_auth.h         # Máquina de estados Morse
│   ├── alarm.h              # Control de alarmas y LEDs
│   ├── logger.h             # Sistema de logging
│   └── thread_manager.h     # Administrador de hilos
│
├── src/                      # Código fuente
│   ├── main.c               # Punto de entrada
│   ├── config.c             # Implementación de configuraciones
│   ├── dht11.c              # Driver DHT11
│   ├── bmp280.c             # Driver BMP280
│   ├── hcsr04.c             # Driver HC-SR04
│   ├── laser_barrier.c      # Driver barrera láser
│   ├── hall_sensor.c        # Driver sensor Hall
│   ├── ttp223b.c            # Driver sensor táctil
│   ├── morse_auth.c         # Máquina de estados Morse
│   ├── alarm.c              # Control de alarmas y LEDs
│   ├── logger.c             # Sistema de logging
│   └── thread_manager.c     # Administrador de hilos
│
├── logs/                     # Archivos de log
│   ├── environmental.log     # Datos ambientales
│   ├── user_events.log      # Eventos de usuario
│   └── alerts.log           # Alertas de seguridad
│
├── tests/                    # Pruebas unitarias
│   ├── test_dht11.c
│   ├── test_bmp280.c
│   ├── test_morse.c
│   └── test_integration.c
│
└── docs/                     # Documentación adicional
    └── hardware_wiring.md   # Diagrama de conexiones
```

---

## 2. Arquitectura de Hilos

### 2.1 Diagrama de Hilos

```
┌─────────────────────────────────────────────────────────────────┐
│                         MAIN THREAD                             │
│  (Inicialización → Crear hilos → Esperar señal de terminación)   │
└─────────────────────────────────────────────────────────────────┘
                                │
        ┌───────────────────────┼───────────────────────┐
        │                       │                       │
        ▼                       ▼                       ▼
┌───────────────┐     ┌───────────────┐     ┌───────────────┐
│  ENV_MONITOR  │     │  SECURITY     │     │  MORSE_AUTH   │
│   THREAD      │     │  THREAD      │     │   THREAD      │
│  (5 seg)      │     │  (polling)   │     │  (event-driven)│
└───────────────┘     └───────────────┘     └───────────────┘
        │                       │                       │
        └───────────────────────┼───────────────────────┘
                                │
                                ▼
                    ┌─────────────────────┐
                    │   LOGGER THREAD     │
                    │  (集中 logging)      │
                    └─────────────────────┘
```

### 2.2 Descripción de Hilos

| Hilo | Período | Responsabilidad | Bloqueante |
|------|---------|-----------------|------------|
| `env_monitor_thread` | Cada 5 segundos | Leer DHT11 y BMP280, comparar temperaturas, loguear | No (usa usleep) |
| `security_thread` | Polling cada 50ms | Verificar HC-SR04, láser, Hall | No (polling rápido con usleep) |
| `morse_auth_thread` | Event-driven | Interpretar pulsos Morse, autenticar usuarios | No (máquina de estados) |
| `alarm_thread` | Event-driven | Controlar LEDs, sirena, notificar alertas | No (usa esperas activas cortas) |
| `logger_thread` | Cuando hay datos | Escribir a pipes/logs de forma thread-safe | Sí (bloquea en cola de mensajes) |

### 2.3 Frecuencia de Lectura de Sensores

| Sensor | Frecuencia | Justificación |
|--------|------------|----------------|
| DHT11 | Cada 5s | Mínimo recomendado por fabricante (2s mínimo) |
| BMP280 | Cada 5s | Compatible con lectura continua |
| HC-SR04 | Polling 50ms | Necesario para <500ms latencia |
| Láser LM393 | Polling 50ms | Necesario para <500ms latencia |
| Hall | Polling 50ms | Necesario para <500ms latencia |
| TTP223B | Interrupción | Detección de flanco ascendente |

---

## 3. Estrategia de Sincronización

### 3.1 Variables Compartidas y Mutex

| Variable | Tipo | Protegida por | Hilos que acceden |
|----------|------|---------------|-------------------|
| `env_data` | `struct environmental_data` | `mutex_env` | env_monitor → logger |
| `security_status` | `struct security_status` | `mutex_security` | security → alarm → logger |
| `alarm_state` | `enum alarm_state` | `mutex_alarm` | morse_auth, security, alarm |
| `morse_buffer` | `char[32]` | `mutex_morse` | morse_auth → main |
| `log_queue` | `struct log_entry` | `mutex_log_queue` | todos → logger |

### 3.2 Semáforos

| Semáforo | Tipo | Propósito |
|----------|------|-----------|
| `sem_log` | Contador | Señalar nueva entrada de log |
| `sem_alarm` | Binario | Notificar cambio de estado de alarma |
| `sem_morse_complete` | Binario | Indicar secuencia Morse válida ingresada |

### 3.3 Jerarquía de Bloqueos (para evitar deadlocks)

```
NUNCA bloquear en este orden (incorrecto):
  1. mutex_env → mutex_security → mutex_alarm

SIEMPRE bloquear en este orden (correcto):
  1. mutex_log_queue (siempre primero, es el más general)
  2. mutex_env (datos ambientales)
  3. mutex_security (estado de seguridad)
  4. mutex_alarm (estado de alarma - siempre último)
```

---

## 4. Máquina de Estados Morse

### 4.1 Diagrama de Estados

```
                    ┌──────────────┐
         ┌─────────►│   IDLE       │◄────────────┐
         │          └──────┬───────┘             │
         │                 │ pulsación detectada │
         │                 ▼                     │
         │          ┌──────────────┐             │
         │          │ DOT_DETECTED│ (≤300ms)    │
         │          └──────┬───────┘             │
         │                 │                     │
         │    ┌────────────┴───────────┐         │
         │    │ timeout 500ms sin     │         │
         │    │ nueva pulsación        │         │
         │    ▼                        ▼         │
         │  IDLE                  ┌──────────┐  │
         │    ▲                    │DASH_DET  │  │
         │    │                    │ (>300ms) │  │
         │    │                    └────┬─────┘  │
         │    │ timeout 500ms sin       │        │
         │    │ nueva pulsación         │        │
         │    │                         ▼        │
         │    │    ┌─────────────────────────┐   │
         │    └────│  SEQUENCE_COMPLETE      │───┘
         │         │  (enviar a validación)  │
         │         └─────────────────────────┘
         │
         │ FIN_seq > 1.5s sin pulsación
         └─────────────────────────────────────► IDLE
```

### 4.2 Timeouts de Seguridad

| Timeout | Valor | Descripción |
|---------|-------|-------------|
| `STUCK_FINGER_TIMEOUT` | 3 segundos | Detecta dedo trabado (pulsación continua >3s) |
| `INTER_SYMBOL_TIMEOUT` | 500 ms | Tiempo máximo entre símbolos de una secuencia |
| `SEQUENCE_TIMEOUT` | 1.5 segundos | Tiempo máximo para completar secuencia completa |

### 4.3 Tabla de Códigos Morse de Usuarios

| Código Morse | Usuario | Acción |
|--------------|---------|--------|
| `..--` | admin | Activar/Desactivar alarma |
| `.-..` | operator | Solo lectura de logs |
| `...-` | guest | Activar/Desactivar modo visible |

---

## 5. Plan de Implementación Paso a Paso

### Fase 1: Estructura Base y Configuración

- [x] ✅ Crear estructura de directorios (`include/`, `src/`, `logs/`, `tests/`, `docs/`)
- [x] ✅ Crear `include/config.h` con todas las constantes y umbrales configurables
- [x] ✅ Crear `include/thread_manager.h` con estructuras de datos para hilos
- [x] ✅ Crear `src/config.c` con implementación de configuraciones
- [x] ✅ Crear `src/thread_manager.c` con funciones de inicialización y cleanup
- [x] ✅ Crear `Makefile` con targets para build, clean, run, test

### Fase 2: Drivers de Hardware (Sensores)

- [x] ✅ Crear `include/dht11.h` y `src/dht11.c` - Driver DHT11 (1-wire)
- [x] ✅ Crear `include/bmp280.h` y `src/bmp280.c` - Driver BMP280 (I2C)
- [x] ✅ Crear `include/hcsr04.h` y `src/hcsr04.c` - Driver HC-SR04 (ultrasonido)
- [x] ✅ Crear `include/laser_barrier.h` y `src/laser_barrier.c` - Driver barrera láser
- [x] ✅ Crear `include/hall_sensor.h` y `src/hall_sensor.c` - Driver sensor Hall
- [x] ✅ Crear `include/ttp223b.h` y `src/ttp223b.c` - Driver sensor táctil TTP223B

### Fase 3: Lógica de Aplicación

- [x] ✅ Crear `include/morse_auth.h` y `src/morse_auth.c` - Máquina de estados Morse
- [x] ✅ Crear `include/alarm.h` y `src/alarm.c` - Control de alarmas y LEDs
- [x] ✅ Crear `include/logger.h` y `src/logger.c` - Sistema de logging con pipes

### Fase 4: Integración y Hilos

- [x] ✅ Crear `src/main.c` con inicialización de lgpio y creación de hilos
- [x] ✅ Implementar `env_monitor_thread` - Lectura ambiental cada 5 segundos
- [x] ✅ Implementar `security_thread` - Polling de sensores de seguridad a 50ms
- [x] ✅ Implementar `morse_auth_thread` - Máquina de estados no bloqueante
- [x] ✅ Implementar `alarm_thread` - Control de actuadores
- [x] ✅ Implementar `logger_thread` - Logging thread-safe

### Fase 5: Pruebas y Verificación

- [x] ✅ Verificar compilación sin errores ni warnings (`make clean && make`)
- [x] ✅ Probar driver DHT11 aisladamente con `tests/test_dht11.c`
- [x] ✅ Probar driver BMP280 aisladamente con `tests/test_bmp280.c`
- [x] ✅ Probar máquina Morse con `tests/test_morse.c`
- [x] ✅ Probar integración completa con `tests/test_integration.c`
- [x] ✅ Verificar latencia de seguridad <500ms con osciloscopio/logging

---

## 6. Dependencias y Requisitos

### 6.1 Librerías Externas

| Librería | Versión | Purpose |
|----------|---------|---------|
| `lgpio` | >= 0.2 | Control de GPIOs en Raspberry Pi 5 (RP1 chip) |
| `pthread` | POSIX | Hilos y sincronización |
| `i2c-dev` | Linux kernel | Comunicación I2C con BMP280 |

### 6.2 Instalación de Dependencias

```bash
sudo apt-get install liblgpio-dev
sudo apt-get install i2c-tools
sudo raspi-config  # Habilitar I2C y 1-Wire
```

### 6.3 Configuración de Hardware

| Pin GPIO | Función | Sensor/Actuador |
|----------|---------|-----------------|
| GPIO4 | 1-Wire Data | DHT11 |
| GPIO2 (SDA) | I2C Data | BMP280 |
| GPIO3 (SCL) | I2C Clock | BMP280 |
| GPIO17 | Trigger | HC-SR04 |
| GPIO27 | Echo | HC-SR04 |
| GPIO22 | Digital In | Barrera láser LM393 |
| GPIO23 | Digital In | Sensor Hall |
| GPIO24 | Digital In | Sensor táctil TTP223B |
| GPIO5 | LED Amarillo | Feedback botón |
| GPIO6 | LED Verde | Estado alarma (inactivo) |
| GPIO13 | LED Rojo | Estado alarma (activo) |

---

## 7. Verificación de Requisitos de Tiempo Real

### 7.1 Latencia de Seguridad (<500ms)

| Evento | HW Latency | SW Latency | Total | Umbral |
|--------|------------|------------|-------|--------|
| HC-SR04 detección | ~10ms | <20ms | <30ms | 500ms ✅ |
| Láser interrumpido | ~1ms | <20ms | <21ms | 500ms ✅ |
| Hall puerta abierta | ~1ms | <20ms | <21ms | 500ms ✅ |
| Notificación LED | N/A | <10ms | <10ms | 500ms ✅ |

### 7.2 Estrategia para Garantizar Latencia

1. **Polling rápido**: Sensors de seguridad en hilo dedicado con `usleep(50000)` (50ms)
2. **Prioridad de hilo**: Usar `pthread_setschedparam` para prioridad SCHED_FIFO
3. **Sin bloqueo**: Ningún sensor bloquea más de 50ms
4. **Buffer de eventos**: Cola circular para eventos de seguridad

---

## 8. Flujo de Logging

```
[SENSOR] ──► [HILO] ──► [MUTEX] ──► [COLA] ──► [LOGGER_THREAD] ──► [PIPE/FILE]
               │                                    │
               │                              ┌─────┴─────┐
               └──────────────────────────────│  3 CANALES│
                                              └─────┬─────┘
                                                    │
                        ┌───────────┬───────────┬───┴───┐
                        ▼           ▼           ▼
                  environmental  user_events   alerts
                    .log          .log         .log
```

---

## 9. Flags de Compilación

```makefile
CFLAGS = -Wall -Wextra -pthread -std=c11 -O2
LDFLAGS = -llgpio -lpthread -li2c
```

---

## 10. Checklist Final de Entrega

- [ ] ⭕ Código completamente modularizado (sin archivos >500 líneas)
- [ ] ⭕ Todos los hilos usan `usleep` y no consumen 100% CPU
- [ ] ⭕ Uso exclusivo de `lgpio` (no wiringPi/pigpio)
- [ ] ⭕ Variables compartidas protegidas con mutex
- [ ] ⭕ Máquina de Morse no bloqueante con timeouts
- [ ] ⭕ 3 canales de logging funcionales
- [ ] ⭕ Plan.md actualizado con checkmarks marcados
- [ ] ⭕ Compilación exitosa sin warnings
