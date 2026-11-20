# Sistema de Gestion de Taller Mecanico - Proyecto 2

**Programacion 2 - Proyecto 2 - 2026-C**
**Integrantes:** Estefano Barboza y Isabella Finol

## 1. Descripcion

Evolucion del Proyecto 1: ahora los datos viven en **archivos binarios** dentro de la carpeta `datos/` y sobreviven entre ejecuciones.
Ya no existe `SistemaTaller` ni arreglos dinamicos en RAM: cada operacion abre el archivo, calcula la posicion del registro,
salta con `seekg/seekp`, lee o escribe **un solo registro** y cierra. Se usa solo `<fstream>` (nada de `fopen`).
Las lineas de detalle de cada orden se guardan en un archivo propio, `detalles.bin` (ver seccion 5).

## 2. Compilacion y ejecucion

```bash
g++ -std=c++11 -Wall -Wextra -o taller proyecto_II.cpp
./taller                                   # modo interactivo (usa la carpeta datos/)
bash pruebas/ejecutar_pruebas.sh ./taller          # casos de uso 1 a 8, desde datos vacios
bash pruebas/ejecutar_validaciones.sh ./taller     # validaciones 9a a 9d, sobre una copia de datos/
```

- Al arrancar, el programa crea `datos/` y cada `.bin` de entidades que falte con su header inicial (`vehiculos`, `mecanicos`, `repuestos`, `ordenes` y `detalles`).
  Lo que ya existe **no se toca**, por eso la informacion sobrevive entre ejecuciones. En el primer arranque (si no existe `taller.bin`) pide los datos del taller; despues solo los lee.
- La carpeta `datos/` entregada ya viene **pre-poblada**: 8 vehiculos, 4 mecanicos (1 dado de baja), 12 repuestos, 10 ordenes (6 CERRADAS, 2 ANULADAS, 2 ABIERTAS)
  y 24 lineas de detalle (3 ordenes con 4 o mas lineas: 4, 5 y 4). Se genero con `pruebas/entrada_0_poblar.txt`.
- Las entradas de `pruebas/` (casos 1 a 8) estan pensadas para correr **en orden y partiendo de datos vacios**. Para reproducirlas use `pruebas/ejecutar_pruebas.sh`,
  que trabaja en una carpeta temporal y **no toca** `datos/`.
- Cuando la entrada viene de un archivo (`./taller < entrada.txt`) el programa repite en pantalla cada linea leida, las lineas que empiezan con `#` son comentarios
  y no se limpia ni se pausa la pantalla (asi `salida_X.txt` sale completa). En modo interactivo la pantalla se limpia al elegir una opcion y se espera un ENTER.
- En cualquier dato de un registro se puede escribir `CANCELAR` para abortarlo sin crear nada.
- Toda la salida es ASCII puro (sin acentos): menus con lineas `====` y tablas con `+---+` y `|`.

## 3. Verificacion de opciones de los menus

- **Opciones validas:** cada menu lee la opcion con `pedirOpcion`, que solo acepta un numero entero dentro del rango del menu.
  Si se escribe texto, una linea vacia o un numero fuera de rango, muestra el error y vuelve a pedir.
- **Opciones que no tienen sentido todavia:** las opciones de un modulo (buscar, actualizar, eliminar, listar...) se pueden elegir siempre, pero **al elegirlas el programa verifica**
  (`verificarHay`) que haya algo registrado. Si no lo hay, no continua y explica por que, por ejemplo:
  `ERROR: No hay vehiculos registrados. Primero registre uno con la opcion 1.`
- Cada submenu muestra un contador que se recalcula cada vez que se dibuja (`Vehiculos registrados: 8`, `Ordenes: 10 (abiertas: 2)`...). Los totales y activos se leen del header sin recorrer el archivo; solo el numero de ordenes abiertas recorre `ordenes.bin`.
- Las acciones destructivas (eliminar, anular, cerrar, dar de baja, crear respaldo) piden confirmacion S/N.

## 4. Estructuras: sizeof y posicion en disco

Valores medidos compilando el programa:

| Estructura | sizeof |
|---|---|
| `ArchivoHeader` (4 ints) | 16 bytes |
| `Taller` (sin header, un solo registro) | 360 bytes |
| `Vehiculo` | 288 bytes |
| `Mecanico` | 176 bytes |
| `Repuesto` | 208 bytes |
| `OrdenServicio` | 280 bytes |
| `DetalleOrden` (una linea de detalle) | 40 bytes |

Formula de posicion del registro con ID `id` (indice fisico `i = id - 1`):

```
posicion = sizeof(ArchivoHeader) + (id - 1) * sizeof(Estructura)
```

Ejemplo (se ve en el mensaje `[DEPURACION]` al buscar un vehiculo por ID): vehiculo ID 5 -> `16 + (5 - 1) * 288 = 1168` bytes.

### Relacion entre ID e indice fisico

**indice = ID - 1.** Funciona porque (a) los IDs se asignan en orden de insercion empezando en 1, (b) los registros siempre se agregan al final
y (c) nunca se mueven ni se borran fisicamente (el borrado es logico). Dejaria de ser valida si: se eliminara fisicamente un registro y se compactara el archivo,
se reordenaran los registros, se reutilizaran IDs o se fusionaran dos archivos. Por eso `leerRegistroPorID` calcula la posicion directamente (despues de comprobar con el header que el ID exista)
y la busqueda por ID nunca recorre el archivo. La verificacion de integridad comprueba esta relacion: reporta si el registro numero `i` de un archivo no tiene `id == i`.

## 5. Diseno del detalle de la orden

**Estructura (un archivo aparte, `detalles.bin`, con header como todos los de entidades):**

```cpp
struct DetalleOrden {
    int   id;
    int   idOrden;           // a que orden pertenece la linea
    int   idRepuesto;
    int   cantidad;
    float precioUnitario;    // precio AL MOMENTO de agregar la linea (precio historico)
    bool  eliminado;         // quitar una linea = borrado logico
    time_t fechaCreacion;
    time_t fechaUltimaModificacion;
};                           // sizeof = 40 bytes
```

`OrdenServicio` (280 bytes) **no contiene** las lineas: cada linea es un registro propio de `detalles.bin` que apunta a su orden por `idOrden`.
Asi todas las ordenes pesan lo mismo, las lineas tambien, y las ordenes nunca crecen ni se mueven.

**Bytes en disco:**

| Caso | Orden (`ordenes.bin`) | Lineas (`detalles.bin`) | Total |
|---|---|---|---|
| Orden con 1 linea | 280 | 1 x 40 = 40 | **320 bytes** |
| Orden con 15 lineas | 280 | 15 x 40 = 600 | **880 bytes** |

No hay espacio reservado sin usar (cada linea ocupa espacio solo cuando existe). El costo es otro: de los 40 bytes de una linea, solo 16 son datos de negocio
(`idOrden`, `idRepuesto`, `cantidad`, `precioUnitario`); el resto es el `id`, la marca `eliminado` y las fechas. Ademas, las lineas quitadas siguen ocupando su lugar en el archivo (borrado logico).
`datos/detalles.bin` entregado pesa `16 + 24 * 40 = 976` bytes (24 lineas, incluidas las de ordenes anuladas).

**Como cumple los requisitos de la seccion 3 del enunciado:**
1. Las lineas se guardan en `detalles.bin` con repuesto, cantidad y precio historico, y sobreviven entre ejecuciones (casos de uso 3 y 4).
2. Admite **20 lineas activas por orden** (`MAX_LINEAS_ORDEN = 20`, mas que las 15 pedidas). La linea 21 distinta se rechaza con un mensaje claro (prueba `9a`).
3. Todos los registros de un mismo archivo ocupan el mismo tamano.
4. Consultar las lineas de una orden (`obtenerLineasDeOrden`) recorre `detalles.bin` leyendo **un registro a la vez** y se queda con los de esa orden: no carga ninguna otra orden en memoria.
5. Las reglas del Proyecto 1 se mantienen: si el repuesto ya esta en la orden se **acumula en su linea** (conserva el precio con el que se creo), quitar elimina la **linea completa** devolviendo su stock,
   y el total se calcula siempre desde las lineas (`manoObra + suma(cantidad * precioUnitario)`).

**Alternativa que consideramos y descartamos:** guardar las lineas **dentro** de la orden, como un arreglo fijo (`DetalleOrden detalles[15]` mas un contador `numDetalles`).
Cumple los requisitos, ordenar y leer es mas simple (una orden = un registro) y no hace falta un archivo adicional, pero: (1) reserva espacio para 15 lineas aunque la orden tenga una sola
(con 12 bytes por linea, una orden con 1 linea desperdicia 168 bytes), (2) el limite de lineas queda **fijo en la estructura**: cambiarlo cambia el `sizeof` y obliga a reconvertir `ordenes.bin`,
y (3) quitar una linea obliga a correr las siguientes y reescribir la orden completa. Con `detalles.bin` el limite es solo una constante de logica (`MAX_LINEAS_ORDEN`).
El costo de nuestra eleccion es que consultar las lineas de una orden recorre todo `detalles.bin` y que agregar un repuesto toca dos archivos (detalles y repuestos) en vez de uno.

**Como se resuelve quitar una linea:** se busca la linea activa del repuesto en esa orden (`buscarLineaActiva`) y se hace su **borrado logico** (`eliminado = true`,
`registrosActivos--` en el header de `detalles.bin`). Despues se devuelve su cantidad al stock del repuesto. Si el repuesto se vuelve a agregar a la orden, se crea una **linea nueva** (la anterior queda como historial eliminado).
Las lineas de una orden ANULADA se conservan (solo se devuelve el stock): por eso un repuesto que aparece en una orden anulada tampoco se puede eliminar.

## 6. Capas y firmas

El `.cpp` esta dividido en tres capas:

- **Persistencia** (seccion 2 del `.cpp`): lee y escribe registros crudos. **No usa `cin/cout`**, no aplica reglas de negocio y no filtra eliminados.
- **Logica** (seccion 3): reglas de negocio. **No usa `cin/cout` y no abre archivos directamente** (todo pasa por la persistencia). **Ninguna funcion retorna un puntero a un registro.**
- **Presentacion** (seccion 4): menus, lectura de datos y tablas. **No abre archivos ni modifica datos**: siempre llama a la logica.

### Como informan exito, fallo y causa

- **Persistencia:** retorna `ResArchivo` (`ARCH_OK`, `ARCH_ERROR`, `ARCH_ID_NO_EXISTE`). Basta para distinguir "no se pudo abrir/leer" de "ese ID no existe".
- **Logica:** retorna `Resultado`, con una causa por cada regla de negocio que puede fallar (`RES_DUPLICADO`, `RES_MECANICO_LLENO`, `RES_KM_MENOR`, `RES_STOCK_INSUFICIENTE`...).
  Las funciones de la logica **reciben copias y entregan copias** por parametros de salida (`Vehiculo* salida`). La presentacion traduce cada `Resultado` a un mensaje con `textoResultado()`
  y agrega los detalles que necesita (por ejemplo, el estado actual de la orden).
- **Busquedas y listados:** entregan como maximo `MAX_RESULTADOS = 20` registros por llamada; `*hayMas` avisa si quedan y `saltar` permite pedir la pagina siguiente.
  La presentacion avisa `HAY MAS resultados` y pregunta `Ver los siguientes? (S/N)`.

```cpp
enum ResArchivo { ARCH_OK, ARCH_ERROR, ARCH_ID_NO_EXISTE };

enum Resultado { RES_OK, RES_ERROR_ARCHIVO, RES_DATO_INVALIDO, RES_DUPLICADO, RES_VEHICULO_NO_EXISTE,
  RES_VEHICULO_ELIMINADO, RES_MECANICO_NO_EXISTE, RES_MECANICO_DE_BAJA, RES_REPUESTO_NO_EXISTE,
  RES_REPUESTO_ELIMINADO, RES_ORDEN_NO_EXISTE, RES_CANTIDAD_INVALIDA, RES_PRECIO_INVALIDO,
  RES_VEHICULO_CON_ORDEN_ABIERTA, RES_VEHICULO_CON_ORDENES, RES_MECANICO_LLENO,
  RES_MECANICO_CON_ORDENES_ABIERTAS, RES_KM_MENOR, RES_ORDEN_NO_ABIERTA, RES_ORDEN_NO_ANULABLE,
  RES_STOCK_INSUFICIENTE, RES_LINEA_NO_EXISTE, RES_LIMITE_LINEAS, RES_ORDEN_SIN_CONTENIDO, RES_REPUESTO_EN_USO };
```

### Capa de persistencia

Operaciones sobre el header (sirven para cualquier archivo de entidades):

| Firma | Retorno |
|---|---|
| `bool inicializarArchivo(const char* ruta)` | `true` si el archivo quedo listo (existe y tiene header completo). Lo crea con `cantidadRegistros=0`, `proximoID=1`, `registrosActivos=0`, `version=1` solo si no existe; si existe no lo modifica. |
| `bool leerHeader(const char* ruta, ArchivoHeader* h)` | `false` si no se pudo abrir o leer. Lee solo el header (posicion 0). |
| `bool escribirHeader(const char* ruta, const ArchivoHeader* h)` | `false` si fallo. Sobrescribe solo el header, sin tocar los registros (se abre con `in\|out`). |
| `int totalRegistros(ruta)` / `int contarActivos(ruta)` | Cantidad leida del header sin recorrer el archivo (`-1` si fallo). |

Acceso aleatorio generico (lo usan los envoltorios de cada entidad):

| Firma | Retorno |
|---|---|
| `long calcularPosicion(int id, int tam)` | La posicion en bytes (formula de la seccion 4). |
| `ResArchivo leerRegistroPorID(ruta, tam, id, void* destino, long* posicion)` | Ubica el registro por calculo y lo entrega **activo o eliminado**; `ARCH_ID_NO_EXISTE` si el ID esta fuera del rango del header. Deja en `posicion` donde leyo (para el mensaje de depuracion). |
| `ResArchivo sobrescribirRegistroPorID(ruta, tam, id, const void* origen)` | Escribe en su posicion original con `seekp`. |
| `ResArchivo agregarRegistroAlFinal(ruta, tam, const void* origen, ArchivoHeader h)` | Escribe al final y despues actualiza el header (`cantidadRegistros++`, `registrosActivos++`, `proximoID++`). |
| `bool leerRegistroCrudo(ruta, tam, numero, destino, posicion)` | Lee sin consultar el header; la usa la verificacion de integridad, que no puede fiarse del header. |

Las **5 operaciones por entidad** (vehiculos, mecanicos, repuestos, ordenes y detalles; envoltorios delgados sobre las funciones genericas):

| Operacion | Firma (ejemplo con vehiculos) | Retorno |
|---|---|---|
| Guardar nuevo | `ResArchivo guardarVehiculo(Vehiculo* v, int* idAsignado)` | Asigna el ID desde el header, `eliminado = false` y las fechas; el llamador recibe el ID asignado en `idAsignado`. |
| Leer por ID | `ResArchivo leerVehiculo(int id, Vehiculo* v, long* posicion = nullptr)` | `ARCH_OK` / `ARCH_ID_NO_EXISTE` / `ARCH_ERROR`. Entrega activos y eliminados. |
| Sobrescribir | `ResArchivo sobrescribirVehiculo(Vehiculo* v)` | Escribe en su posicion y actualiza `fechaUltimaModificacion`. |
| Marcar eliminado | `ResArchivo marcarVehiculoEliminado(int id)` | Borrado logico + `registrosActivos--`. |
| Contar activos | `int contarVehiculosActivos()` | Lee el contador del header (`-1` si fallo). |

Se repite igual para `Mecanico`, `Repuesto`, `OrdenServicio` (`leerOrden`, `guardarOrden`...) y `DetalleOrden` (`leerDetalle`, `guardarDetalle`, `sobrescribirDetalle`, `marcarDetalleEliminado`).
`taller.bin` (un solo registro, sin header): `bool existeTaller()`, `bool guardarTaller(Taller*)`, `bool leerTaller(Taller*)`.
Tambien hay utilidades de archivos: `existeArchivo`, `crearCarpeta`, `tamanoDeArchivo`, `copiarArchivo` (copia byte a byte).

### Capa de logica

**Arranque**

| Firma | Retorno |
|---|---|
| `Resultado prepararArchivos(const char** archivoFallido)` | Crea `datos/` y los archivos que falten; si algo falla deja en `archivoFallido` el nombre del archivo con problemas. |
| `Resultado registrarTaller(Taller datos)` / `Resultado obtenerTaller(Taller* t)` | `RES_DATO_INVALIDO` / `RES_ERROR_ARCHIVO`. |

**Vehiculos**

| Firma | Justificacion del retorno |
|---|---|
| `Resultado registrarVehiculo(Vehiculo datos, int* idNuevo)` | `RES_DATO_INVALIDO` o `RES_DUPLICADO` (placa repetida entre activos; la de un vehiculo eliminado puede reutilizarse). |
| `Resultado obtenerVehiculoPorID(int id, Vehiculo* salida, long* posicion = nullptr)` | Distingue `RES_VEHICULO_NO_EXISTE` de `RES_VEHICULO_ELIMINADO`; el vehiculo se entrega en ambos casos. |
| `Resultado obtenerVehiculoPorPlaca(const char* placa, Vehiculo* salida)` | Recorre el archivo con un registro en memoria; solo activos. |
| `Resultado buscarVehiculos(texto, resultados[], int* cantidad, bool* hayMas, int saltar)` / `listarVehiculos(...)` | Propietario parcial sin distinguir mayusculas; maximo `MAX_RESULTADOS`; `hayMas` avisa del resto. |
| `Resultado actualizarVehiculo(int id, Vehiculo datos)` | No cambia placa, kilometraje ni ID. |
| `Resultado eliminarVehiculo(int id)` | `RES_VEHICULO_CON_ORDENES` si tiene ordenes en cualquier estado. |

**Mecanicos**

| Firma | Justificacion del retorno |
|---|---|
| `Resultado registrarMecanico(Mecanico datos, int* idNuevo)` | `RES_DUPLICADO` (cedula repetida) o `RES_DATO_INVALIDO` (especialidad, fecha...). |
| `Resultado obtenerMecanicoPorID(int id, Mecanico* salida, long* posicion = nullptr)` / `obtenerMecanicoPorCedula(...)` | `RES_OK` = activo, `RES_MECANICO_DE_BAJA` = dado de baja (igual se entrega, para mostrar su nombre en ordenes cerradas). |
| `Resultado listarMecanicos(resultados[], int* cantidad, bool* hayMas, int saltar)` | Activos y dados de baja; la presentacion los marca y muestra sus ordenes abiertas con `contarOrdenesAbiertasDeMecanico`. |
| `int contarOrdenesAbiertasDeMecanico(int idMecanico)` | El numero tiene significado propio (listado y limite de 3); `-1` = error de archivo. |
| `Resultado darDeBajaMecanico(int id)` | `RES_MECANICO_CON_ORDENES_ABIERTAS` si no se puede. |

**Repuestos**

| Firma | Justificacion del retorno |
|---|---|
| `Resultado registrarRepuesto(Repuesto datos, int* idNuevo)` | `RES_DUPLICADO` (codigo entre activos), `RES_PRECIO_INVALIDO`, `RES_CANTIDAD_INVALIDA`. |
| `Resultado obtenerRepuestoPorID(...)` / `obtenerRepuestoPorCodigo(...)` | Solo activos; `RES_REPUESTO_ELIMINADO` si corresponde. |
| `Resultado buscarRepuestos(texto, ...)` / `listarRepuestos(...)` | Codigo o nombre parcial; cantidad + `hayMas` + `saltar`. |
| `bool repuestoEnBajoStock(const Repuesto* r)` | `stock <= stockMinimo`; la presentacion pone la marca BAJO STOCK. |
| `Resultado registrarEntradaRepuesto(int idRepuesto, int cantidad)` | `RES_CANTIDAD_INVALIDA` si la cantidad es <= 0. |
| `Resultado cambiarPrecioRepuesto(int idRepuesto, float nuevoPrecio)` | `RES_PRECIO_INVALIDO`. No afecta ordenes ya registradas (cada linea guarda su precio). |
| `Resultado eliminarRepuesto(int idRepuesto)` | `RES_REPUESTO_EN_USO` si aparece en alguna linea activa de cualquier orden (incluidas las anuladas). |

**Ordenes**

| Firma | Justificacion del retorno |
|---|---|
| `Resultado abrirOrden(int idVehiculo, int idMecanico, int kmIngreso, const char* falla, int* idNuevo)` | Muchas causas: vehiculo o mecanico inexistente/eliminado/de baja, vehiculo con orden abierta, mecanico lleno, km menor. |
| `Resultado agregarRepuestoAOrden(int idOrden, int idRepuesto, int cantidad)` | Orden no abierta, stock insuficiente, `RES_LIMITE_LINEAS`, repuesto inexistente... |
| `Resultado quitarRepuestoDeOrden(int idOrden, int idRepuesto)` | Ademas distingue `RES_LINEA_NO_EXISTE`. |
| `Resultado cerrarOrden(int idOrden, float manoObra)` | `RES_ORDEN_NO_ABIERTA`, `RES_ORDEN_SIN_CONTENIDO`, mano de obra invalida. |
| `Resultado anularOrden(int idOrden)` | `RES_ORDEN_NO_ANULABLE` si ya esta ANULADA. |
| `Resultado calcularTotalesOrden(const OrdenServicio* o, float* totalRepuestos, float* totalOrden)` | El total **no se guarda**: se calcula leyendo las lineas. Retorna `Resultado` porque leer las lineas puede fallar. |
| `Resultado obtenerLineasDeOrden(int idOrden, DetalleOrden lineas[], int* cantidad)` | Lineas activas de la orden (maximo `MAX_LINEAS_ORDEN`). |
| `Resultado obtenerOrdenPorID(int id, OrdenServicio* salida, long* posicion = nullptr)` | `RES_ORDEN_NO_EXISTE`. |
| `Resultado listarOrdenesPorEstado(estado, ...)` / `listarOrdenesPorVehiculo(idVehiculo, ...)` | Con `estado == nullptr` entrega todas; `hayMas` + `saltar`. |
| `int contarOrdenesDeVehiculo`, `buscarOrdenAbiertaDeVehiculo`, `contarLineasActivas`, `buscarLineaActiva`, `repuestoEstaEnUso` | Consultas auxiliares para las reglas de negocio (`-1` = error de archivo). |

**Reportes y mantenimiento (logica)**

| Firma | Descripcion |
|---|---|
| `Resultado calcularFacturacionVehiculo(int idVehiculo, float* total)` | Total facturado en ordenes CERRADAS de un vehiculo. |
| `Resultado calcularFacturacionMecanico(int idMecanico, int* cerradas, float* manoObra, float* repuestos)` | Ordenes cerradas, mano de obra y repuestos de un mecanico (activo o de baja). |
| `Resultado calcularRepuestosMasUsados(UsoRepuesto top[], int* cantidad)` | Top `TOP_REPUESTOS = 10` en ordenes CERRADAS. Usa un contador numerico por repuesto (no carga registros). |
| `Resultado verificarIntegridad(ReporteIntegridad* rep)` | La logica no imprime: llena un reporte (contadores + mensajes `ROTO: ...`) y la presentacion lo dibuja. |
| `Resultado crearBackup(char* rutaDestino, int tam)` | Crea `backup_AAAA-MM-DD_HH-MM` (con segundos si ya hay uno en ese minuto), copia byte a byte los 6 `.bin` y deja la ruta en `rutaDestino`. |

### Decisiones que el enunciado dejaba abiertas
- Si se agrega un repuesto que ya esta en la orden, la linea **conserva el precio con el que se creo** y solo aumenta la cantidad.
- Al anular una orden, sus lineas **se conservan** como historial (se devuelve el stock, pero el repuesto sigue contando como "usado" y no se puede eliminar).
- Un mecanico dado de baja no recibe ordenes nuevas, pero sus ordenes CERRADAS y ANULADAS siguen mostrando su nombre (marcado BAJA).
- Las busquedas y listados entregan como maximo 20 resultados por pagina y avisan si hay mas.

## 7. Orden de escritura en operaciones con varios archivos

Todas las validaciones se hacen **antes** de la primera escritura. Aun asi el programa puede interrumpirse entre dos escrituras.

| Operacion | Orden de escritura | Si se interrumpe entre las dos |
|---|---|---|
| Guardar un registro nuevo (cualquier archivo) | 1) el registro al final, 2) el header | Queda un registro "huerfano": el header indica menos registros de los que hay. **La verificacion de integridad lo detecta.** |
| Abrir orden | 1) la orden, 2) el kilometraje del vehiculo | Existe la orden pero el vehiculo conserva su km anterior (dano pequeno y visible). |
| Agregar repuesto | 1) la **linea** (`detalles.bin`), 2) el **stock** (`repuestos.bin`) | Hay una linea en la orden pero el stock no se desconto: el inventario muestra **mas** unidades de las reales. |
| Quitar repuesto | 1) borrado logico de la linea, 2) devolver el stock | La linea desaparecio pero el stock no se devolvio: faltan unidades en el inventario. |
| Anular orden | 1) estado ANULADA, 2) stock de cada linea | Orden anulada con parte (o nada) del stock devuelto. No se puede anular dos veces. Antes de escribir se valida que cada repuesto exista y pueda recibir las unidades. |
| Cerrar orden | solo `ordenes.bin` | Una sola escritura: no hay riesgo. |

## 8. Preguntas de analisis

**1. Escribir con `write` una `OrdenServicio` del Proyecto 1 (con su `DetalleOrden*`), cerrar el programa, volver a ejecutarlo y leer ese registro. Que se observo al acceder a sus lineas? Por que?**

Se hizo el experimento con un programa que escribe la orden y otro que la lee. Al leer, el campo `numDetalles` y los demas campos estaban bien, y el puntero `detalles` mostraba una direccion,
pero al acceder a `detalles[0]` el programa se cerro con **Segmentation fault** (codigo de salida 139).
Pasa porque `write` copia los **bytes de la estructura**, y un puntero es solo una direccion de memoria (un numero). Se guardo la direccion, no las lineas que estaban en el heap.
Al volver a ejecutar, el sistema operativo asigna otra memoria y esa direccion ya no es valida (o apunta a otra cosa). Por eso una estructura que va a disco no puede tener punteros:
todo debe estar dentro de la estructura con tamano fijo, o en otro archivo, como hacemos con `detalles.bin`.

**2. Al agregar un repuesto se escriben el detalle y el stock. Si el programa se interrumpe entre esas dos escrituras, que inconsistencia queda segun el orden elegido? Que orden deja el dano mas facil de detectar o corregir?**

Elegimos escribir **primero la linea y despues el stock**. Si se interrumpe en medio, la orden ya tiene el repuesto (y el cliente lo paga) pero el stock no se desconto: el inventario muestra **mas** unidades de las que hay.
El dano queda **a la vista**: la linea existe en la orden, con su cantidad y precio, y se puede ver al mostrar la orden. Se corrige comparando con el inventario fisico y ajustando el stock.
Con el orden contrario (primero el stock, despues la linea), una interrupcion deja unidades descontadas **sin ninguna linea que las explique**: el dano es invisible y no hay donde buscar a donde fueron esas unidades.
Hay que ser honestos con la contrapartida: con nuestro orden el inventario puede quedar con unidades de mas, y se podria prometer una unidad que no existe. Ademas, `verificarIntegridad` **no detecta** ninguno de los dos casos
(el stock inicial y las entradas no quedan como historial, asi que no se puede recalcular el stock a partir de las lineas). Elegimos dejar el rastro en la orden antes que perderlo.

**3. En el Proyecto 1 no se podia eliminar un mecanico con ordenes. Que habria pasado con el historial si se hubiera permitido la eliminacion fisica? Por que la baja logica lo resuelve?**

Las ordenes guardan solo el `idMecanico`. Si el mecanico se borrara fisicamente (y se compactara el archivo), esas ordenes quedarian apuntando a un ID que no existe
(o, peor, a otro mecanico que ocupo su lugar): se perderia quien hizo el trabajo, el reporte de facturacion por mecanico quedaria incompleto y `verificarIntegridad` marcaria ordenes rotas.
Ademas se romperia la relacion `indice = ID - 1`. Con la baja logica el registro sigue en el archivo con `eliminado = true`:
el ID sigue siendo valido, las ordenes CERRADAS y ANULADAS siguen mostrando su nombre (marcado BAJA), y solo se le impide recibir ordenes nuevas.

## 9. Mantenimiento y pruebas

- **Verificacion de integridad** (menu Mantenimiento, `verificarIntegridad`): recorre los 5 archivos de entidades y `taller.bin` leyendo un registro a la vez y detecta como minimo:
  (a) ordenes con vehiculo o mecanico inexistente, (b) ordenes ABIERTAS con mecanico dado de baja (en CERRADAS y ANULADAS un mecanico de baja es valido) o vehiculo eliminado,
  (c) lineas de detalle con repuesto inexistente, (d) stock negativo y (e) headers que no coinciden con el contenido real (`cantidadRegistros` o `registrosActivos`).
  Tambien reporta: `proximoID` incoherente, bytes sobrantes en un archivo, IDs que no coinciden con su posicion, estado de orden desconocido, km de ingreso mayor al del vehiculo,
  lineas que apuntan a una orden inexistente, lineas con cantidad <= 0 y un `taller.bin` de tamano incorrecto.
  Cada problema se reporta con los datos que permiten ubicarlo (`ROTO: Orden #7 apunta al vehiculo ID 7 (no existe en vehiculos.bin)`) y al final se muestra un resumen.
  Se demuestran asi: el caso (a) en `7b` y los casos (b), (c), (d) y (e) en `9d` (con datos corrompidos a proposito).
- **Respaldo** (`crearBackup`): pide confirmacion, crea `backup_AAAA-MM-DD_HH-MM/` y copia byte a byte los 6 `.bin` (`taller`, `vehiculos`, `mecanicos`, `repuestos`, `ordenes`, `detalles`). `detalles.bin` se incluye en el respaldo y en la verificacion.
- **Reportes** (se calculan leyendo los archivos, un registro a la vez; solo cuentan las ordenes CERRADAS): historial de un vehiculo, repuestos mas utilizados y facturacion por mecanico.
- **`pruebas/`**: `entrada_X.txt` / `salida_X.txt` por cada ejecucion del programa, con el estilo del Proyecto 1: solo lo que se escribe, mas lineas `#` (encabezado y comentario de cada paso) que el programa repite en la salida.

| Caso de uso | Archivos |
|---|---|
| 1 Persistencia entre ejecuciones | `1a`, `1b` |
| 2 Acceso directo por ID | `2` |
| 3 La orden sobrevive con su detalle (y precio historico) | `3a`, `3b`, `3c` |
| 4 Orden con 15 lineas | `4a`, `4b` |
| 5 Anulacion con reversion en disco | `5a`, `5b` |
| 6 Baja de mecanico e historial (y limite de 3 ordenes abiertas) | `6` |
| 7 Integridad tras restauracion parcial | `7a` (respaldo + orden nueva), `7b` (tras reemplazar `vehiculos.bin` por la copia) |
| 8 Reportes | `8` |
| Validaciones y manejo de errores (seccion 10) | `9a`, `9b`, `9c`, `9d` |

- El programa se compilo con `-Wall -Wextra` sin advertencias y se ejecuto toda la secuencia de pruebas con `-fsanitize=address,undefined` sin errores ni fugas.

## 10. Validaciones y manejo de errores

Las validaciones se hacen en **dos lugares**: la presentacion valida el formato de lo que escribe el usuario y vuelve a pedir el dato (`pedirDato`, `pedirEntero`, `pedirDecimal`, `pedirOpcion`),
y la logica **vuelve a validar** todo y aplica las reglas de negocio, sin confiar en lo que recibe. Los fallos de la logica llegan a la presentacion como un `Resultado` que se traduce a un mensaje `ERROR: ...`.

**Situaciones nuevas del Proyecto 2 (seccion 11 del enunciado):**

| Situacion | Como se maneja | Prueba |
|---|---|---|
| Archivo inexistente al iniciar | `inicializarArchivo` lo crea con su header inicial y el programa sigue | `9b` |
| Fallo al abrir o leer un archivo | `prepararArchivos` devuelve el nombre del archivo y el programa avisa `ERROR: No se pudo preparar el archivo 'datos/mecanicos.bin'...` y termina con codigo 1 (un `.bin` con el header incompleto cuenta como fallo) | `9c` |
| ID inexistente o eliminado | La logica retorna una causa distinta para cada caso; la presentacion dice `fue eliminado (placa ...)` o `No existe un vehiculo con ID ...` | `2`, `9a` |
| Orden en estado incorrecto | El mensaje indica el estado actual: `La orden #1 esta CERRADA y no puede modificarse.` | `5b`, `9a` |
| Limite de lineas alcanzado | `RES_LIMITE_LINEAS`: `La orden #11 ya tiene el maximo de 20 lineas de detalle. La linea no se agrego.` | `9a` |
| Opcion de menu sobre datos que todavia no existen | `verificarHay` explica por que no se puede continuar | `9b` |
| Listado con mas de 20 resultados | Avisa `HAY MAS resultados` y pregunta si se muestran los siguientes | `9a` |

**Verificacion de integridad:** `9d` corrompe a proposito una copia de `datos/` (header de repuestos, stock negativo, orden ABIERTA con mecanico de baja y linea con repuesto inexistente)
y comprueba que se reporten los problemas (5: el header danado produce dos avisos, `cantidadRegistros` y `proximoID`).

**Validaciones del Proyecto 1 que se mantienen** (probadas en `9a`):

| Dato o regla | Validacion |
|---|---|
| Opcion de menu | Solo numeros dentro del rango; texto, vacio o fuera de rango se rechazan |
| Placa | 6 o 7 caracteres alfanumericos; unica entre vehiculos activos (la de un vehiculo eliminado puede reutilizarse) |
| Anio / kilometraje | Anio entre 1950 y 2027; kilometraje >= 0; si se escribe texto se vuelve a pedir |
| Cedula | Solo digitos, con prefijo opcional V-, E- o J-; la de un mecanico es unica |
| Especialidad / fecha | Solo MOTOR, FRENOS, ELECTRICIDAD, SUSPENSION o GENERAL; fecha `YYYY-MM-DD` real (rechaza `2025-02-30`) |
| Repuesto | Codigo unico entre activos; precio > 0; stock y stock minimo >= 0; entrada y cantidades > 0 |
| Abrir orden | Vehiculo y mecanico existentes y activos; sin otra orden ABIERTA para el vehiculo; maximo 3 ordenes ABIERTAS por mecanico; kilometraje no menor al registrado |
| Agregar repuesto | Orden ABIERTA; stock suficiente; repuesto existente; maximo 20 lineas |
| Cerrar orden | Orden ABIERTA; mano de obra >= 0; debe tener al menos un repuesto o mano de obra > 0 |
| Eliminar / dar de baja | Vehiculo con ordenes, repuesto usado en alguna orden y mecanico con ordenes ABIERTAS se rechazan |
| `CANCELAR` | En cualquier dato de un registro aborta la operacion sin crear nada |

## 11. Casos de uso obligatorios: que se verifica y donde verlo

Todos se ejecutan desde datos vacios con `bash pruebas/ejecutar_pruebas.sh ./taller`. Cada `entrada_X.txt` es **una ejecucion del programa**
(cuando el caso pide reiniciar hay una entrada por ejecucion) y su resultado esta en `salida_X.txt`.

| Caso | Archivos | Que buscar en la salida |
|---|---|---|
| **1** Persistencia entre ejecuciones | `1a` registra 3 vehiculos, 2 mecanicos y 4 repuestos; `1b` es una ejecucion nueva | En `salida_1b` siguen los 3 vehiculos, 2 mecanicos y 4 repuestos con los mismos IDs (BUJ-005 con BAJO STOCK) |
| **2** Acceso directo por ID | `2` | `[DEPURACION] Registro leido directamente en la posicion 1168 bytes (16 + (5 - 1) * 288)` y, para el ID 3, `fue eliminado` (no "no existe") |
| **3** La orden sobrevive con su detalle | `3a` (abre orden y agrega 3 repuestos), `3b` (reinicio: 4to repuesto, +2 unidades acumuladas, quita uno y cierra), `3c` (reinicio: muestra la orden) | En `3b` el stock de FRN-014 vuelve a 5; en `3c` la orden #1 muestra sus lineas, precios historicos y `TOTAL 157.00` (4x25 + 1x8 + 2x4.50 + 40). Cambiar el precio a 30.00 no altera el total |
| **4** Orden con 15 lineas | `4a` (agrega 15 repuestos distintos), `4b` (reinicio) | `salida_4b`: `(Lineas de detalle: 15 \| maximo por orden: 20)` con las 15 lineas |
| **5** Anulacion con reversion en disco | `5a` (anula la orden #2 CERRADA con 15 lineas), `5b` (reinicio) | El stock de cada repuesto sube 1 entre `5a` y `5b` (FRN-001 5 -> 6, REP-006 19 -> 20); anular otra vez muestra `esta ANULADA` |
| **6** Baja de mecanico e historial | `6` | Baja rechazada con orden ABIERTA; se cierra la orden y la baja se acepta; sus ordenes CERRADAS muestran `Jose Perez (BAJA)`; orden nueva con el rechazada. Tambien prueba el limite de 3 ordenes abiertas |
| **7** Integridad tras restauracion parcial | `7a` (respaldo + vehiculo nuevo + orden), `7b` (tras reemplazar `vehiculos.bin` por la copia) | `salida_7b`: `ROTO: Orden #7 apunta al vehiculo ID 7 (no existe en vehiculos.bin)` y `Problemas encontrados : 1` |
| **8** Reportes | `8` | 8 ordenes (5 cerradas, 2 anuladas y 1 abierta) entre 3 mecanicos; los tres reportes no suman las ANULADAS ni la ABIERTA (por ejemplo Jose: repuestos 117.00 = solo la orden #1; Luis: 1 orden cerrada). Termina con `SISTEMA INTEGRO` |

El paso 3 del caso 7 (reemplazar `datos/vehiculos.bin` por la copia del respaldo) es **manual** en el enunciado; `ejecutar_pruebas.sh` lo hace con `cp` entre `7a` y `7b`
y despues recupera los datos completos para que el caso 8 siga con la informacion intacta.

## 12. Contenido de la entrega

```
proyecto2_detalles/
 |- proyecto_II.cpp                Codigo fuente (un solo archivo: persistencia, logica y presentacion)
 |- README_V2.md                   Este documento
 |- datos/                         Archivos .bin PRE-POBLADOS
 |   |- taller.bin
 |   |- vehiculos.bin              8 vehiculos
 |   |- mecanicos.bin              4 mecanicos (1 dado de baja)
 |   |- repuestos.bin              12 repuestos
 |   |- ordenes.bin                10 ordenes: 6 CERRADAS, 2 ANULADAS, 2 ABIERTAS
 |   `- detalles.bin               24 lineas de detalle (3 ordenes con 4 o mas lineas: 4, 5 y 4)
 `- pruebas/                       Entradas y salidas, una pareja por ejecucion del programa
     |- LEEME.md                   Indice de esta carpeta
     |- entrada_0_poblar.txt / salida_0_poblar.txt   Genera la carpeta datos/ pre-poblada
     |- entrada_1a, 1b                Caso de uso 1
     |- entrada_2                     Caso de uso 2
     |- entrada_3a, 3b, 3c            Caso de uso 3
     |- entrada_4a, 4b                Caso de uso 4
     |- entrada_5a, 5b                Caso de uso 5
     |- entrada_6                     Caso de uso 6
     |- entrada_7a, 7b                Caso de uso 7
     |- entrada_8                     Caso de uso 8
     |- entrada_9a, 9b, 9c, 9d        Validaciones y manejo de errores
     |   (cada entrada_X.txt tiene su salida_X.txt)
     |- ejecutar_pruebas.sh           Corre los casos 1 a 8 desde datos vacios
     `- ejecutar_validaciones.sh      Corre 9a a 9d sobre una copia de datos/
```

**Lista de verificacion contra el enunciado (seccion 14):**

| Pide el enunciado | Donde esta |
|---|---|
| Codigo fuente (`.cpp`, `.h`) | `proyecto_II.cpp` |
| `README_V2.md` segun la seccion 13 | Secciones 2 (compilacion), 4 (sizeof, formula, ID e indice), 5 (detalle de la orden), 6 (firmas), 7 (orden de escritura) y 8 (preguntas de analisis) |
| Carpeta `datos/` pre-poblada (8 vehiculos, 4 mecanicos con uno de baja, 12 repuestos, 10 ordenes en los 3 estados, 3 con 4 o mas lineas) | `datos/` |
| Carpeta `pruebas/` con entradas y salidas de los casos de uso; archivos numerados por ejecucion cuando hay reinicios | `pruebas/` (casos 1 a 8, mas validaciones `9a` a `9d`) |
