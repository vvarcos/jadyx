# Applets del firmware: editor de Mii

Estado, decisiones y límites del editor de Mii del firmware (`MiiEdit`) en este fork de
Jadyx, tanto abierto por un juego como a mano desde la biblioteca, y de la composición
multi-capa que hace que se vea.

> Documento rehecho el 23 oct 2026 para la entrega **0.3.8.6**, ya rebasada sobre
> Jadyx **0.3.8.5**. La implementación es la que se probó en la base 0.2.1.x; tras el
> rebase el código está portado, pero **los gates de PC y Series de esta base siguen
> pendientes**: no des por buena una fila de evidencia que hable de otra versión.

## Qué es y por qué no hay que escribirlo

- El "creador de Mii" **no es código de Eden**: es un **applet del firmware**, `MiiEdit`,
  programa `0x0100000000001009`, un NCA de tipo `Program` en el NAND del sistema
  (`LocalState/jadyx/nand/system/Contents/registered`). Solo aparece en la consola si el
  firmware importado lo trae; el importador lo exige (ver abajo).
- Eden trae únicamente un **stub HLE** (`src/core/frontend/applets/mii_edit.h`), que
  responde éxito al instante y no dibuja nada. Con la configuración por defecto
  (`mii_edit_applet_mode = AppletMode::LLE`, `src/common/settings.h`) el applet huésped
  **sí se crea** cuando un juego lo pide: `ShouldCreateGuestApplet()` devuelve `false`
  (HLE) solo si el modo no es LLE. El frontend UWP no toca ese ajuste.
- Un juego que solo **lee** la base de Miis no necesita el editor: al arrancar llama a
  `Service.Mii` (`mii_count`) y usa el Mii que ya exista. Por eso un Mii creado aparte se
  ve en el juego sin que el juego pida nada.
- El importador de firmware exige que el firmware traiga `MiiEdit`
  (`src/jadyx/storage/system_import.cpp`: `mii |= title_id == 0x0100000000001009 &&
  type == Program`, y sin él tira `firmware_incomplete` en modo estricto), así que
  "firmware importado" implica que el applet está. Ese mismo recorrido tolera ahora NCAs
  de juego dentro de la carpeta de firmware (ver abajo).

## Dos maneras de abrirlo

| Vía | Para qué | Estado en esta base |
|---|---|---|
| `boot.cfg` `applet=mii_edit` | Pruebas en PC | Portado; gate de PC en 0.2.1.x, pendiente en 0.3.8.6 |
| Biblioteca → Configuración → **Editor de Mii** | La consola (el `boot.cfg` del APPX no se edita) | Portado; gate de PC en 0.2.1.x, pendiente en 0.3.8.6 |

Un juego que pide el editor (Miitopia y otros) **no necesita ninguna de las dos**: el
applet se crea solo y lo único que hacía falta era componerlo en pantalla, que es lo que
sí está portado.

### La vía del `boot.cfg`

`BootConfig::applet` (parse `applet=`) busca el NCA con
`GetFileSystemController().GetSystemNANDContents()->GetEntry(program_id, Program)`, arma
`FrontendAppletParameters{.applet_id = MiiEdit, .applet_type = LibraryApplet}` +
`SetCurrentAppletId`, y **se salta** la resolución de contenido del juego, el proveedor
manual y Pulse. Fuerza `play=true`. Retornos propios: **20** applet desconocido, **21**
applet que no está en el firmware importado.

### La vía de la interfaz

- Fila `HomeMii` en el home de Configuración (`uwp_file_manager.h`,
  `ConfigurationHomeRow`), entre "Mandos y teclado" y "Gestor de archivos": 4 filas, con
  el paso y la altura ajustados para que la última no choque con el aviso ni con el pie.
- Texto nuevo en `localization.h` y en los tres catálogos (`en`, `es`, `pt_br`): `MiiEditor`,
  `MiiEditorHint`, `MiiEditorNoFirmware`. `MakeCatalog` es `consteval`, así que los tres
  catálogos llevan las mismas tres cadenas.
- **Sentinela UI → arranque**: `ShowGameLibrary` devuelve `"@applet:mii_edit"`
  (`Jadyx::MiiEditorRequest`, prefijo `Jadyx::AppletRequestPrefix` en `uwp_library.h`) en
  vez de una ruta de juego; `uwp_boot.cpp` lo separa del camino normal, rellena
  `config.applet` y deja `library_game` vacío, así que no se toca `nro_path`.
- **Puertas de la fila** (avisos en la propia pantalla, sin abrir nada): sin claves →
  `Text::KeysFirst`; sin firmware importado → `Text::MiiEditorNoFirmware`.

## La composición multi-capa: por qué el applet era invisible

Esta parte es lo que hace aparecer el editor sobre el juego. **El upstream sigue
componiendo solo la capa de abajo**, así que este cambio sigue siendo necesario aquí.

- `nvdisp`/HWC entregan la **pila de capas** de la pantalla de atrás hacia delante: la del
  juego primero, la del applet encima (`AppletZIndex`: juego `Foreground` z=1, applet
  `ForegroundVisible` z=2, `am_types.h`).
- Antes, `RendererD3D12::Composite()` componía **solo `framebuffers.front()`**: el applet se
  renderizaba pero nunca se presentaba. Síntoma exacto: el juego pide el editor, la imagen
  se queda en negro y no hay forma de crear el Mii.
- `CompositeLayers(std::span<const Tegra::FramebufferConfig>, PresentFrame&)`
  (`d3d12_present_blit.cpp`) resuelve cada capa con `rasterizer.AccelerateDisplay` +
  `Tegra::NormalizeCrop` y hace un blit por capa, de atrás hacia delante. Hay **una PSO
  por `Tegra::BlendMode`** (`Opaque`, `Premultiplied` `ONE/INV_SRC_ALPHA`, `Coverage`
  `SRC_ALPHA/INV_SRC_ALPHA`, con `SrcBlendAlpha=ONE/DestBlendAlpha=ZERO` como
  `glBlendFuncSeparate(..., GL_ONE, GL_ZERO)`), de modo que el applet se mezcla como pide
  el invitado (el teclado es translúcido, el editor de Mii es opaco).
- **FSR 1 queda apagado** en los frames con más de una capa (blit bilinear) sin cambiar el
  filtro guardado: `PlanFsr(layer, single_layer)` solo se planifica con una sola capa. Con
  varias, todos los SRV suben en un único rango del `descriptor_ring` para que ninguna
  asignación retire a la anterior.
- Si **alguna** capa no es una imagen de GPU, se cae al camino de CPU de siempre (la capa
  de abajo).
- Log nuevo, una línea por cambio de número de capas:
  `D3D12: compositing 2 guest layers back to front (the applet over the game)`.
- `d3d12_overlay.cpp` (menú del juego y captura F7) recompone igual con las últimas capas
  (`last_framebuffers`), sin perder `bare_composite` de la captura.

## Cierre de la sesión pedido por el invitado

Sin callback registrado, Eden avisa por log y **no hace nada**: el invitado que se cerraba
a sí mismo dejaba la imagen congelada. Las tres puertas:

1. El último applet desaparece → `WindowSystem` llama a `system.Exit()`. Es el caso del
   applet que fue la sesión entera.
2. `svcExitProcess` → `system.Exit()`.
3. `ILibraryAppletSelfAccessor::ExitProcessAndReturn` sin caller → `system.ExecuteProgram(0)`,
   que es lo que hace el editor de Mii al terminar.

`uwp_boot.cpp` registra `RegisterExitCallback` (siempre) y `RegisterExecuteProgramCallback`
**solo en un arranque de applet**; ambos levantan una bandera atómica sin estado propio y el
bucle de juego sale con `RETURN_TO_LIBRARY` (10), igual que "Volver a la biblioteca" del menú
del juego. No se guarda sesión Pulse (el invitado ya puede estar terminado) y el
`ExecuteProgram` de un juego se deja sin tocar.

**No hace falta para el caso del juego**: cuando el editor lo pidió un juego, el
`ExitProcessAndReturn` vuelve al juego (que es su caller) y la sesión sigue.

## Importación de firmware con archivos de juego

Un jugador suele tener los volcados juntos: la carpeta del firmware puede traer también NCAs
de juego. Antes el import entero se rechazaba con `firmware_game`; ahora esos archivos se
**quedan fuera** y el firmware se instala.

- `InspectFirmware(folder, strict)` (`system_import.cpp`) recoge los títulos fuera de
  `0x010000000000xxxx` en `outside`. Los que el meta `SystemUpdate` lista son contenido de
  sistema legítimo y se conservan; el resto son juegos.
- En modo estricto (solo el **stage** de una importación nueva) cada NCA de juego se borra
  del stage y se registra:
  `Firmware import: leaving out the game NCA {:016X} ('...')`. El stage es una copia, así
  que **los archivos que eligió el jugador no se tocan**.
- En modo no estricto (la carpeta instalada, que solo se inspecciona para informar de la
  versión) no se borra nada.
- Vale igual para ZIP y para archivos sueltos: los dos caminos extraen/copian al stage antes
  de inspeccionar.
- Si tras descartar los juegos no quedan `SystemVersion` y `MiiEdit`, el import sigue
  fallando con `firmware_incomplete`: no se puede instalar un firmware incompleto.

## Base de esta entrega (0.3.8.6)

- Rebasado sobre `JulianDr14/jadyx` **0.3.8.5**. Se reaplicaron: el editor de Mii (fila,
  `boot.cfg`, callbacks) y la composición multi-capa, más la tolerancia a NCAs de juego en
  el import.
- **No** se reaplican como cambios propios porque el upstream ya los trae: el catálogo
  **pt-BR** (el suyo es más completo) y el **prewarm de pipelines** (con su propio tuning,
  16 sesiones / 4096 PSOs).
- El **`pso_async`** del fork anterior (descartar el draw mientras el PSO compila) queda
  fuera de esta entrega: el upstream reescribió `PipelineCache` con búsqueda y seguimiento
  de reutilización de claves, y reaplicarlo exige rehacerlo sobre esa estructura.

## Evidencia

Las filas siguientes se midieron en la base **0.2.1.x**, con el mismo código de applet y
composición. Sirven como referencia de comportamiento, **no** como gate de 0.3.8.6.

| Gate | Resultado |
|---|---|
| **PC, Miitopia pide el editor (18 oct 2026)** | `01003DA010E8A000` v0 desde USB. Abre el NSP → **`compositing 1 guest layer back to front`** → el juego pide el applet (`GetLibraryAppletLaunchableEvent`) → **`compositing 2 guest layers back to front (the applet over the game)`** → el editor abre el teclado del frontend para el nombre → `ProcessTextCheck` → `PushOutData` + **`ExitProcessAndReturn`** → **`compositing 1 guest layer`** y el juego sigue. 0 `Render` Error/Critical. El usuario confirma: el editor salió por encima del juego, creó el Mii y volvió solo al juego. |
| **PC, biblioteca → Configuración → Editor de Mii (19 oct 2026)** | `library: opening the firmware applet mii_edit` → `library: booting the mii_edit applet from the imported firmware` → `applet: booting mii_edit from ...nca` → `step: system.Load() returned status 0` → `CompositeLayers: compositing 1 guest layer back to front`; 0 `Render` Error/Critical. Salida por el menú del juego ("Volver a la biblioteca") y la biblioteca vuelve a explorar. |
| **PC, `applet=mii_edit` (19 oct 2026)** | `boot.cfg: applet mii_edit` → `applet: booting mii_edit from ...nca` → `step: system.Load() returned status 0` → `CompositeLayers: compositing 1 guest layer back to front`. Al cerrarse solo: `PushOutData` + `ExitProcessAndReturn` disparan el callback (`the applet asked for program 0, leaving the session`) → `guest: asked for another program it has no caller for; back to the library` → `RunHeadlessBoot returned 10`. |

## Límites (lo que esto no certifica)

- **Nada de esto está probado en la Series**, ni en 0.2.1.x ni en 0.3.8.6.
- Tras el rebase, **el gate de PC de 0.3.8.6 está pendiente**: arranque del applet,
  composición de 2 capas y salida limpia.
- **Solo un juego** ha pedido el editor en un log real (Miitopia): otros juegos que lo usan
  (Smash Ultimate, Switch Sports, Mario Golf) no se han probado, y MK8D no lo pide (solo lee
  la base de Miis).
- No se ha medido FPS con el editor abierto desde la fila de Configuración (el gate de PC es
  de arranque/composición/salida, no de rendimiento).
- Si el applet se cuelga (no termina), no hay temporizador: la salida sigue siendo el menú
  del juego ("Volver a la biblioteca") o cerrar la app.
- El primer Mii del editor se guarda en `nand/system/save/8000000000000030/MiiDatabase.dat`
  (tamaño fijo 6808 B): un `mii_count` mayor es la señal de que se guardó.
- Abrir el editor **no** crea una sesión Pulse y volver de él no la guarda.
- El mando del applet por encima del juego no se ha validado aparte: en el gate de PC la
  entrada fue de teclado/ratón.
- El import tolerante se ha ejercitado con la lógica de `InspectFirmware`; falta una prueba
  con una carpeta real que mezcle firmware y juegos en esta base.

## Fuentes

- Eden: `src/core/hle/service/am/{window_system,applet_manager,display_layer_manager}.cpp`,
  `service/{library_applet_creator,library_applet_self_accessor,global_state_controller}.cpp`,
  `src/core/frontend/applets/mii_edit.h`, `src/core/hle/kernel/svc/svc_process.cpp`.
- Backend Vulkan de Eden (`vk_present_pipeline`/`vk_swapchain`) como modelo de composición
  por capas, y `renderer_opengl` (`glBlendFuncSeparate`) para los modos de mezcla.
- Qt de Eden: `LaunchFirmwareApplet(AppletProgramId::MiiEdit, ...)`
  (`src/yuzu/main_window.cpp`) y sus callbacks de `Exit`/`ExecuteProgram`: el mismo contrato
  que implementa `uwp_boot.cpp`.
