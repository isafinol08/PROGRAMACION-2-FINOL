# Sistema de Gestión de Taller Mecánico

**Programación 2 · Proyecto 1 · 2026-C**
**Integrantes:** _[Nombre 1]_ y _[Nombre 2]_  (completar)

## 1. Descripción del sistema

Programa de consola en C++ para administrar un taller mecánico: **vehículos**, **mecánicos**, **inventario de repuestos** y **órdenes de servicio**. Todo se guarda en memoria dinámica (`new` / `delete`), sin `vector` ni otros contenedores de la STL. `std::string` se usa solamente para leer temporalmente lo que escribe el usuario.

Todo el código está en un único archivo, `taller.cpp` (no hay archivos `.h`).

## 2. Compilación y ejecución

```bash
g++ -std=c++11 -Wall -Wextra -o taller taller.cpp
./taller                                              # modo interactivo
./taller < pruebas/entrada.txt > pruebas/salida.txt   # ejecuta los 8 casos de uso
```

Notas:
- En Windows con MinGW el comando es el mismo (`taller.exe`). Toda la salida del programa es ASCII puro (sin acentos ni caracteres especiales), por lo que se ve bien en cualquier consola.
- Cuando la entrada viene de un archivo, el programa **repite en pantalla** cada línea leída para que `salida.txt` se pueda leer completo, y las líneas de `entrada.txt` que empiezan con `#` se tratan como comentarios. En modo interactivo esto no ocurre.
- En modo interactivo la pantalla se limpia al elegir una opcion y, al terminar la accion, el programa espera un ENTER para que se pueda leer el resultado. Con la entrada redirigida desde archivo no se limpia ni se pausa (por eso `salida.txt` sale completo y no depende de ninguna tecla).
- En cualquier dato de un registro se puede escribir `CANCELAR` para abortarlo sin crear nada.

## 3. Estructuras y sus relaciones

```
SistemaTaller
 ├─ Taller taller                (datos del taller, único)
 ├─ Vehiculo*      vehiculos     [numVehiculos / capacidadVehiculos]
 ├─ Mecanico*      mecanicos     [numMecanicos / capacidadMecanicos]
 ├─ Repuesto*      repuestos     [numRepuestos / capacidadRepuestos]
 ├─ OrdenServicio* ordenes       [numOrdenes   / capacidadOrdenes]
 │      └─ cada OrdenServicio tiene SU PROPIO arreglo:
 │            DetalleOrden* detalles [numDetalles / capacidadDetalles]
 └─ siguienteIdVehiculo / Mecanico / Repuesto / Orden   (IDs autoincrementales)
```

Las entidades se relacionan **por ID**, no por puntero:

```
OrdenServicio.idVehiculo ──► Vehiculo.id
OrdenServicio.idMecanico ──► Mecanico.id
DetalleOrden.idRepuesto  ──► Repuesto.id
```

Se usan IDs porque un puntero a un elemento de un arreglo dinámico queda inválido cuando el arreglo se redimensiona o se desplazan elementos al eliminar; un ID siempre se puede volver a buscar.

Memoria: todos los arreglos empiezan con capacidad 4 (los detalles de cada orden con capacidad 2) y duplican su capacidad cuando `num == capacidad`. `liberarSistema()` libera primero el arreglo `detalles` de cada orden y luego los cuatro arreglos principales.

## 4. Separación de capas

El archivo está dividido en dos zonas:

- **Capa de lógica** (sección 2 del `.cpp`): funciones que operan sobre `SistemaTaller*` y retornan `Entidad*`, `Entidad**`, `bool` o un código `Resultado`. **No usan `cin` ni `cout`.** Aquí viven las reglas de negocio (placa única, máximo 3 órdenes abiertas por mecánico, stock suficiente, etc.).
- **Capa de presentación** (sección 3): menús, lectura de datos, validación del formato de lo que escribe el usuario y dibujo de tablas. **No modifica datos directamente**: siempre llama a una función de la lógica. Si la lógica devuelve un fallo, la presentación lo traduce al mensaje `ERROR: ...`.

Dentro de la lógica hay funciones de validación puras (`esPlacaValida`, `esCedulaValida`, `esFechaValida`, ...). La presentación las usa para volver a pedir un dato mal escrito, y la lógica las vuelve a usar para no confiar nunca en lo que recibe.

## 5. Firmas diseñadas

Patrón usado:
- Funciones que **crean** una entidad: retornan el puntero a la entidad creada, o `nullptr`, y dejan el motivo del fallo en `Resultado* motivo`.
- Funciones que **ejecutan una operación** sobre algo existente: retornan un `Resultado` (enum), no un `bool`.
- Funciones que **listan o buscan varios**: retornan `Entidad**` (el llamador libera con `delete[]`) y escriben la cantidad en `*cantidad`.

```cpp
enum Resultado { RES_OK, RES_DATO_INVALIDO, RES_DUPLICADO, RES_VEHICULO_NO_EXISTE,
  RES_MECANICO_NO_EXISTE, RES_REPUESTO_NO_EXISTE, RES_ORDEN_NO_EXISTE, RES_CANTIDAD_INVALIDA,
  RES_PRECIO_INVALIDO, RES_VEHICULO_CON_ORDEN_ABIERTA, RES_MECANICO_LLENO, RES_KM_MENOR,
  RES_ORDEN_NO_ABIERTA, RES_ORDEN_NO_ANULABLE, RES_STOCK_INSUFICIENTE, RES_LINEA_NO_EXISTE,
  RES_ORDEN_SIN_CONTENIDO, RES_REPUESTO_EN_USO };
```

### Mecánicos
| Firma | Justificación del retorno |
|---|---|
| `Mecanico* agregarMecanico(SistemaTaller*, Mecanico datos, Resultado* motivo)` | Retorna el mecánico creado (igual que `agregarVehiculo`); si falla, `nullptr` y el motivo distingue cédula duplicada de dato inválido. |
| `Mecanico* buscarMecanicoPorID(SistemaTaller*, int id)` | Puntero o `nullptr`: no hay más de un motivo de fallo. |
| `Mecanico* buscarMecanicoPorCedula(SistemaTaller*, const char* cedula)` | Igual; se usa para validar la cédula única. |
| `Mecanico** listarMecanicos(SistemaTaller*, int* cantidad)` | Arreglo de punteros que libera el llamador. |
| `int contarOrdenesAbiertasDeMecanico(SistemaTaller*, int idMecanico)` | El número tiene significado propio (se muestra en el listado y se compara con el límite de 3). |

### Repuestos
| Firma | Justificación del retorno |
|---|---|
| `Repuesto* agregarRepuesto(SistemaTaller*, Repuesto datos, Resultado* motivo)` | Retorna el repuesto creado; el motivo distingue código duplicado de dato inválido. |
| `Repuesto* buscarRepuestoPorID / buscarRepuestoPorCodigo` | Puntero o `nullptr`. |
| `Repuesto** buscarRepuestosPorTexto(SistemaTaller*, const char* texto, int* cantidad)` | Varias coincidencias posibles (código o nombre parcial). |
| `Resultado registrarEntradaRepuesto(SistemaTaller*, int idRepuesto, int cantidad)` | No crea nada: informa si salió bien, si el repuesto no existe o si la cantidad es inválida. |
| `Repuesto** listarRepuestos(SistemaTaller*, int* cantidad)` + `bool repuestoEnBajoStock(const Repuesto*)` | El listado entrega los punteros; la marca BAJO STOCK se decide con una función aparte que la presentación consulta. |
| `Resultado cambiarPrecioRepuesto(SistemaTaller*, int idRepuesto, float nuevoPrecio)` | Distingue "no existe" de "precio inválido". |
| `Resultado eliminarRepuesto(SistemaTaller*, int idRepuesto)` | Distingue "no existe" de "está en uso" (`RES_REPUESTO_EN_USO`), que es justo lo que un `bool` no permite. |

### Órdenes
| Firma | Justificación del retorno |
|---|---|
| `OrdenServicio* abrirOrden(SistemaTaller*, int idVehiculo, int idMecanico, int kmIngreso, const char* falla, Resultado* motivo)` | Retorna la orden creada; hay muchos motivos de fallo (vehículo con orden abierta, mecánico lleno, km menor, ...), por eso el `Resultado`. |
| `Resultado agregarRepuestoAOrden(SistemaTaller*, int idOrden, int idRepuesto, int cantidad)` | Modifica una orden existente; puede fallar por orden no abierta, stock insuficiente, cantidad inválida... |
| `Resultado quitarRepuestoDeOrden(SistemaTaller*, int idOrden, int idRepuesto)` | Igual; además distingue "ese repuesto no está en la orden". |
| `Resultado cerrarOrden(SistemaTaller*, int idOrden, float manoObra)` | Distingue orden no abierta, mano de obra inválida y orden sin repuestos ni mano de obra. |
| `Resultado anularOrden(SistemaTaller*, int idOrden)` | Distingue "no existe" de "ya está anulada". |
| `float calcularTotalRepuestos(const OrdenServicio*)` y `float calcularTotalOrden(const OrdenServicio*)` | El total **no se guarda**: se calcula siempre desde las líneas (`manoObra + Σ cantidad × precioUnitario`). |
| `OrdenServicio* buscarOrdenPorID(SistemaTaller*, int id)` | Puntero o `nullptr`. |
| `OrdenServicio** listarOrdenesPorEstado(SistemaTaller*, const char* estado, int* cantidad)` | Con `estado == nullptr` retorna todas. |
| `OrdenServicio** listarOrdenesPorVehiculo(SistemaTaller*, int idVehiculo, int* cantidad)` | Historial de un vehículo. |
| `OrdenServicio* buscarOrdenAbiertaDeVehiculo(...)`, `int contarOrdenesDeVehiculo(...)`, `DetalleOrden* buscarDetalleEnOrden(...)` | Consultas auxiliares que usan los demás módulos y la presentación para armar mensajes de error precisos. |

Decisiones que el enunciado dejaba abiertas:
- Si se agrega a una orden un repuesto que ya está, la línea **conserva el precio con el que se creó** y solo aumenta la cantidad.
- Al anular una orden, sus líneas **se conservan** como historial (se devuelve el stock, pero el repuesto sigue contando como "usado" y no se puede eliminar).
- Las órdenes guardan `idVehiculo`/`idMecanico`; por eso eliminar un vehículo desplaza elementos sin dañar ninguna orden (y de todas formas no se permite si tiene órdenes).

## 6. Preguntas de análisis

> Estas respuestas son una base: cada integrante debe poder explicarlas con sus propias palabras en la revisión.

**1. `agregarVehiculo` retorna un puntero a un elemento de `s->vehiculos`. Si lo guardo, registro varios vehículos más y luego lo uso, ¿qué puede ocurrir y por qué?**

El puntero apunta a una posición dentro del arreglo dinámico. Cuando el arreglo se llena, `redimensionarVehiculos` crea uno nuevo más grande, copia los datos y hace `delete[]` del arreglo viejo. El puntero guardado sigue apuntando al bloque viejo, que ya fue liberado: es un puntero colgante. Usarlo es comportamiento indefinido: puede mostrar basura, datos viejos o cerrar el programa. Algo parecido pasa si se elimina un vehículo anterior, porque los siguientes se desplazan y el puntero queda apuntando a otro vehículo. Por eso el código usa el puntero enseguida y, para guardar una referencia duradera, guarda el **ID** y vuelve a buscar.

**2. ¿Por qué `DetalleOrden` guarda `precioUnitario` en lugar de consultar el precio del repuesto cada vez que se calcula el total?**

Porque el precio de un repuesto cambia con el tiempo, pero lo que se le cobró al cliente en una orden ya hecha no debe cambiar. Si se consultara el precio vigente, al subir el precio del repuesto A el total de una orden cerrada también cambiaría, y la contabilidad dejaría de cuadrar. Guardar el precio en la línea lo congela en el momento en que se agregó (Caso de Uso 4: la orden #1 sigue en 115.00 aunque A pasó de 25.00 a 30.00).

**3. Cuando se redimensiona el arreglo de órdenes, ¿qué ocurre con el arreglo de detalles de cada orden?**

Se **copia el puntero**, no el arreglo. En `redimensionarOrdenes`, la línea `nuevo[i] = s->ordenes[i];` copia todos los campos de la orden, incluido `detalles`, así que la orden nueva apunta exactamente al mismo bloque de memoria de detalles que tenía la vieja. Después, `delete[] s->ordenes` libera solo el arreglo viejo de órdenes: como `OrdenServicio` no tiene destructor, no toca los bloques de detalles. Resultado: los detalles no se pierden, no se duplican y hay un único dueño (la orden nueva). Es el motivo por el que `liberarSistema` debe hacer `delete[] ordenes[i].detalles` para cada orden antes de liberar el arreglo principal.

## 7. Pruebas

La carpeta `pruebas/` contiene:
- `entrada.txt`: secuencia de entradas que recorre los 8 casos de uso (con comentarios `#` que indican cada caso y la preparación necesaria, como registrar mecánicos).
- `salida.txt`: resultado de `./taller < pruebas/entrada.txt > pruebas/salida.txt`.

El programa también se compiló con `-fsanitize=address,undefined` y se ejecutó con la misma entrada sin errores ni fugas de memoria.
