#!/usr/bin/env python3
"""
plotter_3ejes.py
Visualizador en tiempo real de señales de vibración (3 ejes simulados).
Lee datos CSV desde stdin: x,y,z\n  (producidos por medidor_vibracion)

Uso:
    sudo ./medidor_vibracion | python3 plotter_3ejes.py

Dependencias:
    pip install matplotlib numpy
"""

import sys
import threading
import collections
import matplotlib.pyplot as plt
import matplotlib.animation as animation
import numpy as np

# ── Configuración ──────────────────────────────────────────────────────
WINDOW_SIZE  = 200      # Muestras visibles en el gráfico
UPDATE_MS    = 50       # Intervalo de redibujado (ms) → ~20 fps
MAX_QUEUE    = 500      # Máximo de datos en buffer interno

# ── Buffer circular thread-safe ────────────────────────────────────────
lock   = threading.Lock()
buf_x  = collections.deque([0.0] * WINDOW_SIZE, maxlen=WINDOW_SIZE)
buf_y  = collections.deque([0.0] * WINDOW_SIZE, maxlen=WINDOW_SIZE)
buf_z  = collections.deque([0.0] * WINDOW_SIZE, maxlen=WINDOW_SIZE)
running = True

# ── Hilo lector de stdin ───────────────────────────────────────────────
def reader_thread():
    """Lee líneas CSV de stdin y las acumula en los buffers."""
    global running
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            parts = line.split(',')
            if len(parts) != 3:
                continue
            x, y, z = float(parts[0]), float(parts[1]), float(parts[2])
            with lock:
                buf_x.append(x)
                buf_y.append(y)
                buf_z.append(z)
        except ValueError:
            pass  # Ignorar líneas malformadas sin romper el pipe
    running = False

# ── Configuración de la figura ─────────────────────────────────────────
fig, axes = plt.subplots(3, 1, figsize=(12, 7), sharex=True)
fig.patch.set_facecolor('#0d0d0d')
fig.suptitle('Monitor de Vibración — SW-420 (RPi 5)', 
             color='white', fontsize=14, fontweight='bold', y=0.98)

colors      = ['#00ff9f', '#ff6b6b', '#4ecdc4']
labels      = ['Eje X  (amplitud)', 'Eje Y  (amplitud)', 'Eje Z  (amplitud + 1g)']
y_limits    = [(-6, 6), (-6, 6), (-1, 8)]
lines       = []
fill_areas  = []
time_axis   = np.arange(WINDOW_SIZE)

for i, (ax, color, label, ylim) in enumerate(zip(axes, colors, labels, y_limits)):
    ax.set_facecolor('#111111')
    ax.tick_params(colors='#888888')
    ax.spines['bottom'].set_color('#333333')
    ax.spines['top'].set_color('#333333')
    ax.spines['left'].set_color('#333333')
    ax.spines['right'].set_color('#333333')
    ax.set_ylabel(label, color=color, fontsize=9)
    ax.set_ylim(*ylim)
    ax.axhline(0, color='#333333', linewidth=0.8, linestyle='--')
    ax.grid(True, color='#1e1e1e', linewidth=0.5)

    line, = ax.plot(time_axis, [0] * WINDOW_SIZE, color=color, linewidth=1.2, antialiased=True)
    fill  = ax.fill_between(time_axis, [0]*WINDOW_SIZE, [0]*WINDOW_SIZE,
                             color=color, alpha=0.07)
    lines.append(line)
    fill_areas.append(fill)

axes[-1].set_xlabel('Muestras (más recientes a la derecha)', color='#888888', fontsize=9)

# Indicador de estado (vibración activa / quieto)
status_text = fig.text(0.82, 0.96, '● QUIETO', color='#4ecdc4',
                        fontsize=11, fontweight='bold', ha='center')

plt.tight_layout(rect=[0, 0, 1, 0.95])

# ── Función de actualización (llamada por FuncAnimation) ───────────────
def update(_frame):
    with lock:
        data_x = list(buf_x)
        data_y = list(buf_y)
        data_z = list(buf_z)

    for i, (line, data) in enumerate(zip(lines, [data_x, data_y, data_z])):
        arr = np.array(data)
        line.set_ydata(arr)

        # Redibujar área rellena
        fill_areas[i].remove()
        fill_areas[i] = axes[i].fill_between(
            time_axis, arr, 0,
            color=colors[i], alpha=0.07
        )

    # Actualizar indicador de estado según el último valor
    if abs(data_x[-1]) > 0.5 or abs(data_y[-1]) > 0.5:
        status_text.set_text('● VIBRANDO')
        status_text.set_color('#ff6b6b')
    else:
        status_text.set_text('● QUIETO')
        status_text.set_color('#4ecdc4')

    return lines

# ── Ejecutar ───────────────────────────────────────────────────────────
if __name__ == '__main__':
    t = threading.Thread(target=reader_thread, daemon=True)
    t.start()

    ani = animation.FuncAnimation(
        fig, update,
        interval=UPDATE_MS,
        blit=False,
        cache_frame_data=False
    )

    try:
        plt.show()
    except KeyboardInterrupt:
        pass

    print("\n[Plotter] Visualizador cerrado.", file=sys.stderr)