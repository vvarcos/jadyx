# Presentación, vsync y spins: el techo de 20 FPS (28 octubre 2026)

Serie de mediciones sobre Game A en la Series, con `gpu_profile` apagado salvo donde se
indica. El objetivo era subir de los 20,0 FPS estables que el juego mantenía en todas las
áreas, y el resultado fue 23–27,7 FPS con la velocidad de simulación correcta. El camino
largo hasta aquí importa tanto como el resultado: la mayor parte del tiempo se gastó
optimizando fases que el propio instrumento estaba inflando.

## Evidencia de partida

Janela de 300 frames, build 3.9.19 (sin cortes de spin):

```
300 frames en 14.731 ms ................ 48,5 ms/frame = 20,3 FPS
vsyncs 884 para 300 frames ............. 2,95 vsyncs por frame
game waited for a free framebuffer ..... 600 veces / 14.033 ms  (47 ms por frame)
GPU thread: ocupada ~44 ms, ociosa ~4 ms
GPU busy .............................. 3.477 ms / 11,6 ms por frame (24%)
```

20,0 FPS = 60/3 exacto, frame de 50,0 ms sin variancia y casi tres refrescos por frame.
Eso no es el perfil de un límite de CPU: es cuantización por vsync. El juego esperaba un
framebuffer libre casi todo el frame porque el present no devolvía buffers hasta el
siguiente múltiplo de refresco.

## El spin: 1,1 de 4 núcleos

El perfil de muestreo (`profile=1`, `tools/xbox/profile-report.py`) del build 3.9.18:

| hilo | CPU (% de un núcleo) | dónde |
| --- | --- | --- |
| HostTiming | 88,7% | 96,5% en `Common::CpuWaitForBits` dentro de `Event::WaitFor` |
| CPUCore_2 | 67,9% | 53,6% JIT guest + 24,5% spin de idle |
| CPUCore_1 | 62,1% | 41% JIT + 11% spin de idle |
| GPU | 50,3% | graba draws |
| CPUCore_0 | 49,6% | 7,3% spin de idle |

Sumando solo spin: 0,86 (HostTiming) + 0,27 (idle de los núcleos guest) ≈ **1,1 de 4
núcleos**. No era trabajo: eran hilos que se quedaban con el núcleo dentro de una espera.

`Event::WaitFor` duerme hasta la latencia aprendida del timer antes del deadline y gira ese
tramo en la CPU; como los eventos de timing del guest esperan menos que ese tramo, **cada
espera gira entera**. Se añadió `timer_spin=<us>` (0 conserva el comportamiento anterior) y
se usó el `idle_spin` que ya existía para los núcleos ociosos.

Después del corte (3.9.26), el mismo perfil da `HostTiming 0,2%`, `idle_spin` con cero
wakeups por spin, y la GPU thread con 3,8 s más de tiempo ocioso por ventana. **El FPS no
subió**: el spin no estaba en el camino crítico, pero liberó ~1,1 núcleo que ahora usa el
resto del emulador.

## El limitador de velocidad no existe en multicore

Con el vsync del host apagado (`vsync=0`) el juego pasó de 20 a casi 40 FPS en interiores,
pero **acelerado**: `Core::SpeedLimiter::DoSpeedLimiting` retorna de inmediato cuando
`use_multi_core` está activo, así que nada marca el ritmo de un juego que avanza la
simulación una vez por frame presentado.

Se añadió `frame_limit=<fps>` en `nvdisp_disp0::Composite`: el present duerme hasta el
siguiente hueco de una cadencia fija de 1/fps. La primera versión sumaba el intervalo antes
de dormir y podía empujar un frame que ya estaba en tasa, produciendo la cadencia irregular
que se percibía como aceleración; la versión vigente duerme solo lo que falta y reinicia la
cadencia cuando el guest está por detrás, para no acumular backlog (que saldría como
ráfagas).

## Estado medido al cerrar

Build 3.9.24 (vsync off, `frame_limit=60`), la configuración más rápida y estable:

| | antes | después |
| --- | --- | --- |
| FPS | 20,0 fijo | 23–27,7 según área |
| velocidad del juego | aceleraba sin vsync | correcta |
| vsyncs perdidos | 193 (vsync on) / 0 | 0 |
| espera por framebuffer | 600 veces / 16,9 s | 0–94 veces / 0,4 s |
| stutter de pipeline | 3,9 s por ventana | ~0 con caché caliente |
| CPU por núcleo | 3,8 de 4 | ~2,4 de 4 |

Perfil 3.9.26 (config buena): **ningún hilo saturado** (≥85% de un núcleo). CPUCore_2 70,2%,
CPUCore_0 61,0%, CPUCore_1 56,8%, GPU 52,4%. Durante juego, la GPU thread ronda el 89% del
frame y su coste es plano: el mayor ítem propio es 4,3% (0,02 de un núcleo), con
`DmaPusher` 7,2%, `GraphicsPipeline::Configure` 3,1%, `BindHostGraphicsUniformBuffer` 2,3%
y `VisitImageView` 2,2%. Los 4–6 ms que faltan para 30 FPS estables no están en un solo
sitio: requieren trabajo amplio en el camino de draw, no un ajuste.

## Lo que NO era el cuello (y cómo se concluyó)

- **Uniform buffers**: `BindHostGraphicsUniformBuffer` cuesta 29 ns por draw (34 ms por
  ventana de 300 frames), 0,4% del draw. Los tres builds dedicados no movieron el FPS.
- **Stage buffers**: la fase medía 1,1 µs por draw, pero 1.232 de sus 1.268 ms eran **el
  coste de los propios timers** (2 QPC por etapa, 5 etapas, 1,16 M draws ≈ 100 ns cada uno).
  El trabajo real era 0,09 µs por draw.
- **Descriptor build**: igual, inflado por los timers anidados de cada etapa.

La lección de método: en este camino cada `ScopedNsTimer` cuesta ~100 ns y hay entre 5 y 10
por draw, así que **las fases de menos de ~1 µs no se pueden medir con timers por fase**.
Para esos tamaños hace falta A/B de FPS de extremo a extremo con `gpu_profile` apagado, o un
contador acumulativo sin timer.

## Configuración recomendada de prueba

```
vsync=0          # el vsync del host cuantiza cada frame a 3 refrescos
frame_limit=30   # mantiene la velocidad del juego; 60 para juegos de 60 Hz
timer_spin=60    # corta el giro de precisión del host timing (0 = anterior)
idle_spin=off    # los núcleos guest ociosos bloquean en vez de girar
```

`vsync=0` puede producir tearing; es el precio de dejar el present libre. Con
`frame_limit=30` el juego conserva su velocidad porque la cadencia la marca el emulador.

## Gates pendientes

- 30 FPS estables en las áreas pesadas: faltan 4–6 ms por frame y el perfil es plano.
  Candidatos por orden de coste medido: procesamiento de comandos (`DmaPusher`), caché de
  buffers (17% del hilo GPU), caché de texturas (12,8%) y setup de draw (12%).
- Tearing con `vsync=0`: evaluar presentar con intervalo 1 pero cadencia de 30 (un refresco
  de cada dos) para conservar la velocidad sin tearing.
- El `frame_limit` solo se probó con el pacer corregido en 3.9.25/26, donde el juego se
  mantuvo en 23–26 FPS: falta una prueba en área ligera donde el tope de 30 sí actúe.
