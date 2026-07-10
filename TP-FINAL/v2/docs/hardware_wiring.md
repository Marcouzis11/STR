# Raspberry Pi 5 - Conexión Física de Hardware

## Mapa del Header GPIO (40 pines)

```
        3.3V  (1) (2)  5V
       GPIO2  (3) (4)  5V         ┌─────────────────────────┐
       GPIO3  (5) (6)  GND        │  Raspberry Pi 5        │
        GPIO4  (7) (8)  GPIO14    │  40-pin Header          │
         GND  (9)(10)  GPIO15     │  (vista desde arriba)  │
       GPIO17 (11)(12) GPIO18     └─────────────────────────┘
       GPIO27 (13)(14)  GND
       GPIO22 (15)(16) GPIO23
        3.3V (17)(18) GPIO24
       GPIO10 (19)(20)  GND
        GPIO9 (21)(22) GPIO25
        GPIO11 (23)(24) GPIO8
         GND (25)(26) GPIO7
        IDSD (27)(28) IDSC
       GPIO5  (29)(30)  GND
       GPIO6  (31)(32) GPIO12
      GPIO13  (33)(34)  GND
       GPIO19 (35)(36) GPIO16
       GPIO26 (37)(38) GPIO20
         GND (39)(40) GPIO21
```

## Tabla de Conexiones Completas

| Pin Físico | GPIO | Función | Dispositivo | Color cable sugerido |
|------------|------|---------|-------------|---------------------|
| (1) | 3.3V | VCC | DHT11, BMP280, TTP223B | Rojo |
| (6) | GND | Tierra | DHT11, BMP280, TTP223B, LEDs | Negro |
| (7) | GPIO4 | Data | DHT11 (1-Wire) | Verde |
| (3) | GPIO2 | SDA | BMP280 (I2C) | Blanco |
| (5) | GPIO3 | SCL | BMP280 (I2C) | Amarillo |
| (11) | GPIO17 | Trigger | HC-SR04 Trigger | Azul |
| (13) | GPIO27 | Echo | HC-SR04 Echo | Naranja |
| (15) | GPIO22 | Digital In | Barrera láser LM393 | Violeta |
| (16) | GPIO23 | Digital In | Sensor Hall | Gris |
| (18) | GPIO24 | Digital In | TTP223B Touch | Blanco |
| (29) | GPIO5 | LED Out | LED Amarillo (feedback) | Amarillo |
| (31) | GPIO6 | LED Out | LED Verde (alarma off) | Verde |
| (33) | GPIO13 | LED Out | LED Rojo (alarma on) | Rojo |

## Diagramas de Conexión por Dispositivo

### 1. DHT11 (Temperatura/Humedad)
```
    DHT11
   ┌─────┐
   │ VCC │──────┬──── 3.3V (pin 1)
   │ GND │──────┴──── GND (pin 6)
   │ DAT │──────┬──── 10K pull-up ──┬──── 3.3V
   │     │      └──── GPIO4 (pin 7)
   └─────┘
```

### 2. BMP280 (Presión/Temperatura I2C)
```
    BMP280
   ┌─────┐
   │ VCC │────── 3.3V (pin 1)
   │ GND │────── GND (pin 6)
   │ SDA │────── GPIO2 (pin 3)
   │ SCL │────── GPIO3 (pin 5)
   └─────┘
```

### 3. HC-SR04 (Ultrasonido)
```
    HC-SR04
   ┌─────┐
   │ VCC │────── 5V (pin 2)  ← Puede usar 3.3V con rango reducido
   │ GND │────── GND (pin 6)
   │ TRIG│────── GPIO17 (pin 11)
   │ ECHO│────── GPIO27 (pin 13)
   └─────┘
```

### 4. Barrera Láser KY-008 + LM393
```
    KY-008          LM393
   ┌─────┐        ┌─────┐
   │  +  │──VCC───│ VCC │── 3.3V (pin 1)
   │  -  │──GND───│ GND │── GND (pin 6)
   └─────┘        │ OUT │── GPIO22 (pin 15)
                  └─────┘
```

### 5. Sensor Hall (Puerta)
```
    Hall Sensor
   ┌─────┐
   │ VCC │────── 3.3V (pin 1)
   │ GND │────── GND (pin 6)
   │ OUT │────── GPIO23 (pin 16)
   └─────┘
```

### 6. TTP223B (Sensor Táctil)
```
    TTP223B
   ┌─────┐
   │ VCC │────── 3.3V (pin 1)
   │ GND │────── GND (pin 6)
   │ OUT │────── GPIO24 (pin 18)
   │  T  │────── (no conectar, es touch pad)
   └─────┘
```

### 7. LEDs (3 unidades)
```
    LED
   ┌─────┐
   │  +  │────── GPIO (5, 6, o 13)
   │  -  │────── 330Ω resistor ──┬── GND (pin 6)
   └─────┘                       │
   Todos los LEDs comparten GND ─┘

   GPIO5  → LED Amarillo (feedback botón)
   GPIO6  → LED Verde   (alarma inactiva)
   GPIO13 → LED Rojo    (alarma activa)
```

## Resumen de Colores de Cables Recomendado

| Componente | Cantidad cables | Colores sugeridos |
|------------|-----------------|-------------------|
| DHT11 | 3 | Rojo (+), Negro (-), Verde (data) |
| BMP280 | 4 | Rojo (+), Negro (-), Blanco (SDA), Amarillo (SCL) |
| HC-SR04 | 4 | Rojo (+), Negro (-), Azul (TRIG), Naranja (ECHO) |
| Láser/LM393 | 3 | Rojo (+), Negro (-), Violeta (OUT→GPIO22) |
| Hall | 3 | Rojo (+), Negro (-), Gris (OUT) |
| TTP223B | 3 | Rojo (+), Negro (-), Blanco (OUT) |
| LEDs | 6 | 3 cables positivos (amarillo, verde, rojo), 3 cables GND con resistencias |
| **TOTAL** | ~26 cables | |

## Configuración de Software

```bash
# /boot/config.txt - agregar estas líneas:
dtparam=i2c_arm=on
dtparam=1-wire=on

# Luego reiniciar:
sudo reboot
```

## Verificación de Conexiones

```bash
# Ver I2C (debe mostrar 0x76 para BMP280)
i2cdetect -y 1

# Verificar GPIO con lgpio
gpioinfo

# Test rápido sin sensores reales:
./tests/test_morse
```

## Notas Importantes

1. **DHT11**: Requiere 10KΩ pull-up entre data y 3.3V. Sin esto no funciona.
2. **HC-SR04**: Funciona mejor con 5V, pero puede usar 3.3V con alcance reducido.
3. **LM393**: Salida es open-collector, requiere pull-up. Ya tiene resistencia interna en muchos módulos.
4. **LEDs**: SIEMPRE usar resistencia de 330Ω para no quemar los GPIOs.
5. **Todos los GND** pueden conectarse al mismo pin GND (pin 6 o 9, etc).