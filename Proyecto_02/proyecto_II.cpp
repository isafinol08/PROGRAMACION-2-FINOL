#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <climits>
#include <ctime>
#include <string>   // SOLO se usa para leer texto temporalmente del teclado

#ifdef _WIN32
  #include <io.h>
  #include <direct.h>
  #define ES_TERMINAL() _isatty(0)
  #define CREAR_CARPETA(ruta) _mkdir(ruta)
#else
  #include <unistd.h>
  #include <sys/stat.h>
  #define ES_TERMINAL() isatty(0)
  #define CREAR_CARPETA(ruta) mkdir(ruta, 0755)
#endif

using namespace std;

// =============
//  CONSTANTES
// =============
const char* CARPETA_DATOS    = "datos";
const char* RUTA_TALLER      = "datos/taller.bin";
const char* RUTA_VEHICULOS   = "datos/vehiculos.bin";
const char* RUTA_MECANICOS   = "datos/mecanicos.bin";
const char* RUTA_REPUESTOS   = "datos/repuestos.bin";
const char* RUTA_ORDENES     = "datos/ordenes.bin";

const int ANIO_MINIMO        = 1950;
const int ANIO_MAXIMO        = 2027;
const int MAX_ORDENES_ABIERTAS = 3;    // por mecanico
const int MAX_DETALLES       = 15;     // lineas de detalle por orden
const int MAX_RESULTADOS     = 50;     // maximo de resultados de una busqueda o listado
const int MAX_PROBLEMAS      = 100;    // maximo de mensajes en el reporte de integridad

// ===========================================================
//  1. ESTRUCTURAS (tamanio fijo: sin punteros ni std::string)
// ===========================================================
struct ArchivoHeader {
    int cantidadRegistros;    // total historico (incluye eliminados)
    int proximoID;            // siguiente ID a asignar
    int registrosActivos;     // registros con eliminado == false
    int version;              // version del formato (1)
};

struct Taller {
    char nombre[100];
    char rif[20];
    char direccion[200];
    char telefono[20];
    time_t fechaCreacion;
    time_t fechaUltimaModificacion;
};

struct Vehiculo {
    int  id;
    char placa[10];
    char marca[50];
    char modelo[50];
    int  anio;
    int  kilometraje;
    char nombrePropietario[100];
    char cedulaPropietario[20];
    char telefonoPropietario[20];
    bool eliminado;
    time_t fechaCreacion;
    time_t fechaUltimaModificacion;
};

struct Mecanico {
    int  id;
    char nombre[100];
    char cedula[20];
    char especialidad[20];     // MOTOR, FRENOS, ELECTRICIDAD, SUSPENSION, GENERAL
    char fechaIngreso[11];     // YYYY-MM-DD
    bool eliminado;            // true = dado de baja
    time_t fechaCreacion;
    time_t fechaUltimaModificacion;
};

struct Repuesto {
    int   id;
    char  codigo[20];
    char  nombre[100];
    char  marca[50];
    float precio;
    int   stock;
    int   stockMinimo;
    bool  eliminado;
    time_t fechaCreacion;
    time_t fechaUltimaModificacion;
};

// Una linea de la orden: que repuesto, cuantos y a que precio se agrego
struct DetalleOrden {
    int   idRepuesto;
    int   cantidad;
    float precioUnitario;      // precio historico
};

// DISENO DEL DETALLE: cada orden lleva DENTRO un arreglo fijo de MAX_DETALLES lineas.
// Asi todas las ordenes pesan lo mismo y las lineas viajan junto con la orden.
struct OrdenServicio {
    int  id;
    int  idVehiculo;
    int  idMecanico;
    char fechaApertura[11];
    char fechaCierre[11];      // vacia mientras este ABIERTA
    char estado[12];           // "ABIERTA", "CERRADA" o "ANULADA"
    char descripcionFalla[200];
    int  kilometrajeIngreso;
    float manoObra;
    int  numDetalles;                      // lineas usadas (0..MAX_DETALLES)
    DetalleOrden detalles[MAX_DETALLES];   // lineas de detalle
    bool eliminado;                        // las ordenes no se eliminan: siempre false
    time_t fechaCreacion;
    time_t fechaUltimaModificacion;
};

// Codigos de resultado: la logica los devuelve y la presentacion los traduce a mensajes
enum Resultado {
    RES_OK,
    RES_ERROR_ARCHIVO,          // no se pudo abrir/leer/escribir un archivo
    RES_NO_EXISTE,              // el ID o dato buscado no existe
    RES_ELIMINADO,              // existe pero esta eliminado (o dado de baja)
    RES_DATO_INVALIDO,
    RES_DUPLICADO,
    RES_CANTIDAD_INVALIDA,
    RES_PRECIO_INVALIDO,
    RES_VEHICULO_NO_EXISTE,
    RES_VEHICULO_ELIMINADO,
    RES_MECANICO_NO_EXISTE,
    RES_MECANICO_DE_BAJA,
    RES_REPUESTO_NO_EXISTE,
    RES_ORDEN_NO_EXISTE,
    RES_VEHICULO_CON_ORDEN_ABIERTA,
    RES_MECANICO_LLENO,
    RES_KM_MENOR,
    RES_ORDEN_NO_ABIERTA,
    RES_ORDEN_NO_ANULABLE,
    RES_STOCK_INSUFICIENTE,
    RES_LINEA_NO_EXISTE,
    RES_LIMITE_LINEAS,
    RES_ORDEN_SIN_CONTENIDO,
    RES_REPUESTO_EN_USO,
    RES_VEHICULO_CON_ORDENES,
    RES_MECANICO_CON_ORDENES_ABIERTAS
};

// Resultado de la verificacion de integridad (la logica lo llena, la presentacion lo muestra)
struct ReporteIntegridad {
    int vehiculosVerificados;
    int mecanicosVerificados;
    int repuestosVerificados;
    int ordenesVerificadas;
    int problemas;                         // total de problemas encontrados
    int numMensajes;                       // mensajes guardados (maximo MAX_PROBLEMAS)
    char mensajes[MAX_PROBLEMAS][150];
};

struct UsoRepuesto {                       // fila del reporte "repuestos mas utilizados"
    int idRepuesto;
    int unidades;
};

struct FilaFacturacion {                   // fila del reporte "facturacion por mecanico"
    Mecanico mecanico;
    int   ordenesCerradas;
    float manoObra;
    float repuestos;
};

// ====================================================================
// ====================================================================
//  2. CAPA DE PERSISTENCIA  (lee y escribe bytes)
// ====================================================================
// ====================================================================

// ---------------- Header ----------------

// Crea el archivo con su header inicial si NO existe. Si existe no lo toca.
// Retorna true si el archivo quedo listo para usarse.
bool inicializarArchivo(const char* ruta) {
    ifstream existe(ruta, ios::binary);
    if (existe.is_open()) return true;
    ofstream nuevo(ruta, ios::binary);
    if (!nuevo) return false;
    ArchivoHeader h = {0, 1, 0, 1};
    nuevo.write((const char*)&h, sizeof(h));
    return nuevo.good();
}

// Lee SOLO el header (posicion 0)
bool leerHeader(const char* ruta, ArchivoHeader* h) {
    ifstream f(ruta, ios::binary);
    if (!f) return false;
    f.read((char*)h, sizeof(ArchivoHeader));
    return (size_t)f.gcount() == sizeof(ArchivoHeader);
}

// Sobrescribe SOLO el header, sin tocar los registros
bool escribirHeader(const char* ruta, const ArchivoHeader* h) {
    fstream f(ruta, ios::in | ios::out | ios::binary);
    if (!f) return false;
    f.seekp(0);
    f.write((const char*)h, sizeof(ArchivoHeader));
    return f.good();
}

// ---------------- Acceso aleatorio ----------------
// Los registros nunca se mueven y los IDs empiezan en 1, por eso:
//     indice fisico = id - 1
//     posicion en bytes = sizeof(ArchivoHeader) + (id - 1) * sizeof(Estructura)
long calcularPosicion(int id, size_t tamRegistro) {
    return (long)sizeof(ArchivoHeader) + (long)(id - 1) * (long)tamRegistro;
}

// Comprueba (con el header) que el ID exista en el archivo
Resultado verificarId(const char* ruta, int id) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return RES_ERROR_ARCHIVO;
    if (id < 1 || id > h.cantidadRegistros) return RES_NO_EXISTE;
    return RES_OK;
}

// Lee UN registro por su ID (calcula la posicion y salta con seekg)
Resultado leerRegistroCrudo(const char* ruta, int id, void* destino, size_t tam, long* posicion) {
    ifstream f(ruta, ios::binary);
    if (!f) return RES_ERROR_ARCHIVO;
    if (id < 1) return RES_NO_EXISTE;
    long pos = calcularPosicion(id, tam);
    f.seekg(pos);
    f.read((char*)destino, tam);
    if ((size_t)f.gcount() != tam) return RES_NO_EXISTE;
    if (posicion != nullptr) *posicion = pos;
    return RES_OK;
}

// Escribe UN registro en su posicion original (seekp)
Resultado escribirRegistroCrudo(const char* ruta, int id, const void* origen, size_t tam) {
    Resultado r = verificarId(ruta, id);
    if (r != RES_OK) return r;
    fstream f(ruta, ios::in | ios::out | ios::binary);
    if (!f) return RES_ERROR_ARCHIVO;
    f.seekp(calcularPosicion(id, tam));
    f.write((const char*)origen, tam);
    return f.good() ? RES_OK : RES_ERROR_ARCHIVO;
}

// Agrega un registro al final y actualiza el header
bool agregarRegistroCrudo(const char* ruta, const void* origen, size_t tam) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return false;
    {
        fstream f(ruta, ios::in | ios::out | ios::binary);
        if (!f) return false;
        f.seekp(0, ios::end);
        f.write((const char*)origen, tam);
        if (!f.good()) return false;
    }
    h.cantidadRegistros++;
    h.registrosActivos++;
    h.proximoID++;
    return escribirHeader(ruta, &h);
}

int obtenerProximoId(const char* ruta) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return 0;
    return h.proximoID;
}

bool restarRegistroActivo(const char* ruta) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return false;
    h.registrosActivos--;
    return escribirHeader(ruta, &h);
}

int contarActivosCrudo(const char* ruta) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return -1;
    return h.registrosActivos;
}

// ---------------- Taller (un solo registro, sin header) ----------------
bool existeTaller() {
    ifstream f(RUTA_TALLER, ios::binary);
    return f.is_open();
}

bool guardarTaller(Taller* t) {
    t->fechaCreacion = time(nullptr);
    t->fechaUltimaModificacion = t->fechaCreacion;
    ofstream f(RUTA_TALLER, ios::binary | ios::trunc);
    if (!f) return false;
    f.write((const char*)t, sizeof(Taller));
    return f.good();
}

bool leerTaller(Taller* t) {
    ifstream f(RUTA_TALLER, ios::binary);
    if (!f) return false;
    f.read((char*)t, sizeof(Taller));
    return (size_t)f.gcount() == sizeof(Taller);
}

// ----- Vehiculos -----
int guardarVehiculo(Vehiculo* v) {
    int id = obtenerProximoId(RUTA_VEHICULOS);
    if (id == 0) return 0;
    v->id = id;
    v->eliminado = false;
    v->fechaCreacion = v->fechaUltimaModificacion = time(nullptr);
    return agregarRegistroCrudo(RUTA_VEHICULOS, v, sizeof(Vehiculo)) ? id : 0;
}
Resultado leerVehiculo(int id, Vehiculo* v, long* posicion = nullptr) {
    return leerRegistroCrudo(RUTA_VEHICULOS, id, v, sizeof(Vehiculo), posicion);
}
Resultado sobrescribirVehiculo(Vehiculo* v) {
    v->fechaUltimaModificacion = time(nullptr);
    return escribirRegistroCrudo(RUTA_VEHICULOS, v->id, v, sizeof(Vehiculo));
}
Resultado marcarVehiculoEliminado(int id) {
    Vehiculo v;
    Resultado r = leerVehiculo(id, &v);
    if (r != RES_OK) return r;
    if (v.eliminado) return RES_ELIMINADO;
    v.eliminado = true;
    r = sobrescribirVehiculo(&v);
    if (r != RES_OK) return r;
    return restarRegistroActivo(RUTA_VEHICULOS) ? RES_OK : RES_ERROR_ARCHIVO;
}
int contarVehiculosActivos() { return contarActivosCrudo(RUTA_VEHICULOS); }

// ----- Mecanicos -----
int guardarMecanico(Mecanico* m) {
    int id = obtenerProximoId(RUTA_MECANICOS);
    if (id == 0) return 0;
    m->id = id;
    m->eliminado = false;
    m->fechaCreacion = m->fechaUltimaModificacion = time(nullptr);
    return agregarRegistroCrudo(RUTA_MECANICOS, m, sizeof(Mecanico)) ? id : 0;
}
Resultado leerMecanico(int id, Mecanico* m, long* posicion = nullptr) {
    return leerRegistroCrudo(RUTA_MECANICOS, id, m, sizeof(Mecanico), posicion);
}
Resultado sobrescribirMecanico(Mecanico* m) {
    m->fechaUltimaModificacion = time(nullptr);
    return escribirRegistroCrudo(RUTA_MECANICOS, m->id, m, sizeof(Mecanico));
}
Resultado marcarMecanicoEliminado(int id) {
    Mecanico m;
    Resultado r = leerMecanico(id, &m);
    if (r != RES_OK) return r;
    if (m.eliminado) return RES_ELIMINADO;
    m.eliminado = true;
    r = sobrescribirMecanico(&m);
    if (r != RES_OK) return r;
    return restarRegistroActivo(RUTA_MECANICOS) ? RES_OK : RES_ERROR_ARCHIVO;
}
int contarMecanicosActivos() { return contarActivosCrudo(RUTA_MECANICOS); }
int contarMecanicosTotales() {                 // activos + dados de baja
    ArchivoHeader h;
    if (!leerHeader(RUTA_MECANICOS, &h)) return -1;
    return h.cantidadRegistros;
}

// ----- Repuestos -----
int guardarRepuesto(Repuesto* r) {
    int id = obtenerProximoId(RUTA_REPUESTOS);
    if (id == 0) return 0;
    r->id = id;
    r->eliminado = false;
    r->fechaCreacion = r->fechaUltimaModificacion = time(nullptr);
    return agregarRegistroCrudo(RUTA_REPUESTOS, r, sizeof(Repuesto)) ? id : 0;
}
Resultado leerRepuesto(int id, Repuesto* r, long* posicion = nullptr) {
    return leerRegistroCrudo(RUTA_REPUESTOS, id, r, sizeof(Repuesto), posicion);
}
Resultado sobrescribirRepuesto(Repuesto* r) {
    r->fechaUltimaModificacion = time(nullptr);
    return escribirRegistroCrudo(RUTA_REPUESTOS, r->id, r, sizeof(Repuesto));
}
Resultado marcarRepuestoEliminado(int id) {
    Repuesto r;
    Resultado res = leerRepuesto(id, &r);
    if (res != RES_OK) return res;
    if (r.eliminado) return RES_ELIMINADO;
    r.eliminado = true;
    res = sobrescribirRepuesto(&r);
    if (res != RES_OK) return res;
    return restarRegistroActivo(RUTA_REPUESTOS) ? RES_OK : RES_ERROR_ARCHIVO;
}
int contarRepuestosActivos() { return contarActivosCrudo(RUTA_REPUESTOS); }

// ----- Ordenes -----
int guardarOrden(OrdenServicio* o) {
    int id = obtenerProximoId(RUTA_ORDENES);
    if (id == 0) return 0;
    o->id = id;
    o->eliminado = false;
    o->fechaCreacion = o->fechaUltimaModificacion = time(nullptr);
    return agregarRegistroCrudo(RUTA_ORDENES, o, sizeof(OrdenServicio)) ? id : 0;
}
Resultado leerOrden(int id, OrdenServicio* o, long* posicion = nullptr) {
    return leerRegistroCrudo(RUTA_ORDENES, id, o, sizeof(OrdenServicio), posicion);
}
Resultado sobrescribirOrden(OrdenServicio* o) {
    o->fechaUltimaModificacion = time(nullptr);
    return escribirRegistroCrudo(RUTA_ORDENES, o->id, o, sizeof(OrdenServicio));
}
Resultado marcarOrdenEliminada(int id) {     // no se usa en el sistema (las ordenes no se eliminan)
    OrdenServicio o;
    Resultado r = leerOrden(id, &o);
    if (r != RES_OK) return r;
    if (o.eliminado) return RES_ELIMINADO;
    o.eliminado = true;
    r = sobrescribirOrden(&o);
    if (r != RES_OK) return r;
    return restarRegistroActivo(RUTA_ORDENES) ? RES_OK : RES_ERROR_ARCHIVO;
}
int contarOrdenesActivas() { return contarActivosCrudo(RUTA_ORDENES); }

// ====================================================================
// ====================================================================
//  3. CAPA DE LOGICA
//     Ninguna funcion retorna punteros a registros: los datos se
//     entregan como COPIAS (parametros de salida) y el Resultado
//     indica si salio bien o por que fallo.
// ====================================================================
// ====================================================================

// --------------------------------------------------------------------
//  3.1 Utilidades y validaciones
// --------------------------------------------------------------------

int main () {    
    cout << "El sistema de gestion de taller ha sido inicializado." << endl;

    return 0;
}
