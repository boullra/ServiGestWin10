# ServiGestWin10 1.1.0 — Gestor masivo de servicios de Windows

Aplicación nativa de Windows (C++17 + API Win32, sin dependencias externas, un único `.exe`
estático de ~555 KB) para listar, configurar, iniciar y detener servicios **por lotes**, y para
guardar y restaurar el estado completo de los servicios mediante un archivo CSV.

## Uso rápido

1. Ejecuta `ServiGestWin10.exe`. Si tu cuenta no tiene ya privilegios elevados, la app pide
   la elevación (UAC) al arrancar. Si la rechazas, funciona en **modo sólo lectura** (se
   puede listar y guardar, pero no modificar). `Abrir_solo_lectura.bat` la abre sin pedir elevación.
2. La lista de servicios aparece nada más abrirse, con las mismas columnas que `services.msc`
   (Nombre, Descripción, Estado, Tipo de inicio, Iniciar sesión como) más:
   * **Nombre del servicio** (el nombre interno, el que se usa en el CSV y con `sc`).
   * **Inicio por defecto (Win10)**: en **naranja** cuando el tipo actual es distinto del de fábrica.
   * **Crítico / nota**: el motivo por el que un servicio está en rojo.
3. Marca los servicios con sus casillas (la casilla de la **cabecera** marca o desmarca todos los
   mostrados). También: *Marcar todo*, *Desmarcar todo*, *Invertir marcas*, menú *Selección*
   (marcar o desmarcar las filas seleccionadas, desmarcar todos los críticos).
4. Acciones sobre los **marcados**:
   * **Tipo de inicio** + **Aplicar a marcados**: Automático, Automático (inicio retrasado), Manual
     o Deshabilitado. Para aplicarlo a los *no marcados*: pulsa *Invertir marcas* y luego aplica.
   * **Iniciar / Detener / Reiniciar marcados**. Al detener se puede detener también a los dependientes.
   * El clic derecho sobre filas seleccionadas ofrece las mismas acciones sobre la selección.
5. **Guardar estado...** (Ctrl+S): guarda *todos* los servicios con su tipo de inicio y estado.
   **Restaurar estado...** (Ctrl+O): muestra un resumen de los cambios y los aplica en orden seguro
   (primero los tipos de inicio, después las detenciones y por último los inicios).

Las operaciones van en segundo plano (la ventana no se congela), con progreso en la barra de
estado, un registro detallado abajo y un botón **Cancelar**. Los estados se refrescan cada 3 s; F5
recarga todo. **Buscar** (Ctrl+F) filtra por nombre, nombre de servicio o descripción.

## Idiomas

La interfaz está disponible en **inglés, español, francés y chino simplificado**.

* **Al arrancar** se usa el idioma de la **interfaz de Windows** (lista de idiomas preferidos del
  usuario). Si no es uno de los cuatro (por ejemplo alemán o chino tradicional), se usa el siguiente
  de la lista de preferencias de Windows que sí lo sea, y en último término el **inglés**.
* **Menú *Idioma / Language***: cambia el idioma en caliente (sin reiniciar) y lo recuerda en
  `HKCU\Software\ServiGestWin10\Language`. *Automático* vuelve a seguir el idioma de Windows. Los
  nombres de los idiomas se muestran siempre en su propio idioma ("Français", "简体中文").
* **`/lang:en|es|fr|zh-CN`** fuerza un idioma para esa ejecución sin guardarlo.
* **Números y horas** siguen la **configuración regional** del usuario (Panel de control > Región),
  que es la convención de Windows: un francés con Windows en inglés ve la interfaz en inglés y las
  horas y números en formato francés.
* Los **nombres y descripciones de los servicios** los traduce el propio Windows. Los botones de los
  cuadros de mensaje del sistema ("Aceptar", "Cancelar") salen en el idioma de Windows.
* En chino se usa la fuente *Microsoft YaHei UI* (incluida en Windows 10/11). Si faltara, se usa la
  primera fuente instalada con chino simplificado.

**Archivos de recursos.** Los textos están en `lang/<código>.lang`: UTF-8, una línea `clave = texto`,
con marcadores `{0}`, `{1}`… cuyo orden puede cambiar según el idioma, y plurales `clave.one` /
`clave.other` con la regla de cada idioma (el francés trata el 0 como singular y el chino no tiene
plural). Los cuatro archivos van **incrustados en el `.exe`**. Un archivo `lang\<código>.lang`
colocado junto al `.exe` sustituye las entradas del incrustado, lo que permite corregir una traducción
sin recompilar.

**Añadir un idioma**: copiar `lang/en.lang` a `lang/<código>.lang` y traducirlo, añadir el código en
`SupportedLanguages()` (`src/i18n.cpp`), su regla de plural en `PluralCategory()`, una línea `RCDATA`
en `res/app.rc` y el código en `tools/check_i18n.py`. `build.sh` ejecuta `tools/check_i18n.py`, que
falla si a algún idioma le falta una clave, le sobra o tiene marcadores distintos, o si el código usa
una clave inexistente.

> Las traducciones al francés y al chino las ha hecho Claude siguiendo la terminología de
> `services.msc`. Antes de una distribución comercial conviene que las revise un hablante nativo.

## Protección de servicios críticos (filas rojas)

34 servicios marcados en rojo, con una barra roja a la izquierda (RPC, DCOM, LSM, SamSs, perfiles de
usuario, Plug and Play, registro de eventos, WMI, cifrado, DHCP/DNS, firewall, UAC, Windows Installer,
etc.). También se marca en rojo cualquier servicio que Windows declare como crítico para el arranque
(`ErrorControl = Critical`). La lista es un **criterio propio** documentado en `src/knowledge.cpp`
(los motivos traducidos están en `lang/*.lang`, claves `crit.<Servicio>`).

Si una acción afectaría negativamente a un servicio crítico (deshabilitarlo, pasarlo a Manual cuando
por defecto es Automático, o detenerlo), la app avisa y ofrece **omitirlo (opción recomendada y por
defecto)** o incluirlo, con una segunda confirmación. Los críticos que por defecto son Manual (como
TrustedInstaller o msiserver) se detienen solos con normalidad: detenerlos no genera aviso, pero
deshabilitarlos sí.

## Columna «Inicio por defecto (Win10)»

Contiene 263 servicios, tomados de una **instalación limpia de Windows 10 Pro 22H2** (tabla de
winhelponline.com, fuente secundaria: no es documentación de Microsoft). Los valores pueden variar
según la edición (Home, Enterprise), la versión, el hardware y el software instalado. *Desconocido*
indica un servicio de terceros o que no está en la tabla. Las instancias por usuario (por ejemplo
`CDPUserSvc_4fefbb0`) se buscan por el nombre de su plantilla.

## Formato del CSV

UTF-8 con BOM y separador `;` (se abre directamente en Excel en español). Las columnas `StartType` y
`State` usan **palabras clave en inglés, independientes del idioma**: un archivo guardado con la
interfaz en un idioma se restaura igual con la interfaz en otro. Las columnas `*Label` van en el
idioma activo, solo para leerlas:

```
ServiceName;DisplayName;StartType;State;StartTypeLabel;StateLabel
"Spooler";"Cola de impresión";"Automatic";"Running";"Automático";"En ejecución"
```

* `StartType`: `Automatic`, `AutomaticDelayed`, `Manual`, `Disabled`.
* `State`: `Running`, `Stopped`, `Paused`, etc.
* Al restaurar se usan `ServiceName`, `StartType` y `State`. Se aceptan también `,` o tabulador como
  separador, etiquetas escritas a mano en cualquiera de los cuatro idiomas (por ejemplo
  "Automatique (début différé)" o "自动(延迟启动)") y archivos guardados por Excel en ANSI. Los servicios del archivo que
  no existen en el equipo se informan y se ignoran. Los que existen en el equipo pero no en el archivo
  no se tocan.

## Línea de comandos (desde una consola de administrador)

```
ServiGestWin10.exe /export estado.csv
ServiGestWin10.exe /import estado.csv                    (simulación: sólo muestra el plan)
ServiGestWin10.exe /import estado.csv /apply             (aplica, omitiendo los críticos)
ServiGestWin10.exe /import estado.csv /apply /includecritical
ServiGestWin10.exe /help /lang:fr                         (la salida también se traduce)
```

## Compilar

* **Visual Studio 2019/2022**: abrir la carpeta (CMake) y compilar, o
  `cmake -S . -B build && cmake --build build --config Release`.
* **MinGW-w64** (Windows o Linux): `./build.sh` → `build/ServiGestWin10.exe` y `build/test_core.exe`
  (se ejecuta desde la raíz del repositorio: `build/test_core.exe lang`).

Estructura:

| Ruta | Contenido |
|---|---|
| `src/core.*` | Lógica portable: CSV, etiquetas, plan de restauración |
| `src/i18n.*` | Motor de localización portable (catálogos, marcadores, plurales, elección de idioma) |
| `src/lang_win.*` | Integración con Windows: recursos incrustados, idioma de Windows, registro, formatos regionales |
| `src/knowledge.cpp` | Tablas de modos por defecto y de servicios críticos |
| `src/scm.*` | API del Service Control Manager |
| `src/main.cpp` | Interfaz gráfica y línea de comandos (sin textos fijos: todo pasa por `T()`, `F()`, `P()`) |
| `lang/*.lang` | Archivos de recursos de idioma |
| `tools/check_i18n.py` | Comprobación de coherencia de los idiomas |
| `tests/test_core.cpp` | 415 comprobaciones |
| `res/` | Manifiesto, icono, versión e idiomas incrustados |

## Qué se ha probado y qué no

| Prueba | Dónde | Resultado |
|---|---|---|
| 415 tests unitarios (CSV, UTF-8/ANSI, tablas, plan de restauración, motor i18n, coherencia de los 4 idiomas, etiquetas de cada idioma reconocidas al leer CSV) | Linux (con sanitizers) y como `.exe` de Windows | OK |
| `check_i18n.py`: 272 claves, 213 usadas en el código, mismas claves y marcadores en los 4 idiomas | Linux | OK |
| Compilación sin avisos (`-Wall -Wextra`), con build.sh y con CMake | MinGW-w64 GCC 13 | OK |
| Exportar, simular, aplicar y restaurar (el estado final queda idéntico al original) | Wine 9 | OK |
| Interfaz: marcar, filtrar, cambiar el tipo de inicio, iniciar, detener, diálogo de críticos, guardar, restaurar | Wine 9 | OK |
| Listado real (300 servicios, nombres y descripciones en español, desencadenadores, instancias por usuario), filas rojas, casilla de cabecera, columna de notas | **Tu Windows 10** | OK |
| Interfaz en los 4 idiomas, cambio de idioma en caliente, preferencia recordada, detección del idioma de Windows, CLI traducida, CSV guardado en un idioma y restaurado en otro | Wine 9 | OK |
| Detección automática (Windows en español → español), interfaz completa en chino (título, menús, botones, mensajes, barra de estado) | **Tu Windows 10** | OK |
| Modificar servicios reales (tipo de inicio, inicio retrasado, iniciar, detener) | **Tu Windows 10** | **No probado** |

No se modificó ningún servicio de tu equipo sin permiso. En Wine, activar el *inicio retrasado*
devuelve "Invalid level" porque Wine no implementa esa función. En Windows está soportada desde Vista
(`ChangeServiceConfig2` con `SERVICE_CONFIG_DELAYED_AUTO_START_INFO`), pero se recomienda una primera
prueba con un servicio inofensivo.

## Recomendación

**Guarda siempre el estado (Ctrl+S) antes de hacer cambios masivos.** Así puedes volver atrás con
*Restaurar estado*.
