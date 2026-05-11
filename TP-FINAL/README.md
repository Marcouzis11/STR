# Rack Monitor System - Instrucciones de Ejecución

## Requisitos Previos

### Hardware
- Raspberry Pi 5
- DHT11 (GPIO4)
- BMP280 (I2C)
- HC-SR04 (GPIO17/GPIO27)
- Barrera láser KY-008 + LM393 (GPIO22)
- Sensor Hall (GPIO23)
- TTP223B táctil (GPIO24)
- 3 LEDs (GPIO5/GPIO6/GPIO13)

### Software en Raspberry Pi 5

```bash
# 1. Instalar dependencias
sudo apt-get update
sudo apt-get install -y liblgpio-dev i2c-tools

# 2. Habilitar I2C y 1-Wire
sudo raspi-config
# Navegar: Interface Options → I2C → Yes
# Navegar: Interface Options → 1-Wire → Yes

# 3. Reiniciar
sudo reboot
```

## Compilación

```bash
# En el directorio TP-FINAL
cd /path/to/TP-FINAL

# Limpiar y compilar
make clean
make

# Ver estructura
ls -la
```

## Ejecución

```bash
# Ejecutar como root (requerido para lgpio)
sudo ./rack_monitor

# Para detener: Ctrl+C
```

## Tests

```bash
# Ejecutar tests unitarios
make test-morse
make test-integration
make test  # todos

# Tests individuales
./tests/test_morse
./tests/test_integration
```

## Logs

Los logs se guardan en `logs/`:
- `logs/environmental.log` - Datos de temperatura/humedad/presión
- `logs/user_events.log` - Eventos de usuarios (autenticación Morse)
- `logs/alerts.log` - Alertas de seguridad

```bash
# Ver logs en tiempo real
tail -f logs/environmental.log
tail -f logs/alerts.log
```

## Autenticación Morse

| Código | Usuario | Acción |
|--------|---------|--------|
| `..--` | admin | Activar/Desactivar alarma |
| `.-..` | operator | Solo lectura |
| `...-` | guest | Modo visible |

Ingresar código usando el sensor táctil TTP223B:
- Pulsación corta (≤300ms) = punto (·)
- Pulsación larga (>300ms) = raya (-)
- Timeout 1.5s entre símbolos = fin de secuencia

## Verificación de Latencia (<500ms)

```bash
# Con timestamps en logs, verificar tiempo entre evento y alerta
grep "LASER_BARRIER_TRIGGERED" logs/alerts.log
```

## Estructura del Proyecto

```
TP-FINAL/
├── plan.md              # Este documento
├── Makefile             # Build system
├── include/             # Headers (.h)
├── src/                 # Código fuente (.c)
├── logs/                # Archivos de log
├── tests/               # Tests unitarios
└── docs/                # Documentación
```

## Solución de Problemas

```bash
# Verificar I2C
i2cdetect -y 1

# Verificar GPIO
gpio readall

# Verificar lgpio
ldconfig -p | grep lgpio

# Permisos GPIO
sudo ./rack_monitor  # sin sudo no funcionará
```