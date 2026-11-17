#include <iostream>
#include <fstream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <climits>
#include <cerrno>
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
  #include <sys/types.h>
  #define ES_TERMINAL() isatty(0)
  #define CREAR_CARPETA(ruta) mkdir(ruta, 0755)
#endif

using namespace std;

// ====================================================================
//  CONSTANTES
// ====================================================================
const int ANIO_MINIMO          = 1950;
const int ANIO_MAXIMO          = 2027;
const int MAX_ORDENES_ABIERTAS = 3;     // por mecanico
const int MAX_RESULTADOS       = 20;    // maximo de registros que entrega una busqueda/lista
const int MAX_LINEAS_ORDEN     = 20;    // maximo de lineas ACTIVAS de detalle por orden
const int TOP_REPUESTOS        = 10;    // reporte de repuestos mas utilizados
const int MAX_PROBLEMAS        = 100;   // problemas que guarda el reporte de integridad

const char* const RUTA_CARPETA   = "datos";
const char* const RUTA_TALLER    = "datos/taller.bin";
const char* const RUTA_VEHICULOS = "datos/vehiculos.bin";
const char* const RUTA_MECANICOS = "datos/mecanicos.bin";
const char* const RUTA_REPUESTOS = "datos/repuestos.bin";
const char* const RUTA_ORDENES   = "datos/ordenes.bin";
const char* const RUTA_DETALLES  = "datos/detalles.bin";   // lineas de detalle de las ordenes

// ====================================================================
//  1. ESTRUCTURAS DE DATOS (tamanio fijo: sin punteros ni std::string)
// ====================================================================
struct ArchivoHeader {
    int cantidadRegistros;   // total historico (incluye eliminados logicamente)
    int proximoID;           // siguiente ID a asignar
    int registrosActivos;    // registros con eliminado == false
    int version;             // version del formato (inicia en 1)
};

struct Taller {
    char   nombre[100];
    char   rif[20];
    char   direccion[200];
    char   telefono[20];
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
    char especialidad[20];   // MOTOR, FRENOS, ELECTRICIDAD, SUSPENSION, GENERAL
    char fechaIngreso[11];   // YYYY-MM-DD
    bool eliminado;          // true = dado de baja
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

struct OrdenServicio {
    int   id;
    int   idVehiculo;
    int   idMecanico;
    char  fechaApertura[11];
    char  fechaCierre[11];      // vacia mientras este ABIERTA
    char  estado[12];           // "ABIERTA", "CERRADA" o "ANULADA"
    char  descripcionFalla[200];
    int   kilometrajeIngreso;
    float manoObra;
    bool  eliminado;            // las ordenes no se eliminan: siempre false
    time_t fechaCreacion;
    time_t fechaUltimaModificacion;
};

// DISENO DEL DETALLE: una LINEA de detalle es un registro propio en
// detalles.bin. Cada linea sabe a que orden pertenece (idOrden), asi todas
// las lineas tienen el mismo tamanio y las ordenes no crecen ni se mueven.
struct DetalleOrden {
    int   id;
    int   idOrden;
    int   idRepuesto;
    int   cantidad;
    float precioUnitario;       // precio AL MOMENTO de agregar la linea
    bool  eliminado;            // quitar una linea = borrado logico
    time_t fechaCreacion;
    time_t fechaUltimaModificacion;
};

// Resultado de una operacion de la capa de PERSISTENCIA
enum ResArchivo { ARCH_OK, ARCH_ERROR, ARCH_ID_NO_EXISTE };

// Resultado de una operacion de la capa de LOGICA. La presentacion lo
// traduce a mensajes para el usuario.
enum Resultado {
    RES_OK,
    RES_ERROR_ARCHIVO,
    RES_DATO_INVALIDO,
    RES_DUPLICADO,
    RES_VEHICULO_NO_EXISTE,
    RES_VEHICULO_ELIMINADO,
    RES_MECANICO_NO_EXISTE,
    RES_MECANICO_DE_BAJA,
    RES_REPUESTO_NO_EXISTE,
    RES_REPUESTO_ELIMINADO,
    RES_ORDEN_NO_EXISTE,
    RES_CANTIDAD_INVALIDA,
    RES_PRECIO_INVALIDO,
    RES_VEHICULO_CON_ORDEN_ABIERTA,
    RES_VEHICULO_CON_ORDENES,
    RES_MECANICO_LLENO,
    RES_MECANICO_CON_ORDENES_ABIERTAS,
    RES_KM_MENOR,
    RES_ORDEN_NO_ABIERTA,
    RES_ORDEN_NO_ANULABLE,
    RES_STOCK_INSUFICIENTE,
    RES_LINEA_NO_EXISTE,
    RES_LIMITE_LINEAS,
    RES_ORDEN_SIN_CONTENIDO,
    RES_REPUESTO_EN_USO
};

// Para el reporte de repuestos mas usados
struct UsoRepuesto {
    int  idRepuesto;
    long unidades;
};

// Para el reporte de integridad (la logica no imprime: guarda los mensajes)
struct ReporteIntegridad {
    int vehiculos, mecanicos, repuestos, ordenes, detalles;
    int totalProblemas;                 // todos los problemas encontrados
    int guardados;                      // cuantos mensajes caben en 'mensajes'
    char mensajes[MAX_PROBLEMAS][180];
};


// ====================================================================
// ====================================================================
//  2. CAPA DE PERSISTENCIA  (aqui NO se usa cin ni cout, ni reglas de negocio)
//  Lee y escribe registros crudos. No filtra eliminados.
// ====================================================================
// ====================================================================

// --------------------------------------------------------------------
//  2.1 Operaciones generales de archivos
// --------------------------------------------------------------------
bool existeArchivo(const char* ruta) {
    ifstream f(ruta, ios::binary);
    return f.is_open();
}

// 0 = creada, 1 = ya existia, -1 = error
int crearCarpeta(const char* ruta) {
    if (CREAR_CARPETA(ruta) == 0) return 0;
    if (errno == EEXIST) return 1;
    return -1;
}

// Nombre del archivo sin la carpeta ("datos/vehiculos.bin" -> "vehiculos.bin")
const char* nombreBase(const char* ruta) {
    const char* base = ruta;
    for (const char* p = ruta; *p != '\0'; p++)
        if (*p == '/' || *p == '\\') base = p + 1;
    return base;
}

// Tamanio del archivo en bytes, o -1 si no se pudo abrir
long tamanoDeArchivo(const char* ruta) {
    ifstream f(ruta, ios::binary | ios::ate);
    if (!f.is_open()) return -1;
    return (long)f.tellg();
}

// Copia un archivo byte a byte
bool copiarArchivo(const char* origen, const char* destino) {
    ifstream in(origen, ios::binary);
    if (!in.is_open()) return false;
    ofstream out(destino, ios::binary | ios::trunc);
    if (!out.is_open()) return false;
    char buffer[4096];
    while (in.read(buffer, sizeof(buffer)) || in.gcount() > 0) {
        out.write(buffer, in.gcount());
        if (out.fail()) return false;
    }
    out.flush();
    return !out.fail() && in.eof();
}

// --------------------------------------------------------------------
//  2.2 Header
// --------------------------------------------------------------------

// Si el archivo no existe lo crea con su header inicial; si existe no lo
// modifica. Retorna true si el archivo quedo listo para usarse.
bool inicializarArchivo(const char* ruta) {
    if (existeArchivo(ruta)) {
        ifstream f(ruta, ios::binary);
        ArchivoHeader h;
        f.read(reinterpret_cast<char*>(&h), sizeof(ArchivoHeader));
        return !f.fail();                       // existe y tiene un header completo
    }
    ArchivoHeader h;
    h.cantidadRegistros = 0;
    h.proximoID = 1;
    h.registrosActivos = 0;
    h.version = 1;
    ofstream f(ruta, ios::binary | ios::out);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(&h), sizeof(ArchivoHeader));
    f.flush();
    return !f.fail();
}

// Lee SOLO el header (posicion 0), sin leer ningun registro
bool leerHeader(const char* ruta, ArchivoHeader* h) {
    ifstream f(ruta, ios::binary);
    if (!f.is_open()) return false;
    f.seekg(0, ios::beg);
    f.read(reinterpret_cast<char*>(h), sizeof(ArchivoHeader));
    return !f.fail();
}

// Sobrescribe SOLO el header (posicion 0), sin tocar los registros.
// Se abre con in|out para que NO se borre el contenido del archivo.
bool escribirHeader(const char* ruta, const ArchivoHeader* h) {
    fstream f(ruta, ios::in | ios::out | ios::binary);
    if (!f.is_open()) return false;
    f.seekp(0, ios::beg);
    f.write(reinterpret_cast<const char*>(h), sizeof(ArchivoHeader));
    f.flush();
    return !f.fail();
}

// Total historico de registros (incluye eliminados), o -1 si hay error
int totalRegistros(const char* ruta) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return -1;
    return h.cantidadRegistros;
}

// Cantidad de registros activos leida del header (sin recorrer el archivo), o -1
int contarActivos(const char* ruta) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return -1;
    return h.registrosActivos;
}

// Cuantos registros caben realmente segun el tamanio del archivo (para la
// verificacion de integridad), o -1 si no se pudo abrir
int contarRegistrosReales(const char* ruta, int tam) {
    long t = tamanoDeArchivo(ruta);
    if (t < (long)sizeof(ArchivoHeader)) return -1;
    return (int)((t - (long)sizeof(ArchivoHeader)) / tam);
}

// --------------------------------------------------------------------
//  2.3 Acceso aleatorio (registros crudos)
//  Los registros nunca se mueven, se agregan al final y los IDs se asignan
//  en orden desde 1. Por eso:   indice fisico = ID - 1
//  posicion en bytes = sizeof(ArchivoHeader) + (ID - 1) * sizeof(Estructura)
// --------------------------------------------------------------------
long calcularPosicion(int id, int tam) {
    return (long)sizeof(ArchivoHeader) + (long)(id - 1) * tam;
}

// Lee el registro numero 'numero' (ID) SIN consultar el header (la usa la
// verificacion de integridad, que no puede fiarse del header).
bool leerRegistroCrudo(const char* ruta, int tam, int numero, void* destino, long* posicion) {
    if (numero < 1) return false;
    ifstream f(ruta, ios::binary);
    if (!f.is_open()) return false;
    long pos = calcularPosicion(numero, tam);
    f.seekg(pos, ios::beg);
    f.read(reinterpret_cast<char*>(destino), tam);
    if (f.fail()) return false;
    if (posicion != nullptr) *posicion = pos;
    return true;
}

// Leer por ID: ubica el registro por calculo (sin recorrer el archivo).
// Lo entrega este activo o eliminado.
ResArchivo leerRegistroPorID(const char* ruta, int tam, int id, void* destino, long* posicion) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return ARCH_ERROR;
    if (id < 1 || id > h.cantidadRegistros) return ARCH_ID_NO_EXISTE;
    if (!leerRegistroCrudo(ruta, tam, id, destino, posicion)) return ARCH_ERROR;
    return ARCH_OK;
}

// Sobrescribe el registro en su posicion original
ResArchivo sobrescribirRegistroPorID(const char* ruta, int tam, int id, const void* origen) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return ARCH_ERROR;
    if (id < 1 || id > h.cantidadRegistros) return ARCH_ID_NO_EXISTE;
    fstream f(ruta, ios::in | ios::out | ios::binary);
    if (!f.is_open()) return ARCH_ERROR;
    f.seekp(calcularPosicion(id, tam), ios::beg);
    f.write(reinterpret_cast<const char*>(origen), tam);
    f.flush();
    return f.fail() ? ARCH_ERROR : ARCH_OK;
}

// Escribe un registro nuevo al final y despues actualiza el header.
// 'h' es el header YA leido (el registro ya trae su ID = h.proximoID).
// Primero el registro y despues el header: si el programa se interrumpe
// entre las dos escrituras queda un registro "huerfano" que la verificacion
// de integridad detecta (el header indica menos registros de los reales).
ResArchivo agregarRegistroAlFinal(const char* ruta, int tam, const void* origen, ArchivoHeader h) {
    if (h.proximoID != h.cantidadRegistros + 1) return ARCH_ERROR;   // header incoherente
    fstream f(ruta, ios::in | ios::out | ios::binary);
    if (!f.is_open()) return ARCH_ERROR;
    f.seekp(calcularPosicion(h.cantidadRegistros + 1, tam), ios::beg);
    f.write(reinterpret_cast<const char*>(origen), tam);
    f.flush();
    if (f.fail()) return ARCH_ERROR;
    f.close();
    h.cantidadRegistros++;
    h.registrosActivos++;
    h.proximoID++;
    return escribirHeader(ruta, &h) ? ARCH_OK : ARCH_ERROR;
}

// registrosActivos-- en el header (parte del borrado logico)
bool restarRegistroActivo(const char* ruta) {
    ArchivoHeader h;
    if (!leerHeader(ruta, &h)) return false;
    if (h.registrosActivos > 0) h.registrosActivos--;
    return escribirHeader(ruta, &h);
}

// --------------------------------------------------------------------
//  2.4 Por entidad: leer, guardar, sobrescribir y marcar como eliminado
// --------------------------------------------------------------------

// ---- taller.bin: un unico registro, sin header ----
bool existeTaller() { return existeArchivo(RUTA_TALLER); }

bool guardarTaller(Taller* t) {
    time_t ahora = time(nullptr);
    t->fechaCreacion = ahora;
    t->fechaUltimaModificacion = ahora;
    ofstream f(RUTA_TALLER, ios::binary | ios::out | ios::trunc);
    if (!f.is_open()) return false;
    f.write(reinterpret_cast<const char*>(t), sizeof(Taller));
    f.flush();
    return !f.fail();
}

bool leerTaller(Taller* t) {
    ifstream f(RUTA_TALLER, ios::binary);
    if (!f.is_open()) return false;
    f.read(reinterpret_cast<char*>(t), sizeof(Taller));
    return !f.fail();
}

// ---- vehiculos.bin ----
ResArchivo leerVehiculo(int id, Vehiculo* v, long* posicion = nullptr) {
    return leerRegistroPorID(RUTA_VEHICULOS, (int)sizeof(Vehiculo), id, v, posicion);
}

ResArchivo guardarVehiculo(Vehiculo* v, int* idAsignado) {
    ArchivoHeader h;
    if (!leerHeader(RUTA_VEHICULOS, &h)) return ARCH_ERROR;
    time_t ahora = time(nullptr);
    v->id = h.proximoID;
    v->eliminado = false;
    v->fechaCreacion = ahora;
    v->fechaUltimaModificacion = ahora;
    ResArchivo r = agregarRegistroAlFinal(RUTA_VEHICULOS, (int)sizeof(Vehiculo), v, h);
    if (r == ARCH_OK && idAsignado != nullptr) *idAsignado = v->id;
    return r;
}

ResArchivo sobrescribirVehiculo(Vehiculo* v) {
    v->fechaUltimaModificacion = time(nullptr);
    return sobrescribirRegistroPorID(RUTA_VEHICULOS, (int)sizeof(Vehiculo), v->id, v);
}

ResArchivo marcarVehiculoEliminado(int id) {
    Vehiculo v;
    ResArchivo r = leerVehiculo(id, &v);
    if (r != ARCH_OK) return r;
    if (v.eliminado) return ARCH_OK;             // ya estaba eliminado
    v.eliminado = true;
    r = sobrescribirVehiculo(&v);
    if (r != ARCH_OK) return r;
    return restarRegistroActivo(RUTA_VEHICULOS) ? ARCH_OK : ARCH_ERROR;
}

// ---- mecanicos.bin ----
ResArchivo leerMecanico(int id, Mecanico* m, long* posicion = nullptr) {
    return leerRegistroPorID(RUTA_MECANICOS, (int)sizeof(Mecanico), id, m, posicion);
}

ResArchivo guardarMecanico(Mecanico* m, int* idAsignado) {
    ArchivoHeader h;
    if (!leerHeader(RUTA_MECANICOS, &h)) return ARCH_ERROR;
    time_t ahora = time(nullptr);
    m->id = h.proximoID;
    m->eliminado = false;
    m->fechaCreacion = ahora;
    m->fechaUltimaModificacion = ahora;
    ResArchivo r = agregarRegistroAlFinal(RUTA_MECANICOS, (int)sizeof(Mecanico), m, h);
    if (r == ARCH_OK && idAsignado != nullptr) *idAsignado = m->id;
    return r;
}

ResArchivo sobrescribirMecanico(Mecanico* m) {
    m->fechaUltimaModificacion = time(nullptr);
    return sobrescribirRegistroPorID(RUTA_MECANICOS, (int)sizeof(Mecanico), m->id, m);
}

ResArchivo marcarMecanicoEliminado(int id) {
    Mecanico m;
    ResArchivo r = leerMecanico(id, &m);
    if (r != ARCH_OK) return r;
    if (m.eliminado) return ARCH_OK;
    m.eliminado = true;
    r = sobrescribirMecanico(&m);
    if (r != ARCH_OK) return r;
    return restarRegistroActivo(RUTA_MECANICOS) ? ARCH_OK : ARCH_ERROR;
}

// ---- repuestos.bin ----
ResArchivo leerRepuesto(int id, Repuesto* r, long* posicion = nullptr) {
    return leerRegistroPorID(RUTA_REPUESTOS, (int)sizeof(Repuesto), id, r, posicion);
}

ResArchivo guardarRepuesto(Repuesto* rep, int* idAsignado) {
    ArchivoHeader h;
    if (!leerHeader(RUTA_REPUESTOS, &h)) return ARCH_ERROR;
    time_t ahora = time(nullptr);
    rep->id = h.proximoID;
    rep->eliminado = false;
    rep->fechaCreacion = ahora;
    rep->fechaUltimaModificacion = ahora;
    ResArchivo r = agregarRegistroAlFinal(RUTA_REPUESTOS, (int)sizeof(Repuesto), rep, h);
    if (r == ARCH_OK && idAsignado != nullptr) *idAsignado = rep->id;
    return r;
}

ResArchivo sobrescribirRepuesto(Repuesto* rep) {
    rep->fechaUltimaModificacion = time(nullptr);
    return sobrescribirRegistroPorID(RUTA_REPUESTOS, (int)sizeof(Repuesto), rep->id, rep);
}

ResArchivo marcarRepuestoEliminado(int id) {
    Repuesto rep;
    ResArchivo r = leerRepuesto(id, &rep);
    if (r != ARCH_OK) return r;
    if (rep.eliminado) return ARCH_OK;
    rep.eliminado = true;
    r = sobrescribirRepuesto(&rep);
    if (r != ARCH_OK) return r;
    return restarRegistroActivo(RUTA_REPUESTOS) ? ARCH_OK : ARCH_ERROR;
}

// ---- ordenes.bin ----
ResArchivo leerOrden(int id, OrdenServicio* o, long* posicion = nullptr) {
    return leerRegistroPorID(RUTA_ORDENES, (int)sizeof(OrdenServicio), id, o, posicion);
}

ResArchivo guardarOrden(OrdenServicio* o, int* idAsignado) {
    ArchivoHeader h;
    if (!leerHeader(RUTA_ORDENES, &h)) return ARCH_ERROR;
    time_t ahora = time(nullptr);
    o->id = h.proximoID;
    o->eliminado = false;
    o->fechaCreacion = ahora;
    o->fechaUltimaModificacion = ahora;
    ResArchivo r = agregarRegistroAlFinal(RUTA_ORDENES, (int)sizeof(OrdenServicio), o, h);
    if (r == ARCH_OK && idAsignado != nullptr) *idAsignado = o->id;
    return r;
}

ResArchivo sobrescribirOrden(OrdenServicio* o) {
    o->fechaUltimaModificacion = time(nullptr);
    return sobrescribirRegistroPorID(RUTA_ORDENES, (int)sizeof(OrdenServicio), o->id, o);
}

// (Las ordenes no se eliminan; se incluye por completitud de las 5 operaciones)
ResArchivo marcarOrdenEliminada(int id) {
    OrdenServicio o;
    ResArchivo r = leerOrden(id, &o);
    if (r != ARCH_OK) return r;
    if (o.eliminado) return ARCH_OK;
    o.eliminado = true;
    r = sobrescribirOrden(&o);
    if (r != ARCH_OK) return r;
    return restarRegistroActivo(RUTA_ORDENES) ? ARCH_OK : ARCH_ERROR;
}

// ---- detalles.bin ----
ResArchivo leerDetalle(int id, DetalleOrden* d, long* posicion = nullptr) {
    return leerRegistroPorID(RUTA_DETALLES, (int)sizeof(DetalleOrden), id, d, posicion);
}

ResArchivo guardarDetalle(DetalleOrden* d, int* idAsignado) {
    ArchivoHeader h;
    if (!leerHeader(RUTA_DETALLES, &h)) return ARCH_ERROR;
    time_t ahora = time(nullptr);
    d->id = h.proximoID;
    d->eliminado = false;
    d->fechaCreacion = ahora;
    d->fechaUltimaModificacion = ahora;
    ResArchivo r = agregarRegistroAlFinal(RUTA_DETALLES, (int)sizeof(DetalleOrden), d, h);
    if (r == ARCH_OK && idAsignado != nullptr) *idAsignado = d->id;
    return r;
}

ResArchivo sobrescribirDetalle(DetalleOrden* d) {
    d->fechaUltimaModificacion = time(nullptr);
    return sobrescribirRegistroPorID(RUTA_DETALLES, (int)sizeof(DetalleOrden), d->id, d);
}

ResArchivo marcarDetalleEliminado(int id) {
    DetalleOrden d;
    ResArchivo r = leerDetalle(id, &d);
    if (r != ARCH_OK) return r;
    if (d.eliminado) return ARCH_OK;
    d.eliminado = true;
    r = sobrescribirDetalle(&d);
    if (r != ARCH_OK) return r;
    return restarRegistroActivo(RUTA_DETALLES) ? ARCH_OK : ARCH_ERROR;
}


// ====================================================================
// ====================================================================
//  3. CAPA DE LOGICA  (aqui NO se usa cin ni cout; no se abren archivos
//     directamente: todo pasa por la capa de persistencia)
//     Ninguna funcion retorna punteros a registros: cada lectura produce
//     una COPIA que se entrega por parametro de salida.
// ====================================================================
// ====================================================================

// --------------------------------------------------------------------
//  3.1 Utilidades generales
// --------------------------------------------------------------------
void copiarTexto(char* destino, const char* origen, int tam) {
    strncpy(destino, origen, tam - 1);
    destino[tam - 1] = '\0';
}

bool esVacio(const char* t) {
    for (int i = 0; t[i] != '\0'; i++)
        if (!isspace((unsigned char)t[i])) return false;
    return true;
}

void aMayusculas(char* t) {
    for (int i = 0; t[i] != '\0'; i++) t[i] = (char)toupper((unsigned char)t[i]);
}

// true si 'texto' contiene 'sub' sin distinguir mayusculas/minusculas
// (con 'sub' vacio siempre es true)
bool contieneTexto(const char* texto, const char* sub) {
    int n = (int)strlen(texto);
    int m = (int)strlen(sub);
    for (int i = 0; i + m <= n; i++) {
        bool igual = true;
        for (int j = 0; j < m; j++) {
            if (tolower((unsigned char)texto[i + j]) != tolower((unsigned char)sub[j])) {
                igual = false;
                break;
            }
        }
        if (igual) return true;
    }
    return false;
}

// Escribe la fecha de hoy como YYYY-MM-DD (destino debe tener 11 bytes)
void obtenerFechaActual(char* destino) {
    time_t ahora = time(nullptr);
    tm* t = localtime(&ahora);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    copiarTexto(destino, buffer, 11);
}

// Convierte un time_t guardado en un registro a YYYY-MM-DD (destino: 11 bytes)
void formatearFecha(time_t momento, char* destino) {
    if (momento == 0) { copiarTexto(destino, "---", 11); return; }
    tm* t = localtime(&momento);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    copiarTexto(destino, buffer, 11);
}

bool esFechaValida(const char* f) {
    if (strlen(f) != 10) return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (f[i] != '-') return false;
        } else if (!isdigit((unsigned char)f[i])) {
            return false;
        }
    }
    int anio = atoi(f);
    int mes  = atoi(f + 5);
    int dia  = atoi(f + 8);
    if (anio < 1900 || mes < 1 || mes > 12 || dia < 1) return false;
    int diasDelMes[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool bisiesto = (anio % 4 == 0 && anio % 100 != 0) || (anio % 400 == 0);
    int maxDia = diasDelMes[mes - 1];
    if (mes == 2 && bisiesto) maxDia = 29;
    return dia <= maxDia;
}

bool esPlacaValida(const char* p) {
    int n = (int)strlen(p);
    if (n < 6 || n > 7) return false;
    for (int i = 0; i < n; i++)
        if (!isalnum((unsigned char)p[i])) return false;
    return true;
}

bool esCedulaValida(const char* c) {
    int i = 0;
    if (strlen(c) >= 2 && c[1] == '-') {
        char pref = (char)toupper((unsigned char)c[0]);
        if (pref == 'V' || pref == 'E' || pref == 'J') i = 2;
    }
    if (c[i] == '\0') return false;
    for (; c[i] != '\0'; i++)
        if (!isdigit((unsigned char)c[i])) return false;
    return true;
}

bool esEspecialidadValida(const char* e) {
    return strcmp(e, "MOTOR") == 0 || strcmp(e, "FRENOS") == 0 ||
           strcmp(e, "ELECTRICIDAD") == 0 || strcmp(e, "SUSPENSION") == 0 ||
           strcmp(e, "GENERAL") == 0;
}

bool esAnioValido(int anio) {
    return anio >= ANIO_MINIMO && anio <= ANIO_MAXIMO;
}

// Convierte el resultado de una lectura de persistencia en un Resultado de
// logica. 'siNoExiste' es lo que significa "ID fuera de rango" en cada entidad.
Resultado resultadoDeLectura(ResArchivo r, Resultado siNoExiste) {
    if (r == ARCH_OK) return RES_OK;
    if (r == ARCH_ID_NO_EXISTE) return siNoExiste;
    return RES_ERROR_ARCHIVO;
}

Resultado resultadoDeEscritura(ResArchivo r) {
    return r == ARCH_OK ? RES_OK : RES_ERROR_ARCHIVO;
}

// --------------------------------------------------------------------
//  3.2 Arranque: carpeta datos/ y archivos
// --------------------------------------------------------------------

// Crea la carpeta datos/ y deja listos (con su header) todos los archivos de
// entidades que no existan. taller.bin NO se crea aqui (lo crea el primer
// arranque al pedir los datos del taller). Si algo falla, *archivoFallido
// apunta al nombre del archivo con problemas.
Resultado prepararArchivos(const char** archivoFallido) {
    *archivoFallido = RUTA_CARPETA;
    if (crearCarpeta(RUTA_CARPETA) < 0) return RES_ERROR_ARCHIVO;

    const char* rutas[] = {RUTA_VEHICULOS, RUTA_MECANICOS, RUTA_REPUESTOS, RUTA_ORDENES, RUTA_DETALLES};
    for (int i = 0; i < 5; i++) {
        if (!inicializarArchivo(rutas[i])) {
            *archivoFallido = rutas[i];
            return RES_ERROR_ARCHIVO;
        }
    }
    return RES_OK;
}

Resultado registrarTaller(Taller datos) {
    if (esVacio(datos.nombre) || esVacio(datos.rif) || esVacio(datos.direccion) || esVacio(datos.telefono))
        return RES_DATO_INVALIDO;
    return guardarTaller(&datos) ? RES_OK : RES_ERROR_ARCHIVO;
}

Resultado obtenerTaller(Taller* t) {
    return leerTaller(t) ? RES_OK : RES_ERROR_ARCHIVO;
}

// Cantidades para que la presentacion verifique que una opcion tiene sentido
// (retornan -1 si no se pudo leer el archivo)
int contarVehiculosActivos() { return contarActivos(RUTA_VEHICULOS); }
int contarMecanicosActivos() { return contarActivos(RUTA_MECANICOS); }
int contarMecanicosTotales() { return totalRegistros(RUTA_MECANICOS); }
int contarRepuestosActivos() { return contarActivos(RUTA_REPUESTOS); }
int contarOrdenesTotales()   { return totalRegistros(RUTA_ORDENES); }

// --------------------------------------------------------------------
//  3.3 Consultas sobre ordenes y lineas de detalle (las usan los otros modulos)
//  Retornan -1 cuando no se pudo leer un archivo.
// --------------------------------------------------------------------

// Cantidad de ordenes de un vehiculo en cualquier estado
int contarOrdenesDeVehiculo(int idVehiculo) {
    int total = totalRegistros(RUTA_ORDENES);
    if (total < 0) return -1;
    int n = 0;
    OrdenServicio o;
    for (int id = 1; id <= total; id++) {
        if (leerOrden(id, &o) != ARCH_OK) return -1;
        if (!o.eliminado && o.idVehiculo == idVehiculo) n++;
    }
    return n;
}

// ID de la orden ABIERTA del vehiculo, 0 si no tiene, -1 si hay error
int buscarOrdenAbiertaDeVehiculo(int idVehiculo) {
    int total = totalRegistros(RUTA_ORDENES);
    if (total < 0) return -1;
    OrdenServicio o;
    for (int id = 1; id <= total; id++) {
        if (leerOrden(id, &o) != ARCH_OK) return -1;
        if (!o.eliminado && o.idVehiculo == idVehiculo && strcmp(o.estado, "ABIERTA") == 0) return o.id;
    }
    return 0;
}

int contarOrdenesAbiertasDeMecanico(int idMecanico) {
    int total = totalRegistros(RUTA_ORDENES);
    if (total < 0) return -1;
    int n = 0;
    OrdenServicio o;
    for (int id = 1; id <= total; id++) {
        if (leerOrden(id, &o) != ARCH_OK) return -1;
        if (!o.eliminado && o.idMecanico == idMecanico && strcmp(o.estado, "ABIERTA") == 0) n++;
    }
    return n;
}

// Con estado == nullptr cuenta todas las ordenes
int contarOrdenesPorEstado(const char* estado) {
    int total = totalRegistros(RUTA_ORDENES);
    if (total < 0) return -1;
    if (estado == nullptr) return total;
    int n = 0;
    OrdenServicio o;
    for (int id = 1; id <= total; id++) {
        if (leerOrden(id, &o) != ARCH_OK) return -1;
        if (!o.eliminado && strcmp(o.estado, estado) == 0) n++;
    }
    return n;
}

// Cantidad de lineas ACTIVAS de una orden
int contarLineasActivas(int idOrden) {
    int total = totalRegistros(RUTA_DETALLES);
    if (total < 0) return -1;
    int n = 0;
    DetalleOrden d;
    for (int id = 1; id <= total; id++) {
        if (leerDetalle(id, &d) != ARCH_OK) return -1;
        if (!d.eliminado && d.idOrden == idOrden) n++;
    }
    return n;
}

// ID de la linea activa de ese repuesto en esa orden (se copia en *salida),
// 0 si no existe, -1 si hay error
int buscarLineaActiva(int idOrden, int idRepuesto, DetalleOrden* salida) {
    int total = totalRegistros(RUTA_DETALLES);
    if (total < 0) return -1;
    DetalleOrden d;
    for (int id = 1; id <= total; id++) {
        if (leerDetalle(id, &d) != ARCH_OK) return -1;
        if (!d.eliminado && d.idOrden == idOrden && d.idRepuesto == idRepuesto) {
            *salida = d;
            return d.id;
        }
    }
    return 0;
}

// Lineas activas de una orden (como maximo MAX_LINEAS_ORDEN)
Resultado obtenerLineasDeOrden(int idOrden, DetalleOrden lineas[], int* cantidad) {
    *cantidad = 0;
    int total = totalRegistros(RUTA_DETALLES);
    if (total < 0) return RES_ERROR_ARCHIVO;
    DetalleOrden d;
    for (int id = 1; id <= total && *cantidad < MAX_LINEAS_ORDEN; id++) {
        if (leerDetalle(id, &d) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (!d.eliminado && d.idOrden == idOrden) lineas[(*cantidad)++] = d;
    }
    return RES_OK;
}

// 1 si el repuesto aparece en alguna linea activa de cualquier orden
// (incluidas las ANULADAS, que conservan sus lineas como historial), 0 si no, -1 error
int repuestoEstaEnUso(int idRepuesto) {
    int total = totalRegistros(RUTA_DETALLES);
    if (total < 0) return -1;
    DetalleOrden d;
    for (int id = 1; id <= total; id++) {
        if (leerDetalle(id, &d) != ARCH_OK) return -1;
        if (!d.eliminado && d.idRepuesto == idRepuesto) return 1;
    }
    return 0;
}

// --------------------------------------------------------------------
//  3.4 VEHICULOS
// --------------------------------------------------------------------

// Registra un vehiculo. Placa unica entre los ACTIVOS. Valida todo antes de escribir.
Resultado obtenerVehiculoPorPlaca(const char* placa, Vehiculo* salida);

Resultado registrarVehiculo(Vehiculo datos, int* idNuevo) {
    aMayusculas(datos.placa);
    aMayusculas(datos.cedulaPropietario);
    if (esVacio(datos.marca) || esVacio(datos.modelo) || esVacio(datos.nombrePropietario)) return RES_DATO_INVALIDO;
    if (!esPlacaValida(datos.placa)) return RES_DATO_INVALIDO;
    if (!esCedulaValida(datos.cedulaPropietario)) return RES_DATO_INVALIDO;
    if (!esAnioValido(datos.anio)) return RES_DATO_INVALIDO;
    if (datos.kilometraje < 0) return RES_DATO_INVALIDO;

    Vehiculo existente;
    Resultado r = obtenerVehiculoPorPlaca(datos.placa, &existente);
    if (r == RES_OK) return RES_DUPLICADO;
    if (r != RES_VEHICULO_NO_EXISTE) return r;            // error de archivo

    return resultadoDeEscritura(guardarVehiculo(&datos, idNuevo));
}

// RES_OK, RES_VEHICULO_NO_EXISTE o RES_VEHICULO_ELIMINADO (en ese caso
// *salida tambien trae los datos). *posicion = byte donde se leyo (opcional).
Resultado obtenerVehiculoPorID(int id, Vehiculo* salida, long* posicion = nullptr) {
    Resultado r = resultadoDeLectura(leerVehiculo(id, salida, posicion), RES_VEHICULO_NO_EXISTE);
    if (r == RES_OK && salida->eliminado) return RES_VEHICULO_ELIMINADO;
    return r;
}

// Solo encuentra vehiculos ACTIVOS (recorre el archivo, un registro a la vez)
Resultado obtenerVehiculoPorPlaca(const char* placa, Vehiculo* salida) {
    int total = totalRegistros(RUTA_VEHICULOS);
    if (total < 0) return RES_ERROR_ARCHIVO;
    Vehiculo v;
    for (int id = 1; id <= total; id++) {
        if (leerVehiculo(id, &v) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (!v.eliminado && strcmp(v.placa, placa) == 0) {
            *salida = v;
            return RES_OK;
        }
    }
    return RES_VEHICULO_NO_EXISTE;
}

// Vehiculos ACTIVOS cuyo propietario contiene 'texto' (texto vacio = todos).
// Entrega como maximo MAX_RESULTADOS; salta los primeros 'saltar' que coincidan
// (para paginar). *hayMas = true si quedan mas resultados.
Resultado buscarVehiculos(const char* texto, Vehiculo resultados[], int* cantidad, bool* hayMas, int saltar) {
    *cantidad = 0;
    *hayMas = false;
    int total = totalRegistros(RUTA_VEHICULOS);
    if (total < 0) return RES_ERROR_ARCHIVO;
    int coincidencias = 0;
    Vehiculo v;
    for (int id = 1; id <= total; id++) {
        if (leerVehiculo(id, &v) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (v.eliminado || !contieneTexto(v.nombrePropietario, texto)) continue;
        if (coincidencias++ < saltar) continue;
        if (*cantidad == MAX_RESULTADOS) { *hayMas = true; break; }
        resultados[(*cantidad)++] = v;
    }
    return RES_OK;
}

Resultado listarVehiculos(Vehiculo resultados[], int* cantidad, bool* hayMas, int saltar) {
    return buscarVehiculos("", resultados, cantidad, hayMas, saltar);
}

// Actualiza marca, modelo, anio y datos del propietario.
// No toca id, placa, kilometraje ni fechaCreacion.
Resultado actualizarVehiculo(int id, Vehiculo datos) {
    Vehiculo v;
    Resultado r = obtenerVehiculoPorID(id, &v);
    if (r != RES_OK) return r;
    aMayusculas(datos.cedulaPropietario);
    if (esVacio(datos.marca) || esVacio(datos.modelo) || esVacio(datos.nombrePropietario)) return RES_DATO_INVALIDO;
    if (!esCedulaValida(datos.cedulaPropietario)) return RES_DATO_INVALIDO;
    if (!esAnioValido(datos.anio)) return RES_DATO_INVALIDO;

    copiarTexto(v.marca, datos.marca, sizeof(v.marca));
    copiarTexto(v.modelo, datos.modelo, sizeof(v.modelo));
    v.anio = datos.anio;
    copiarTexto(v.nombrePropietario, datos.nombrePropietario, sizeof(v.nombrePropietario));
    copiarTexto(v.cedulaPropietario, datos.cedulaPropietario, sizeof(v.cedulaPropietario));
    copiarTexto(v.telefonoPropietario, datos.telefonoPropietario, sizeof(v.telefonoPropietario));
    return resultadoDeEscritura(sobrescribirVehiculo(&v));
}

// Borrado logico. Rechazado si el vehiculo tiene ordenes en cualquier estado.
Resultado eliminarVehiculo(int id) {
    Vehiculo v;
    Resultado r = obtenerVehiculoPorID(id, &v);
    if (r != RES_OK) return r;
    int n = contarOrdenesDeVehiculo(id);
    if (n < 0) return RES_ERROR_ARCHIVO;
    if (n > 0) return RES_VEHICULO_CON_ORDENES;
    return resultadoDeEscritura(marcarVehiculoEliminado(id));
}

// --------------------------------------------------------------------
//  3.5 MECANICOS
// --------------------------------------------------------------------

// RES_OK, RES_MECANICO_NO_EXISTE o RES_MECANICO_DE_BAJA (en ese caso *salida
// tambien trae los datos, para poder mostrar su nombre en el historial)
Resultado obtenerMecanicoPorID(int id, Mecanico* salida, long* posicion = nullptr) {
    Resultado r = resultadoDeLectura(leerMecanico(id, salida, posicion), RES_MECANICO_NO_EXISTE);
    if (r == RES_OK && salida->eliminado) return RES_MECANICO_DE_BAJA;
    return r;
}

// Busca por cedula entre TODOS los mecanicos (activos y dados de baja)
Resultado obtenerMecanicoPorCedula(const char* cedula, Mecanico* salida) {
    int total = totalRegistros(RUTA_MECANICOS);
    if (total < 0) return RES_ERROR_ARCHIVO;
    Mecanico m;
    for (int id = 1; id <= total; id++) {
        if (leerMecanico(id, &m) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (strcmp(m.cedula, cedula) == 0) {
            *salida = m;
            return RES_OK;
        }
    }
    return RES_MECANICO_NO_EXISTE;
}

// Cedula unica (tambien entre los dados de baja: es la misma persona)
Resultado registrarMecanico(Mecanico datos, int* idNuevo) {
    aMayusculas(datos.cedula);
    aMayusculas(datos.especialidad);
    if (esVacio(datos.nombre) || !esCedulaValida(datos.cedula) ||
        !esEspecialidadValida(datos.especialidad) || !esFechaValida(datos.fechaIngreso))
        return RES_DATO_INVALIDO;

    Mecanico existente;
    Resultado r = obtenerMecanicoPorCedula(datos.cedula, &existente);
    if (r == RES_OK) return RES_DUPLICADO;
    if (r != RES_MECANICO_NO_EXISTE) return r;

    return resultadoDeEscritura(guardarMecanico(&datos, idNuevo));
}

// Todos los mecanicos (activos y dados de baja), paginados
Resultado listarMecanicos(Mecanico resultados[], int* cantidad, bool* hayMas, int saltar) {
    *cantidad = 0;
    *hayMas = false;
    int total = totalRegistros(RUTA_MECANICOS);
    if (total < 0) return RES_ERROR_ARCHIVO;
    int vistos = 0;
    Mecanico m;
    for (int id = 1; id <= total; id++) {
        if (leerMecanico(id, &m) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (vistos++ < saltar) continue;
        if (*cantidad == MAX_RESULTADOS) { *hayMas = true; break; }
        resultados[(*cantidad)++] = m;
    }
    return RES_OK;
}

// Baja logica. Rechazada si tiene ordenes ABIERTAS.
Resultado darDeBajaMecanico(int id) {
    Mecanico m;
    Resultado r = obtenerMecanicoPorID(id, &m);
    if (r != RES_OK) return r;                       // no existe o ya esta de baja
    int abiertas = contarOrdenesAbiertasDeMecanico(id);
    if (abiertas < 0) return RES_ERROR_ARCHIVO;
    if (abiertas > 0) return RES_MECANICO_CON_ORDENES_ABIERTAS;
    return resultadoDeEscritura(marcarMecanicoEliminado(id));
}

// --------------------------------------------------------------------
//  3.6 REPUESTOS (inventario)
// --------------------------------------------------------------------

Resultado obtenerRepuestoPorID(int id, Repuesto* salida, long* posicion = nullptr) {
    Resultado r = resultadoDeLectura(leerRepuesto(id, salida, posicion), RES_REPUESTO_NO_EXISTE);
    if (r == RES_OK && salida->eliminado) return RES_REPUESTO_ELIMINADO;
    return r;
}

// Solo repuestos ACTIVOS
Resultado obtenerRepuestoPorCodigo(const char* codigo, Repuesto* salida) {
    int total = totalRegistros(RUTA_REPUESTOS);
    if (total < 0) return RES_ERROR_ARCHIVO;
    Repuesto r;
    for (int id = 1; id <= total; id++) {
        if (leerRepuesto(id, &r) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (!r.eliminado && strcmp(r.codigo, codigo) == 0) {
            *salida = r;
            return RES_OK;
        }
    }
    return RES_REPUESTO_NO_EXISTE;
}

// Registra un repuesto. Codigo unico entre los activos, precio > 0, stock >= 0, minimo >= 0.
Resultado registrarRepuesto(Repuesto datos, int* idNuevo) {
    aMayusculas(datos.codigo);
    if (esVacio(datos.codigo) || esVacio(datos.nombre) || esVacio(datos.marca) ||
        datos.precio <= 0 || datos.stock < 0 || datos.stockMinimo < 0)
        return RES_DATO_INVALIDO;

    Repuesto existente;
    Resultado r = obtenerRepuestoPorCodigo(datos.codigo, &existente);
    if (r == RES_OK) return RES_DUPLICADO;
    if (r != RES_REPUESTO_NO_EXISTE) return r;

    return resultadoDeEscritura(guardarRepuesto(&datos, idNuevo));
}

// Repuestos ACTIVOS cuyo codigo o nombre contiene 'texto' (vacio = todos), paginados
Resultado buscarRepuestos(const char* texto, Repuesto resultados[], int* cantidad, bool* hayMas, int saltar) {
    *cantidad = 0;
    *hayMas = false;
    int total = totalRegistros(RUTA_REPUESTOS);
    if (total < 0) return RES_ERROR_ARCHIVO;
    int coincidencias = 0;
    Repuesto r;
    for (int id = 1; id <= total; id++) {
        if (leerRepuesto(id, &r) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (r.eliminado) continue;
        if (!contieneTexto(r.codigo, texto) && !contieneTexto(r.nombre, texto)) continue;
        if (coincidencias++ < saltar) continue;
        if (*cantidad == MAX_RESULTADOS) { *hayMas = true; break; }
        resultados[(*cantidad)++] = r;
    }
    return RES_OK;
}

Resultado listarRepuestos(Repuesto resultados[], int* cantidad, bool* hayMas, int saltar) {
    return buscarRepuestos("", resultados, cantidad, hayMas, saltar);
}

bool repuestoEnBajoStock(const Repuesto* r) {
    return r->stock <= r->stockMinimo;
}

// Suma unidades al stock (cantidad > 0)
Resultado registrarEntradaRepuesto(int idRepuesto, int cantidad) {
    Repuesto r;
    Resultado res = obtenerRepuestoPorID(idRepuesto, &r);
    if (res != RES_OK) return res;
    if (cantidad <= 0 || r.stock > INT_MAX - cantidad) return RES_CANTIDAD_INVALIDA;
    r.stock += cantidad;
    return resultadoDeEscritura(sobrescribirRepuesto(&r));
}

// Cambia el precio vigente. NO afecta ordenes ya registradas (cada linea guarda su precio).
Resultado cambiarPrecioRepuesto(int idRepuesto, float nuevoPrecio) {
    Repuesto r;
    Resultado res = obtenerRepuestoPorID(idRepuesto, &r);
    if (res != RES_OK) return res;
    if (nuevoPrecio <= 0) return RES_PRECIO_INVALIDO;
    r.precio = nuevoPrecio;
    return resultadoDeEscritura(sobrescribirRepuesto(&r));
}

// Borrado logico. Rechazado si el repuesto aparece en el detalle de alguna orden.
Resultado eliminarRepuesto(int idRepuesto) {
    Repuesto r;
    Resultado res = obtenerRepuestoPorID(idRepuesto, &r);
    if (res != RES_OK) return res;
    int enUso = repuestoEstaEnUso(idRepuesto);
    if (enUso < 0) return RES_ERROR_ARCHIVO;
    if (enUso > 0) return RES_REPUESTO_EN_USO;
    return resultadoDeEscritura(marcarRepuestoEliminado(idRepuesto));
}

// --------------------------------------------------------------------
//  3.7 ORDENES DE SERVICIO
// --------------------------------------------------------------------

Resultado obtenerOrdenPorID(int id, OrdenServicio* salida, long* posicion = nullptr) {
    return resultadoDeLectura(leerOrden(id, salida, posicion), RES_ORDEN_NO_EXISTE);
}

float sumarLineas(const DetalleOrden lineas[], int n) {
    float total = 0;
    for (int i = 0; i < n; i++) total += lineas[i].cantidad * lineas[i].precioUnitario;
    return total;
}

// El total NO se guarda: se calcula leyendo las lineas.
// total = manoObra + suma(cantidad * precio historico)
Resultado calcularTotalesOrden(const OrdenServicio* o, float* totalRepuestos, float* totalOrden) {
    DetalleOrden lineas[MAX_LINEAS_ORDEN];
    int n;
    Resultado r = obtenerLineasDeOrden(o->id, lineas, &n);
    if (r != RES_OK) return r;
    *totalRepuestos = sumarLineas(lineas, n);
    *totalOrden = o->manoObra + *totalRepuestos;
    return RES_OK;
}

// Abre una orden. TODAS las validaciones van antes de la primera escritura.
// Escritura: primero la orden y despues el kilometraje del vehiculo.
Resultado abrirOrden(int idVehiculo, int idMecanico, int kmIngreso, const char* falla, int* idNuevo) {
    Vehiculo v;
    Resultado r = obtenerVehiculoPorID(idVehiculo, &v);
    if (r != RES_OK) return r;
    Mecanico m;
    r = obtenerMecanicoPorID(idMecanico, &m);
    if (r != RES_OK) return r;                       // no existe o esta de baja
    if (esVacio(falla)) return RES_DATO_INVALIDO;

    int abierta = buscarOrdenAbiertaDeVehiculo(idVehiculo);
    if (abierta < 0) return RES_ERROR_ARCHIVO;
    if (abierta > 0) return RES_VEHICULO_CON_ORDEN_ABIERTA;

    int abiertasMec = contarOrdenesAbiertasDeMecanico(idMecanico);
    if (abiertasMec < 0) return RES_ERROR_ARCHIVO;
    if (abiertasMec >= MAX_ORDENES_ABIERTAS) return RES_MECANICO_LLENO;

    if (kmIngreso < v.kilometraje) return RES_KM_MENOR;

    OrdenServicio o;
    memset(&o, 0, sizeof(o));
    o.idVehiculo = idVehiculo;
    o.idMecanico = idMecanico;
    obtenerFechaActual(o.fechaApertura);
    o.fechaCierre[0] = '\0';
    copiarTexto(o.estado, "ABIERTA", sizeof(o.estado));
    copiarTexto(o.descripcionFalla, falla, sizeof(o.descripcionFalla));
    o.kilometrajeIngreso = kmIngreso;
    o.manoObra = 0;

    if (guardarOrden(&o, idNuevo) != ARCH_OK) return RES_ERROR_ARCHIVO;
    v.kilometraje = kmIngreso;                       // al abrir se actualiza el km del vehiculo
    return resultadoDeEscritura(sobrescribirVehiculo(&v));
}

// Agrega (o acumula) un repuesto en una orden ABIERTA y descuenta el stock.
// Escritura: primero la LINEA y despues el STOCK (ver README, pregunta 2).
Resultado agregarRepuestoAOrden(int idOrden, int idRepuesto, int cantidad) {
    OrdenServicio o;
    Resultado r = obtenerOrdenPorID(idOrden, &o);
    if (r != RES_OK) return r;
    if (strcmp(o.estado, "ABIERTA") != 0) return RES_ORDEN_NO_ABIERTA;
    Repuesto rep;
    r = obtenerRepuestoPorID(idRepuesto, &rep);
    if (r != RES_OK) return r;
    if (cantidad <= 0) return RES_CANTIDAD_INVALIDA;
    if (rep.stock < cantidad) return RES_STOCK_INSUFICIENTE;

    DetalleOrden linea;
    int idLinea = buscarLineaActiva(idOrden, idRepuesto, &linea);
    if (idLinea < 0) return RES_ERROR_ARCHIVO;

    if (idLinea > 0) {
        // Ya estaba en la orden: se acumula en la misma linea (conserva su precio)
        if (linea.cantidad > INT_MAX - cantidad) return RES_CANTIDAD_INVALIDA;
        linea.cantidad += cantidad;
        if (sobrescribirDetalle(&linea) != ARCH_OK) return RES_ERROR_ARCHIVO;
    } else {
        int n = contarLineasActivas(idOrden);
        if (n < 0) return RES_ERROR_ARCHIVO;
        if (n >= MAX_LINEAS_ORDEN) return RES_LIMITE_LINEAS;
        memset(&linea, 0, sizeof(linea));
        linea.idOrden = idOrden;
        linea.idRepuesto = idRepuesto;
        linea.cantidad = cantidad;
        linea.precioUnitario = rep.precio;           // precio vigente en este momento
        if (guardarDetalle(&linea, nullptr) != ARCH_OK) return RES_ERROR_ARCHIVO;
    }
    rep.stock -= cantidad;
    return resultadoDeEscritura(sobrescribirRepuesto(&rep));
}

// Quita la LINEA COMPLETA (borrado logico de la linea) y devuelve sus unidades.
// Escritura: primero la linea y despues el stock.
Resultado quitarRepuestoDeOrden(int idOrden, int idRepuesto) {
    OrdenServicio o;
    Resultado r = obtenerOrdenPorID(idOrden, &o);
    if (r != RES_OK) return r;
    if (strcmp(o.estado, "ABIERTA") != 0) return RES_ORDEN_NO_ABIERTA;

    DetalleOrden linea;
    int idLinea = buscarLineaActiva(idOrden, idRepuesto, &linea);
    if (idLinea < 0) return RES_ERROR_ARCHIVO;
    if (idLinea == 0) return RES_LINEA_NO_EXISTE;

    Repuesto rep;
    r = obtenerRepuestoPorID(idRepuesto, &rep);
    if (r != RES_OK) return r;
    if (rep.stock > INT_MAX - linea.cantidad) return RES_CANTIDAD_INVALIDA;

    if (marcarDetalleEliminado(idLinea) != ARCH_OK) return RES_ERROR_ARCHIVO;
    rep.stock += linea.cantidad;
    return resultadoDeEscritura(sobrescribirRepuesto(&rep));
}

// Cierra una orden ABIERTA registrando la mano de obra.
Resultado cerrarOrden(int idOrden, float manoObra) {
    OrdenServicio o;
    Resultado r = obtenerOrdenPorID(idOrden, &o);
    if (r != RES_OK) return r;
    if (strcmp(o.estado, "ABIERTA") != 0) return RES_ORDEN_NO_ABIERTA;
    if (manoObra < 0) return RES_DATO_INVALIDO;
    int n = contarLineasActivas(idOrden);
    if (n < 0) return RES_ERROR_ARCHIVO;
    if (n == 0 && manoObra <= 0) return RES_ORDEN_SIN_CONTENIDO;

    o.manoObra = manoObra;
    copiarTexto(o.estado, "CERRADA", sizeof(o.estado));
    obtenerFechaActual(o.fechaCierre);
    return resultadoDeEscritura(sobrescribirOrden(&o));
}

// Anula una orden ABIERTA o CERRADA y devuelve al inventario TODAS sus unidades.
// Las lineas se conservan como historial.
// Escritura: primero el ESTADO (marca la orden como ANULADA, asi no se puede
// anular dos veces) y despues el stock de cada linea.
Resultado anularOrden(int idOrden) {
    OrdenServicio o;
    Resultado r = obtenerOrdenPorID(idOrden, &o);
    if (r != RES_OK) return r;
    if (strcmp(o.estado, "ABIERTA") != 0 && strcmp(o.estado, "CERRADA") != 0) return RES_ORDEN_NO_ANULABLE;

    DetalleOrden lineas[MAX_LINEAS_ORDEN];
    int n;
    r = obtenerLineasDeOrden(idOrden, lineas, &n);
    if (r != RES_OK) return r;

    // Validar antes de escribir: cada repuesto debe existir y poder recibir las unidades
    Repuesto rep;
    for (int i = 0; i < n; i++) {
        r = obtenerRepuestoPorID(lineas[i].idRepuesto, &rep);
        if (r != RES_OK) return r;
        if (rep.stock > INT_MAX - lineas[i].cantidad) return RES_CANTIDAD_INVALIDA;
    }

    copiarTexto(o.estado, "ANULADA", sizeof(o.estado));
    if (sobrescribirOrden(&o) != ARCH_OK) return RES_ERROR_ARCHIVO;

    for (int i = 0; i < n; i++) {
        r = obtenerRepuestoPorID(lineas[i].idRepuesto, &rep);
        if (r != RES_OK) return r;
        rep.stock += lineas[i].cantidad;
        if (sobrescribirRepuesto(&rep) != ARCH_OK) return RES_ERROR_ARCHIVO;
    }
    return RES_OK;
}

// Ordenes filtradas por estado (nullptr = todos) y por vehiculo (0 = todos).
// Entrega como maximo MAX_RESULTADOS; 'saltar' pagina; *hayMas avisa si quedan.
Resultado listarOrdenes(const char* estado, int idVehiculo, OrdenServicio resultados[],
                        int* cantidad, bool* hayMas, int saltar) {
    *cantidad = 0;
    *hayMas = false;
    int total = totalRegistros(RUTA_ORDENES);
    if (total < 0) return RES_ERROR_ARCHIVO;
    int coincidencias = 0;
    OrdenServicio o;
    for (int id = 1; id <= total; id++) {
        if (leerOrden(id, &o) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (o.eliminado) continue;
        if (estado != nullptr && strcmp(o.estado, estado) != 0) continue;
        if (idVehiculo != 0 && o.idVehiculo != idVehiculo) continue;
        if (coincidencias++ < saltar) continue;
        if (*cantidad == MAX_RESULTADOS) { *hayMas = true; break; }
        resultados[(*cantidad)++] = o;
    }
    return RES_OK;
}

Resultado listarOrdenesPorEstado(const char* estado, OrdenServicio resultados[], int* cantidad, bool* hayMas, int saltar) {
    return listarOrdenes(estado, 0, resultados, cantidad, hayMas, saltar);
}

Resultado listarOrdenesPorVehiculo(int idVehiculo, OrdenServicio resultados[], int* cantidad, bool* hayMas, int saltar) {
    return listarOrdenes(nullptr, idVehiculo, resultados, cantidad, hayMas, saltar);
}

// --------------------------------------------------------------------
//  3.8 REPORTES (se calculan leyendo los archivos, un registro a la vez)
// --------------------------------------------------------------------

// Total facturado (ordenes CERRADAS) de un vehiculo
Resultado calcularFacturacionVehiculo(int idVehiculo, float* total) {
    *total = 0;
    int cantOrdenes = totalRegistros(RUTA_ORDENES);
    if (cantOrdenes < 0) return RES_ERROR_ARCHIVO;
    OrdenServicio o;
    for (int id = 1; id <= cantOrdenes; id++) {
        if (leerOrden(id, &o) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (o.eliminado || o.idVehiculo != idVehiculo || strcmp(o.estado, "CERRADA") != 0) continue;
        float totalRep, totalOrden;
        Resultado r = calcularTotalesOrden(&o, &totalRep, &totalOrden);
        if (r != RES_OK) return r;
        *total += totalOrden;
    }
    return RES_OK;
}

// Para un mecanico: ordenes CERRADAS, mano de obra y repuestos de esas ordenes
Resultado calcularFacturacionMecanico(int idMecanico, int* cerradas, float* manoObra, float* repuestos) {
    *cerradas = 0;
    *manoObra = 0;
    *repuestos = 0;
    int cantOrdenes = totalRegistros(RUTA_ORDENES);
    if (cantOrdenes < 0) return RES_ERROR_ARCHIVO;
    OrdenServicio o;
    for (int id = 1; id <= cantOrdenes; id++) {
        if (leerOrden(id, &o) != ARCH_OK) return RES_ERROR_ARCHIVO;
        if (o.eliminado || o.idMecanico != idMecanico || strcmp(o.estado, "CERRADA") != 0) continue;
        float totalRep, totalOrden;
        Resultado r = calcularTotalesOrden(&o, &totalRep, &totalOrden);
        if (r != RES_OK) return r;
        (*cerradas)++;
        *manoObra += o.manoObra;
        *repuestos += totalRep;
    }
    return RES_OK;
}

// Los TOP_REPUESTOS repuestos con mas unidades en ordenes CERRADAS (mayor a menor)
Resultado calcularRepuestosMasUsados(UsoRepuesto top[], int* cantidad) {
    *cantidad = 0;
    int cantRep = totalRegistros(RUTA_REPUESTOS);
    int cantDet = totalRegistros(RUTA_DETALLES);
    if (cantRep < 0 || cantDet < 0) return RES_ERROR_ARCHIVO;

    // Un contador por repuesto (indexado por ID). No son registros: son numeros.
    long* unidades = new long[cantRep + 1]();
    DetalleOrden d;
    OrdenServicio o;
    for (int id = 1; id <= cantDet; id++) {
        if (leerDetalle(id, &d) != ARCH_OK) { delete[] unidades; return RES_ERROR_ARCHIVO; }
        if (d.eliminado || d.idRepuesto < 1 || d.idRepuesto > cantRep) continue;
        if (leerOrden(d.idOrden, &o) != ARCH_OK) continue;
        if (strcmp(o.estado, "CERRADA") == 0) unidades[d.idRepuesto] += d.cantidad;
    }
    while (*cantidad < TOP_REPUESTOS) {
        int mejor = 0;
        for (int i = 1; i <= cantRep; i++)
            if (unidades[i] > 0 && (mejor == 0 || unidades[i] > unidades[mejor])) mejor = i;
        if (mejor == 0) break;
        top[*cantidad].idRepuesto = mejor;
        top[*cantidad].unidades = unidades[mejor];
        (*cantidad)++;
        unidades[mejor] = 0;
    }
    delete[] unidades;
    return RES_OK;
}

// --------------------------------------------------------------------
//  3.9 MANTENIMIENTO: integridad y respaldo
// --------------------------------------------------------------------
void agregarProblema(ReporteIntegridad* rep, const char* texto) {
    rep->totalProblemas++;
    if (rep->guardados < MAX_PROBLEMAS) {
        copiarTexto(rep->mensajes[rep->guardados], texto, 180);
        rep->guardados++;
    }
}

// Revisa el header de un archivo: lo lee, cuenta los registros reales segun el
// tamanio del archivo y compara. Retorna false si no se pudo leer.
bool revisarEncabezado(ReporteIntegridad* rep, const char* ruta, int tam, ArchivoHeader* h, int* reales) {
    char texto[180];
    const char* nombre = nombreBase(ruta);
    *reales = 0;
    long t = tamanoDeArchivo(ruta);
    if (t < 0) {
        snprintf(texto, sizeof(texto), "ROTO: No se pudo abrir %s", nombre);
        agregarProblema(rep, texto);
        return false;
    }
    if (t < (long)sizeof(ArchivoHeader) || !leerHeader(ruta, h)) {
        snprintf(texto, sizeof(texto), "ROTO: No se pudo leer el header de %s", nombre);
        agregarProblema(rep, texto);
        return false;
    }
    long cuerpo = t - (long)sizeof(ArchivoHeader);
    *reales = (int)(cuerpo / tam);
    if (cuerpo % tam != 0) {
        snprintf(texto, sizeof(texto), "ROTO: %s tiene %ld bytes sobrantes (no es multiplo del tamanio del registro)",
                 nombre, cuerpo % tam);
        agregarProblema(rep, texto);
    }
    if (h->cantidadRegistros != *reales) {
        snprintf(texto, sizeof(texto), "ROTO: Header de %s indica %d registros; el archivo contiene %d",
                 nombre, h->cantidadRegistros, *reales);
        agregarProblema(rep, texto);
    }
    if (h->proximoID != h->cantidadRegistros + 1) {
        snprintf(texto, sizeof(texto), "ROTO: Header de %s tiene proximoID %d y se esperaba %d",
                 nombre, h->proximoID, h->cantidadRegistros + 1);
        agregarProblema(rep, texto);
    }
    return true;
}

void revisarActivos(ReporteIntegridad* rep, const char* ruta, const ArchivoHeader* h, int activosContados) {
    if (h->registrosActivos != activosContados) {
        char texto[180];
        snprintf(texto, sizeof(texto), "ROTO: Header de %s indica %d registros activos; se contaron %d",
                 nombreBase(ruta), h->registrosActivos, activosContados);
        agregarProblema(rep, texto);
    }
}

// Recorre todos los archivos (un registro a la vez) y anota todo lo que este fuera de lugar
Resultado verificarIntegridad(ReporteIntegridad* rep) {
    memset(rep, 0, sizeof(ReporteIntegridad));
    char texto[180];
    ArchivoHeader h;
    int reales;
    int activos;

    // taller.bin: un unico registro, sin header
    if (!existeTaller()) {
        agregarProblema(rep, "ROTO: No existe taller.bin");
    } else if (tamanoDeArchivo(RUTA_TALLER) != (long)sizeof(Taller)) {
        snprintf(texto, sizeof(texto), "ROTO: taller.bin mide %ld bytes y se esperaban %d",
                 tamanoDeArchivo(RUTA_TALLER), (int)sizeof(Taller));
        agregarProblema(rep, texto);
    }

    // ---- vehiculos ----
    int nVeh = 0;
    if (revisarEncabezado(rep, RUTA_VEHICULOS, (int)sizeof(Vehiculo), &h, &reales)) {
        nVeh = reales;
        activos = 0;
        Vehiculo v;
        for (int i = 1; i <= nVeh; i++) {
            if (!leerRegistroCrudo(RUTA_VEHICULOS, (int)sizeof(Vehiculo), i, &v, nullptr)) {
                snprintf(texto, sizeof(texto), "ROTO: No se pudo leer el registro %d de vehiculos.bin", i);
                agregarProblema(rep, texto);
                continue;
            }
            if (v.id != i) {
                snprintf(texto, sizeof(texto), "ROTO: El registro %d de vehiculos.bin tiene ID %d (deberia ser %d)", i, v.id, i);
                agregarProblema(rep, texto);
            }
            if (!v.eliminado) activos++;
        }
        revisarActivos(rep, RUTA_VEHICULOS, &h, activos);
    }
    rep->vehiculos = nVeh;

    // ---- mecanicos ----
    int nMec = 0;
    if (revisarEncabezado(rep, RUTA_MECANICOS, (int)sizeof(Mecanico), &h, &reales)) {
        nMec = reales;
        activos = 0;
        Mecanico m;
        for (int i = 1; i <= nMec; i++) {
            if (!leerRegistroCrudo(RUTA_MECANICOS, (int)sizeof(Mecanico), i, &m, nullptr)) {
                snprintf(texto, sizeof(texto), "ROTO: No se pudo leer el registro %d de mecanicos.bin", i);
                agregarProblema(rep, texto);
                continue;
            }
            if (m.id != i) {
                snprintf(texto, sizeof(texto), "ROTO: El registro %d de mecanicos.bin tiene ID %d (deberia ser %d)", i, m.id, i);
                agregarProblema(rep, texto);
            }
            if (!m.eliminado) activos++;
        }
        revisarActivos(rep, RUTA_MECANICOS, &h, activos);
    }
    rep->mecanicos = nMec;

    // ---- repuestos (ademas: stock negativo) ----
    int nRep = 0;
    if (revisarEncabezado(rep, RUTA_REPUESTOS, (int)sizeof(Repuesto), &h, &reales)) {
        nRep = reales;
        activos = 0;
        Repuesto r;
        for (int i = 1; i <= nRep; i++) {
            if (!leerRegistroCrudo(RUTA_REPUESTOS, (int)sizeof(Repuesto), i, &r, nullptr)) {
                snprintf(texto, sizeof(texto), "ROTO: No se pudo leer el registro %d de repuestos.bin", i);
                agregarProblema(rep, texto);
                continue;
            }
            if (r.id != i) {
                snprintf(texto, sizeof(texto), "ROTO: El registro %d de repuestos.bin tiene ID %d (deberia ser %d)", i, r.id, i);
                agregarProblema(rep, texto);
            }
            if (r.stock < 0) {
                snprintf(texto, sizeof(texto), "ROTO: Repuesto %s (ID %d) tiene stock negativo (%d)", r.codigo, r.id, r.stock);
                agregarProblema(rep, texto);
            }
            if (!r.eliminado) activos++;
        }
        revisarActivos(rep, RUTA_REPUESTOS, &h, activos);
    }
    rep->repuestos = nRep;

    // ---- ordenes (referencias a vehiculos y mecanicos) ----
    int nOrd = 0;
    if (revisarEncabezado(rep, RUTA_ORDENES, (int)sizeof(OrdenServicio), &h, &reales)) {
        nOrd = reales;
        activos = 0;
        OrdenServicio o;
        for (int i = 1; i <= nOrd; i++) {
            if (!leerRegistroCrudo(RUTA_ORDENES, (int)sizeof(OrdenServicio), i, &o, nullptr)) {
                snprintf(texto, sizeof(texto), "ROTO: No se pudo leer el registro %d de ordenes.bin", i);
                agregarProblema(rep, texto);
                continue;
            }
            if (o.id != i) {
                snprintf(texto, sizeof(texto), "ROTO: El registro %d de ordenes.bin tiene ID %d (deberia ser %d)", i, o.id, i);
                agregarProblema(rep, texto);
            }
            if (!o.eliminado) activos++;

            bool abierta = strcmp(o.estado, "ABIERTA") == 0;
            if (!abierta && strcmp(o.estado, "CERRADA") != 0 && strcmp(o.estado, "ANULADA") != 0) {
                snprintf(texto, sizeof(texto), "ROTO: Orden #%d tiene un estado desconocido '%s'", o.id, o.estado);
                agregarProblema(rep, texto);
            }

            // a) y b) vehiculo
            if (o.idVehiculo < 1 || o.idVehiculo > nVeh) {
                snprintf(texto, sizeof(texto), "ROTO: Orden #%d apunta al vehiculo ID %d (no existe en vehiculos.bin)", o.id, o.idVehiculo);
                agregarProblema(rep, texto);
            } else {
                Vehiculo v;
                if (leerRegistroCrudo(RUTA_VEHICULOS, (int)sizeof(Vehiculo), o.idVehiculo, &v, nullptr)) {
                    if (v.eliminado) {
                        snprintf(texto, sizeof(texto), "ROTO: Orden #%d (%s) apunta al vehiculo ID %d, que esta eliminado",
                                 o.id, o.estado, o.idVehiculo);
                        agregarProblema(rep, texto);
                    }
                    if (o.kilometrajeIngreso > v.kilometraje) {
                        snprintf(texto, sizeof(texto), "ROTO: Orden #%d tiene km de ingreso %d, mayor al km del vehiculo ID %d (%d)",
                                 o.id, o.kilometrajeIngreso, v.id, v.kilometraje);
                        agregarProblema(rep, texto);
                    }
                }
            }
            // a) y b) mecanico (en ordenes CERRADAS o ANULADAS un mecanico de baja es valido)
            if (o.idMecanico < 1 || o.idMecanico > nMec) {
                snprintf(texto, sizeof(texto), "ROTO: Orden #%d apunta al mecanico ID %d (no existe en mecanicos.bin)", o.id, o.idMecanico);
                agregarProblema(rep, texto);
            } else if (abierta) {
                Mecanico m;
                if (leerRegistroCrudo(RUTA_MECANICOS, (int)sizeof(Mecanico), o.idMecanico, &m, nullptr) && m.eliminado) {
                    snprintf(texto, sizeof(texto), "ROTO: Orden #%d esta ABIERTA y su mecanico ID %d esta dado de baja", o.id, o.idMecanico);
                    agregarProblema(rep, texto);
                }
            }
        }
        revisarActivos(rep, RUTA_ORDENES, &h, activos);
    }
    rep->ordenes = nOrd;

    // ---- detalles (c: repuestos inexistentes) ----
    int nDet = 0;
    if (revisarEncabezado(rep, RUTA_DETALLES, (int)sizeof(DetalleOrden), &h, &reales)) {
        nDet = reales;
        activos = 0;
        DetalleOrden d;
        for (int i = 1; i <= nDet; i++) {
            if (!leerRegistroCrudo(RUTA_DETALLES, (int)sizeof(DetalleOrden), i, &d, nullptr)) {
                snprintf(texto, sizeof(texto), "ROTO: No se pudo leer el registro %d de detalles.bin", i);
                agregarProblema(rep, texto);
                continue;
            }
            if (d.id != i) {
                snprintf(texto, sizeof(texto), "ROTO: El registro %d de detalles.bin tiene ID %d (deberia ser %d)", i, d.id, i);
                agregarProblema(rep, texto);
            }
            if (!d.eliminado) activos++;
            if (d.idRepuesto < 1 || d.idRepuesto > nRep) {
                snprintf(texto, sizeof(texto), "ROTO: Linea %d (orden #%d) apunta al repuesto ID %d (no existe en repuestos.bin)",
                         d.id, d.idOrden, d.idRepuesto);
                agregarProblema(rep, texto);
            }
            if (d.idOrden < 1 || d.idOrden > nOrd) {
                snprintf(texto, sizeof(texto), "ROTO: Linea %d apunta a la orden #%d (no existe en ordenes.bin)", d.id, d.idOrden);
                agregarProblema(rep, texto);
            }
            if (!d.eliminado && d.cantidad <= 0) {
                snprintf(texto, sizeof(texto), "ROTO: Linea %d (orden #%d) tiene cantidad %d", d.id, d.idOrden, d.cantidad);
                agregarProblema(rep, texto);
            }
        }
        revisarActivos(rep, RUTA_DETALLES, &h, activos);
    }
    rep->detalles = nDet;
    return RES_OK;
}

// Crea la carpeta backup_YYYY-MM-DD_HH-MM y copia byte a byte todos los .bin.
// La ruta creada queda en rutaDestino (tam = tamanio del arreglo).
Resultado crearBackup(char* rutaDestino, int tam) {
    time_t ahora = time(nullptr);
    tm* t = localtime(&ahora);
    char nombre[100];
    snprintf(nombre, sizeof(nombre), "backup_%04d-%02d-%02d_%02d-%02d",
             t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min);
    int c = crearCarpeta(nombre);
    if (c == 1) {                                    // ya hay un respaldo en este minuto: agregar segundos
        snprintf(nombre, sizeof(nombre), "backup_%04d-%02d-%02d_%02d-%02d-%02d",
                 t->tm_year + 1900, t->tm_mon + 1, t->tm_mday, t->tm_hour, t->tm_min, t->tm_sec);
        c = crearCarpeta(nombre);
    }
    if (c != 0) return RES_ERROR_ARCHIVO;

    const char* rutas[] = {RUTA_TALLER, RUTA_VEHICULOS, RUTA_MECANICOS, RUTA_REPUESTOS, RUTA_ORDENES, RUTA_DETALLES};
    for (int i = 0; i < 6; i++) {
        char destino[300];
        snprintf(destino, sizeof(destino), "%s/%s", nombre, nombreBase(rutas[i]));
        if (!copiarArchivo(rutas[i], destino)) return RES_ERROR_ARCHIVO;
    }
    copiarTexto(rutaDestino, nombre, tam);
    return RES_OK;
}


// ====================================================================
// ====================================================================
//  4. CAPA DE PRESENTACION  (cin / cout / menus)
//     NO abre archivos ni modifica datos directamente: siempre llama a
//     las funciones de la capa de logica.
// ====================================================================
// ====================================================================

// --------------------------------------------------------------------
//  4.1 Lectura de datos del usuario
// --------------------------------------------------------------------

bool entradaTerminada = false;      // true si se acaba la entrada (Ctrl+D / fin de archivo)

enum TipoDato { DATO_TEXTO, DATO_PLACA, DATO_CEDULA, DATO_ESPECIALIDAD, DATO_FECHA, DATO_CODIGO };

void recortar(string& t) {
    size_t ini = 0;
    while (ini < t.size() && isspace((unsigned char)t[ini])) ini++;
    size_t fin = t.size();
    while (fin > ini && isspace((unsigned char)t[fin - 1])) fin--;
    t = t.substr(ini, fin - ini);
}

bool esCancelar(const string& t) {
    if (t.size() != 8) return false;
    string u = t;
    for (size_t i = 0; i < u.size(); i++) u[i] = (char)toupper((unsigned char)u[i]);
    return u == "CANCELAR";
}

// Muestra 'mensaje' y lee una linea. Retorna false si se acabo la entrada.
// Cuando la entrada viene de un archivo (./taller < entrada.txt) la linea leida
// se muestra en pantalla para que salida.txt sea legible, y las lineas que
// empiezan con '#' se tratan como comentarios del archivo de pruebas.
bool leerLinea(const char* mensaje, string& linea) {
    if (ES_TERMINAL()) {
        cout << mensaje;
        if (!getline(cin, linea)) { entradaTerminada = true; return false; }
        recortar(linea);
        return true;
    }
    bool primerComentario = true;
    while (true) {
        if (!getline(cin, linea)) {
            cout << "\n[Fin de la entrada]\n";
            entradaTerminada = true;
            return false;
        }
        recortar(linea);
        if (!linea.empty() && linea[0] == '#') {
            if (primerComentario) cout << "\n";
            cout << linea << "\n";
            primerComentario = false;
            continue;
        }
        break;
    }
    cout << mensaje << linea << "\n";
    return true;
}

// Pide un texto. Retorna false si el usuario escribe CANCELAR (o se acaba la entrada).
bool pedirDato(const char* mensaje, char* destino, int tam, TipoDato tipo) {
    string linea;
    while (true) {
        if (!leerLinea(mensaje, linea)) return false;
        if (esCancelar(linea)) return false;
        if (linea.empty()) {
            cout << "ERROR: Este dato no puede estar vacio.\n";
            continue;
        }
        if ((int)linea.size() >= tam) {
            cout << "ERROR: Maximo " << (tam - 1) << " caracteres.\n";
            continue;
        }
        copiarTexto(destino, linea.c_str(), tam);
        if (tipo == DATO_PLACA || tipo == DATO_CEDULA || tipo == DATO_ESPECIALIDAD || tipo == DATO_CODIGO)
            aMayusculas(destino);

        if (tipo == DATO_PLACA && !esPlacaValida(destino)) {
            cout << "ERROR: La placa '" << destino << "' no es valida (6 o 7 caracteres alfanumericos).\n";
            continue;
        }
        if (tipo == DATO_CEDULA && !esCedulaValida(destino)) {
            cout << "ERROR: La cedula '" << destino << "' no es valida (solo digitos, prefijo opcional V-, E- o J-).\n";
            continue;
        }
        if (tipo == DATO_ESPECIALIDAD && !esEspecialidadValida(destino)) {
            cout << "ERROR: La especialidad '" << destino
                 << "' no es valida. Use MOTOR, FRENOS, ELECTRICIDAD, SUSPENSION o GENERAL.\n";
            continue;
        }
        if (tipo == DATO_FECHA && !esFechaValida(destino)) {
            cout << "ERROR: La fecha '" << destino << "' no es valida. Use el formato YYYY-MM-DD.\n";
            continue;
        }
        return true;
    }
}

// Pide un entero entre minimo y maximo. Si escribe texto, avisa y vuelve a pedir.
bool pedirEntero(const char* mensaje, int* valor, int minimo, int maximo) {
    string linea;
    while (true) {
        if (!leerLinea(mensaje, linea)) return false;
        if (esCancelar(linea)) return false;

        char* fin;
        long long n = strtoll(linea.c_str(), &fin, 10);
        if (linea.empty() || *fin != '\0') {
            cout << "ERROR: '" << linea << "' no es un numero entero valido.\n";
            continue;
        }
        if (n < minimo || n > maximo) {
            if (maximo == INT_MAX) cout << "ERROR: El valor debe ser mayor o igual a " << minimo << ".\n";
            else cout << "ERROR: El valor debe estar entre " << minimo << " y " << maximo << ".\n";
            continue;
        }
        *valor = (int)n;
        return true;
    }
}

// Pide un numero decimal. Si 'estricto' es true debe ser > 0; si no, >= 0.
bool pedirDecimal(const char* mensaje, float* valor, bool estricto) {
    string linea;
    while (true) {
        if (!leerLinea(mensaje, linea)) return false;
        if (esCancelar(linea)) return false;

        string texto = linea;
        for (size_t i = 0; i < texto.size(); i++)
            if (texto[i] == ',') texto[i] = '.';
        char* fin;
        double n = strtod(texto.c_str(), &fin);
        if (texto.empty() || *fin != '\0' || n != n) {
            cout << "ERROR: '" << linea << "' no es un numero valido.\n";
            continue;
        }
        if (estricto && n <= 0) { cout << "ERROR: El valor debe ser mayor que 0.\n"; continue; }
        if (!estricto && n < 0) { cout << "ERROR: El valor debe ser mayor o igual a 0.\n"; continue; }
        if (n > 1000000000.0) { cout << "ERROR: El valor es demasiado grande.\n"; continue; }
        *valor = (float)n;
        return true;
    }
}

// Pregunta S/N. Retorna true solo si responde S.
bool pedirConfirmacion(const char* mensaje) {
    string linea;
    while (true) {
        if (!leerLinea(mensaje, linea)) return false;
        if (linea == "S" || linea == "s") return true;
        if (linea == "N" || linea == "n" || esCancelar(linea)) return false;
        cout << "ERROR: Responda S o N.\n";
    }
}

// Lee la opcion de un menu (0..maxOpcion). Si se acaba la entrada retorna 0.
int pedirOpcion(const char* mensaje, int maxOpcion) {
    string linea;
    while (true) {
        if (!leerLinea(mensaje, linea)) return 0;
        char* fin;
        long n = strtol(linea.c_str(), &fin, 10);
        if (linea.empty() || *fin != '\0') {
            cout << "ERROR: '" << linea << "' no es una opcion valida.\n";
            continue;
        }
        if (n < 0 || n > maxOpcion) {
            cout << "ERROR: Elija una opcion entre 0 y " << maxOpcion << ".\n";
            continue;
        }
        return (int)n;
    }
}

// Solo actua en modo interactivo (con entrada desde archivo no limpia ni pausa)
void limpiarPantalla() {
    if (!ES_TERMINAL()) return;
#ifdef _WIN32
    int r = system("cls");
#else
    int r = system("clear");
#endif
    (void)r;
}

void pausar() {
    if (!ES_TERMINAL()) return;
    cout << "\nPresione ENTER para continuar...";
    string descarte;
    if (!getline(cin, descarte)) entradaTerminada = true;
}

// --------------------------------------------------------------------
//  4.2 Mensajes y verificaciones comunes
// --------------------------------------------------------------------

// Texto generico para cada resultado (los menus lo usan cuando no necesitan
// un mensaje mas especifico con nombres)
const char* textoResultado(Resultado r) {
    switch (r) {
        case RES_OK:                             return "Operacion exitosa.";
        case RES_ERROR_ARCHIVO:                  return "No se pudo leer o escribir un archivo de la carpeta datos/.";
        case RES_DATO_INVALIDO:                  return "Datos invalidos.";
        case RES_DUPLICADO:                      return "Ya existe un registro con ese dato unico.";
        case RES_VEHICULO_NO_EXISTE:             return "El vehiculo no existe.";
        case RES_VEHICULO_ELIMINADO:             return "El vehiculo fue eliminado.";
        case RES_MECANICO_NO_EXISTE:             return "El mecanico no existe.";
        case RES_MECANICO_DE_BAJA:               return "El mecanico esta dado de baja.";
        case RES_REPUESTO_NO_EXISTE:             return "El repuesto no existe.";
        case RES_REPUESTO_ELIMINADO:             return "El repuesto fue eliminado.";
        case RES_ORDEN_NO_EXISTE:                return "La orden no existe.";
        case RES_CANTIDAD_INVALIDA:              return "Cantidad invalida.";
        case RES_PRECIO_INVALIDO:                return "Precio invalido (debe ser mayor que 0).";
        case RES_VEHICULO_CON_ORDEN_ABIERTA:     return "El vehiculo ya tiene una orden ABIERTA.";
        case RES_VEHICULO_CON_ORDENES:           return "El vehiculo tiene ordenes registradas.";
        case RES_MECANICO_LLENO:                 return "El mecanico ya tiene el maximo de ordenes abiertas.";
        case RES_MECANICO_CON_ORDENES_ABIERTAS:  return "El mecanico tiene ordenes ABIERTAS.";
        case RES_KM_MENOR:                       return "El kilometraje es menor al registrado del vehiculo.";
        case RES_ORDEN_NO_ABIERTA:               return "La orden no esta ABIERTA y no puede modificarse.";
        case RES_ORDEN_NO_ANULABLE:              return "La orden no puede anularse.";
        case RES_STOCK_INSUFICIENTE:             return "Stock insuficiente.";
        case RES_LINEA_NO_EXISTE:                return "Ese repuesto no esta en la orden.";
        case RES_LIMITE_LINEAS:                  return "La orden alcanzo el maximo de lineas de detalle.";
        case RES_ORDEN_SIN_CONTENIDO:            return "La orden no tiene repuestos ni mano de obra.";
        case RES_REPUESTO_EN_USO:                return "El repuesto aparece en ordenes de servicio.";
    }
    return "Error desconocido.";
}

// Verifica que una opcion tenga sentido: si no hay registros, explica por que
// no se puede continuar. 'cantidad' < 0 significa que no se pudo leer el archivo.
bool verificarHay(int cantidad, const char* faltante, const char* sugerencia) {
    if (cantidad < 0) {
        cout << "ERROR: No se pudo leer un archivo de la carpeta datos/.\n";
        return false;
    }
    if (cantidad == 0) {
        cout << "ERROR: " << faltante << " " << sugerencia << "\n";
        return false;
    }
    return true;
}

int noNegativo(int n) { return n < 0 ? 0 : n; }

// --------------------------------------------------------------------
//  4.3 Utilidades de dibujo (cajas y tablas)
// --------------------------------------------------------------------
int longitudVisible(const char* t) {
    int n = 0;
    for (; *t != '\0'; t++)
        if (((unsigned char)*t & 0xC0) != 0x80) n++;
    return n;
}

void recortarVisible(const char* origen, char* destino, int maxVisible) {
    int visibles = 0, j = 0;
    for (int i = 0; origen[i] != '\0'; i++) {
        bool inicioDeCaracter = (((unsigned char)origen[i]) & 0xC0) != 0x80;
        if (inicioDeCaracter) {
            if (visibles == maxVisible) break;
            visibles++;
        }
        destino[j++] = origen[i];
    }
    destino[j] = '\0';
}

void imprimirPad(const char* texto, int ancho, bool alinearDerecha) {
    char buf[600];
    recortarVisible(texto, buf, ancho);
    int relleno = ancho - longitudVisible(buf);
    if (alinearDerecha) for (int i = 0; i < relleno; i++) cout << ' ';
    cout << buf;
    if (!alinearDerecha) for (int i = 0; i < relleno; i++) cout << ' ';
}

void repetir(const char* simbolo, int veces) {
    for (int i = 0; i < veces; i++) cout << simbolo;
}

void imprimirBorde(const char* izq, const char* medio, const char* der, const int* anchos, int n) {
    cout << izq;
    for (int i = 0; i < n; i++) {
        repetir("-", anchos[i]);
        if (i < n - 1) cout << medio;
    }
    cout << der << "\n";
}

void imprimirFila(const char* const* celdas, const int* anchos, const bool* derecha, int n) {
    for (int i = 0; i < n; i++) {
        cout << "| ";
        imprimirPad(celdas[i], anchos[i] - 2, derecha[i]);
        cout << " ";
    }
    cout << "|\n";
}

const int ANCHO_MENU = 45;

// Menus: una linea de "=" arriba y abajo del titulo, y las opciones debajo
void imprimirCajaMenu(const char* titulo, const char* subtitulo, const char* const* opciones, int n) {
    cout << "\n";
    repetir("=", ANCHO_MENU); cout << "\n";
    int izq = (ANCHO_MENU - longitudVisible(titulo)) / 2;
    repetir(" ", izq); cout << titulo << "\n";
    if (subtitulo != nullptr) {
        izq = (ANCHO_MENU - longitudVisible(subtitulo)) / 2;
        if (izq < 0) izq = 0;
        repetir(" ", izq); cout << subtitulo << "\n";
    }
    repetir("=", ANCHO_MENU); cout << "\n";
    for (int i = 0; i < n; i++) cout << opciones[i] << "\n";
    repetir("=", ANCHO_MENU); cout << "\n";
}

const int ANCHO_CAJA = 64;

void imprimirLineaCaja(const char* texto) {
    cout << "| "; imprimirPad(texto, ANCHO_CAJA - 2, false); cout << " |\n";
}

// --------------------------------------------------------------------
//  4.4 VEHICULOS (presentacion)
// --------------------------------------------------------------------
void mostrarVehiculo(const Vehiculo& v) {
    cout << "  Vehiculo    : " << v.placa << " - " << v.marca << " " << v.modelo << " " << v.anio;
    if (v.id > 0) cout << "  (ID " << v.id << ")";
    cout << "\n";
    cout << "  Kilometraje : " << v.kilometraje << " km\n";
    cout << "  Propietario : " << v.nombrePropietario << "  |  Cedula: " << v.cedulaPropietario
         << "  |  Tel: " << v.telefonoPropietario << "\n";
    if (v.fechaCreacion != 0) {
        char fecha[11];
        formatearFecha(v.fechaCreacion, fecha);
        cout << "  Registrado  : " << fecha << "\n";
    }
}

void mostrarTablaVehiculos(const Vehiculo lista[], int cantidad) {
    const int anchos[] = {4, 10, 22, 6, 10, 20};
    const bool der[]   = {true, false, false, true, true, false};
    const char* titulos[] = {"ID", "Placa", "Vehiculo", "Anio", "Km", "Propietario"};

    imprimirBorde("+", "+", "+", anchos, 6);
    imprimirFila(titulos, anchos, der, 6);
    imprimirBorde("+", "+", "+", anchos, 6);
    for (int i = 0; i < cantidad; i++) {
        const Vehiculo& v = lista[i];
        char id[20], nombreVeh[120], anio[20], km[20];
        snprintf(id, sizeof(id), "%d", v.id);
        snprintf(nombreVeh, sizeof(nombreVeh), "%s %s", v.marca, v.modelo);
        snprintf(anio, sizeof(anio), "%d", v.anio);
        snprintf(km, sizeof(km), "%d", v.kilometraje);
        const char* fila[] = {id, v.placa, nombreVeh, anio, km, v.nombrePropietario};
        imprimirFila(fila, anchos, der, 6);
    }
    imprimirBorde("+", "+", "+", anchos, 6);
}

// Muestra los vehiculos activos cuyo propietario contiene 'texto' (vacio = todos),
// de MAX_RESULTADOS en MAX_RESULTADOS (avisa cuando hay mas)
void mostrarVehiculosPaginados(const char* texto) {
    int saltar = 0;
    while (true) {
        Vehiculo pagina[MAX_RESULTADOS];
        int cantidad;
        bool hayMas;
        Resultado r = buscarVehiculos(texto, pagina, &cantidad, &hayMas, saltar);
        if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }
        if (cantidad == 0) {
            if (saltar == 0) cout << "No se encontraron vehiculos.\n";
            return;
        }
        mostrarTablaVehiculos(pagina, cantidad);
        saltar += cantidad;
        if (!hayMas) { cout << "Total de vehiculos mostrados: " << saltar << "\n"; return; }
        cout << "Se muestran " << saltar << " vehiculos; HAY MAS resultados.\n";
        if (!pedirConfirmacion("Ver los siguientes? (S/N): ")) return;
    }
}

// Pide una placa que NO exista todavia entre los vehiculos activos
bool pedirPlacaNueva(char* destino, int tam) {
    while (true) {
        if (!pedirDato("Placa: ", destino, tam, DATO_PLACA)) return false;
        Vehiculo existente;
        Resultado r = obtenerVehiculoPorPlaca(destino, &existente);
        if (r == RES_VEHICULO_NO_EXISTE) return true;
        if (r == RES_OK) {
            cout << "ERROR: La placa '" << destino << "' ya esta registrada.\n";
            continue;
        }
        cout << "ERROR: " << textoResultado(r) << "\n";
        return false;
    }
}

void menuRegistrarVehiculo() {
    Vehiculo nuevo;
    memset(&nuevo, 0, sizeof(nuevo));
    cout << "\n--- REGISTRAR VEHICULO (escriba CANCELAR en cualquier momento para abortar) ---\n";

    bool completo =
        pedirPlacaNueva(nuevo.placa, sizeof(nuevo.placa)) &&
        pedirDato("Marca: ", nuevo.marca, sizeof(nuevo.marca), DATO_TEXTO) &&
        pedirDato("Modelo: ", nuevo.modelo, sizeof(nuevo.modelo), DATO_TEXTO) &&
        pedirEntero("Anio (1950-2027): ", &nuevo.anio, ANIO_MINIMO, ANIO_MAXIMO) &&
        pedirEntero("Kilometraje: ", &nuevo.kilometraje, 0, INT_MAX) &&
        pedirDato("Nombre del propietario: ", nuevo.nombrePropietario, sizeof(nuevo.nombrePropietario), DATO_TEXTO) &&
        pedirDato("Cedula del propietario: ", nuevo.cedulaPropietario, sizeof(nuevo.cedulaPropietario), DATO_CEDULA) &&
        pedirDato("Telefono del propietario: ", nuevo.telefonoPropietario, sizeof(nuevo.telefonoPropietario), DATO_TEXTO);

    if (!completo) {
        cout << "Registro cancelado. No se creo ningun vehiculo.\n";
        return;
    }
    cout << "\nDatos a guardar:\n";
    mostrarVehiculo(nuevo);
    if (!pedirConfirmacion("Guardar este vehiculo? (S/N): ")) {
        cout << "Registro cancelado. No se creo ningun vehiculo.\n";
        return;
    }
    int id = 0;
    Resultado r = registrarVehiculo(nuevo, &id);
    if (r == RES_OK) cout << "Vehiculo '" << nuevo.placa << "' registrado correctamente (ID " << id << ").\n";
    else if (r == RES_DUPLICADO) cout << "ERROR: La placa '" << nuevo.placa << "' ya esta registrada.\n";
    else cout << "ERROR: No se pudo registrar el vehiculo '" << nuevo.placa << "'. " << textoResultado(r) << "\n";
}

void menuBuscarVehiculo() {
    cout << "\n--- BUSCAR VEHICULO ---\n";
    if (!verificarHay(contarVehiculosActivos(), "No hay vehiculos registrados.",
                      "Primero registre uno con la opcion 1.")) return;

    cout << "1. Por placa exacta\n2. Por propietario (nombre parcial)\n3. Por ID\n0. Volver\n";
    int op = pedirOpcion("Seleccione una opcion: ", 3);

    if (op == 1) {
        char placa[10];
        if (!pedirDato("Placa: ", placa, sizeof(placa), DATO_PLACA)) { cout << "Busqueda cancelada.\n"; return; }
        Vehiculo v;
        Resultado r = obtenerVehiculoPorPlaca(placa, &v);
        if (r == RES_OK) mostrarVehiculo(v);
        else if (r == RES_VEHICULO_NO_EXISTE) cout << "ERROR: No existe un vehiculo con la placa '" << placa << "'.\n";
        else cout << "ERROR: " << textoResultado(r) << "\n";
    } else if (op == 2) {
        char texto[100];
        if (!pedirDato("Nombre del propietario (o parte): ", texto, sizeof(texto), DATO_TEXTO)) { cout << "Busqueda cancelada.\n"; return; }
        mostrarVehiculosPaginados(texto);
    } else if (op == 3) {
        int id;
        if (!pedirEntero("ID del vehiculo: ", &id, 1, INT_MAX)) { cout << "Busqueda cancelada.\n"; return; }
        Vehiculo v;
        long posicion = -1;
        Resultado r = obtenerVehiculoPorID(id, &v, &posicion);
        if (posicion >= 0)       // mensaje de depuracion: donde se leyo el registro
            cout << "  [DEPURACION] Registro leido directamente en la posicion " << posicion
                 << " bytes (" << sizeof(ArchivoHeader) << " + (" << id << " - 1) * " << sizeof(Vehiculo) << ").\n";
        if (r == RES_OK) mostrarVehiculo(v);
        else if (r == RES_VEHICULO_ELIMINADO) cout << "ERROR: El vehiculo con ID " << id << " fue eliminado (placa " << v.placa << ").\n";
        else if (r == RES_VEHICULO_NO_EXISTE) cout << "ERROR: No existe un vehiculo con ID " << id << ".\n";
        else cout << "ERROR: " << textoResultado(r) << "\n";
    }
}

void menuActualizarVehiculo() {
    char placa[10];
    cout << "\n--- ACTUALIZAR VEHICULO (escriba CANCELAR para abortar) ---\n";
    if (!verificarHay(contarVehiculosActivos(), "No hay vehiculos registrados.",
                      "Primero registre uno con la opcion 1.")) return;

    if (!pedirDato("Placa del vehiculo a actualizar: ", placa, sizeof(placa), DATO_PLACA)) {
        cout << "Actualizacion cancelada.\n";
        return;
    }
    Vehiculo v;
    Resultado r = obtenerVehiculoPorPlaca(placa, &v);
    if (r == RES_VEHICULO_NO_EXISTE) { cout << "ERROR: No existe un vehiculo con la placa '" << placa << "'.\n"; return; }
    if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }
    cout << "Datos actuales:\n";
    mostrarVehiculo(v);

    Vehiculo datos;
    memset(&datos, 0, sizeof(datos));
    bool completo =
        pedirDato("Nueva marca: ", datos.marca, sizeof(datos.marca), DATO_TEXTO) &&
        pedirDato("Nuevo modelo: ", datos.modelo, sizeof(datos.modelo), DATO_TEXTO) &&
        pedirEntero("Nuevo anio (1950-2027): ", &datos.anio, ANIO_MINIMO, ANIO_MAXIMO) &&
        pedirDato("Nuevo nombre del propietario: ", datos.nombrePropietario, sizeof(datos.nombrePropietario), DATO_TEXTO) &&
        pedirDato("Nueva cedula del propietario: ", datos.cedulaPropietario, sizeof(datos.cedulaPropietario), DATO_CEDULA) &&
        pedirDato("Nuevo telefono del propietario: ", datos.telefonoPropietario, sizeof(datos.telefonoPropietario), DATO_TEXTO);
    if (!completo || !pedirConfirmacion("Actualizar este vehiculo? (S/N): ")) {
        cout << "Actualizacion cancelada. No se modifico nada.\n";
        return;
    }
    r = actualizarVehiculo(v.id, datos);
    if (r == RES_OK) {
        cout << "Vehiculo '" << v.placa << "' actualizado correctamente.\n";
        Vehiculo nuevo;
        if (obtenerVehiculoPorID(v.id, &nuevo) == RES_OK) mostrarVehiculo(nuevo);
    } else {
        cout << "ERROR: No se pudo actualizar el vehiculo '" << v.placa << "'. " << textoResultado(r) << "\n";
    }
}

void menuEliminarVehiculo() {
    char placa[10];
    cout << "\n--- ELIMINAR VEHICULO ---\n";
    if (!verificarHay(contarVehiculosActivos(), "No hay vehiculos registrados.",
                      "Primero registre uno con la opcion 1.")) return;

    if (!pedirDato("Placa del vehiculo a eliminar: ", placa, sizeof(placa), DATO_PLACA)) {
        cout << "Eliminacion cancelada.\n";
        return;
    }
    Vehiculo v;
    Resultado r = obtenerVehiculoPorPlaca(placa, &v);
    if (r == RES_VEHICULO_NO_EXISTE) { cout << "ERROR: No existe un vehiculo con la placa '" << placa << "'.\n"; return; }
    if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }

    int nOrdenes = contarOrdenesDeVehiculo(v.id);
    if (nOrdenes < 0) { cout << "ERROR: " << textoResultado(RES_ERROR_ARCHIVO) << "\n"; return; }
    if (nOrdenes > 0) {
        cout << "ERROR: El vehiculo '" << v.placa << "' tiene " << nOrdenes
             << (nOrdenes == 1 ? " orden registrada" : " ordenes registradas") << " y no puede eliminarse.\n";
        return;
    }
    mostrarVehiculo(v);
    if (!pedirConfirmacion("Eliminar este vehiculo? (S/N): ")) {
        cout << "Eliminacion cancelada.\n";
        return;
    }
    r = eliminarVehiculo(v.id);
    if (r == RES_OK) cout << "Vehiculo '" << v.placa << "' eliminado correctamente.\n";
    else cout << "ERROR: No se pudo eliminar el vehiculo '" << v.placa << "'. " << textoResultado(r) << "\n";
}

void menuListarVehiculos() {
    cout << "\n--- LISTA DE VEHICULOS ---\n";
    if (!verificarHay(contarVehiculosActivos(), "No hay vehiculos registrados.",
                      "Primero registre uno con la opcion 1.")) return;
    mostrarVehiculosPaginados("");
}

void submenuVehiculos() {
    const char* opciones[] = {"1. Registrar vehiculo", "2. Buscar vehiculo", "3. Actualizar vehiculo",
                              "4. Eliminar vehiculo", "5. Listar vehiculos", "0. Volver al menu principal"};
    int op = -1;
    while (op != 0 && !entradaTerminada) {
        char sub[60];
        snprintf(sub, sizeof(sub), "Vehiculos registrados: %d", noNegativo(contarVehiculosActivos()));
        limpiarPantalla();
        imprimirCajaMenu("GESTION DE VEHICULOS", sub, opciones, 6);
        op = pedirOpcion("Seleccione una opcion: ", 5);
        if (entradaTerminada) break;
        if (op != 0) limpiarPantalla();
        switch (op) {
            case 1: menuRegistrarVehiculo(); break;
            case 2: menuBuscarVehiculo(); break;
            case 3: menuActualizarVehiculo(); break;
            case 4: menuEliminarVehiculo(); break;
            case 5: menuListarVehiculos(); break;
        }
        if (op != 0) pausar();
    }
}

// --------------------------------------------------------------------
//  4.5 MECANICOS (presentacion)
// --------------------------------------------------------------------

void mostrarMecanico(const Mecanico& m) {
    cout << "  Mecanico      : " << m.nombre << "  (ID " << m.id << ")" << (m.eliminado ? "  ** DADO DE BAJA **" : "") << "\n";
    cout << "  Cedula        : " << m.cedula << "\n";
    cout << "  Especialidad  : " << m.especialidad << "\n";
    cout << "  Fecha ingreso : " << m.fechaIngreso << "\n";
    int abiertas = contarOrdenesAbiertasDeMecanico(m.id);
    cout << "  Ordenes abiertas: " << noNegativo(abiertas) << "\n";
}

void mostrarTablaMecanicos(const Mecanico lista[], int cantidad) {
    const int anchos[] = {4, 24, 14, 16, 12, 10, 8};
    const bool der[]   = {true, false, false, false, false, true, false};
    const char* titulos[] = {"ID", "Nombre", "Cedula", "Especialidad", "Ingreso", "Abiertas", "Estado"};

    imprimirBorde("+", "+", "+", anchos, 7);
    imprimirFila(titulos, anchos, der, 7);
    imprimirBorde("+", "+", "+", anchos, 7);
    for (int i = 0; i < cantidad; i++) {
        const Mecanico& m = lista[i];
        char id[20], abiertas[20];
        snprintf(id, sizeof(id), "%d", m.id);
        snprintf(abiertas, sizeof(abiertas), "%d", noNegativo(contarOrdenesAbiertasDeMecanico(m.id)));
        const char* fila[] = {id, m.nombre, m.cedula, m.especialidad, m.fechaIngreso, abiertas,
                              m.eliminado ? "BAJA" : "ACTIVO"};
        imprimirFila(fila, anchos, der, 7);
    }
    imprimirBorde("+", "+", "+", anchos, 7);
}

void mostrarMecanicosPaginados() {
    int saltar = 0;
    while (true) {
        Mecanico pagina[MAX_RESULTADOS];
        int cantidad;
        bool hayMas;
        Resultado r = listarMecanicos(pagina, &cantidad, &hayMas, saltar);
        if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }
        if (cantidad == 0) {
            if (saltar == 0) cout << "No hay mecanicos registrados.\n";
            return;
        }
        mostrarTablaMecanicos(pagina, cantidad);
        saltar += cantidad;
        if (!hayMas) { cout << "Total de mecanicos mostrados: " << saltar << "\n"; return; }
        cout << "Se muestran " << saltar << " mecanicos; HAY MAS resultados.\n";
        if (!pedirConfirmacion("Ver los siguientes? (S/N): ")) return;
    }
}

void menuRegistrarMecanico() {
    Mecanico nuevo;
    memset(&nuevo, 0, sizeof(nuevo));
    cout << "\n--- REGISTRAR MECANICO (escriba CANCELAR para abortar) ---\n";

    bool completo = pedirDato("Nombre completo: ", nuevo.nombre, sizeof(nuevo.nombre), DATO_TEXTO);
    while (completo) {                      // la cedula debe ser unica: si ya existe, se vuelve a pedir
        completo = pedirDato("Cedula: ", nuevo.cedula, sizeof(nuevo.cedula), DATO_CEDULA);
        if (!completo) break;
        Mecanico existente;
        Resultado r = obtenerMecanicoPorCedula(nuevo.cedula, &existente);
        if (r == RES_MECANICO_NO_EXISTE) break;
        if (r == RES_OK) {
            cout << "ERROR: La cedula '" << nuevo.cedula << "' ya pertenece al mecanico '" << existente.nombre << "'"
                 << (existente.eliminado ? " (dado de baja)" : "") << ".\n";
            continue;
        }
        cout << "ERROR: " << textoResultado(r) << "\n";
        completo = false;
    }
    completo = completo &&
        pedirDato("Especialidad (MOTOR/FRENOS/ELECTRICIDAD/SUSPENSION/GENERAL): ", nuevo.especialidad, sizeof(nuevo.especialidad), DATO_ESPECIALIDAD) &&
        pedirDato("Fecha de ingreso (YYYY-MM-DD): ", nuevo.fechaIngreso, sizeof(nuevo.fechaIngreso), DATO_FECHA);

    if (!completo) {
        cout << "Registro cancelado. No se creo ningun mecanico.\n";
        return;
    }
    cout << "\nDatos a guardar: " << nuevo.nombre << " | Cedula " << nuevo.cedula << " | "
         << nuevo.especialidad << " | Ingreso " << nuevo.fechaIngreso << "\n";
    if (!pedirConfirmacion("Guardar este mecanico? (S/N): ")) {
        cout << "Registro cancelado. No se creo ningun mecanico.\n";
        return;
    }
    int id = 0;
    Resultado r = registrarMecanico(nuevo, &id);
    if (r == RES_OK) cout << "Mecanico '" << nuevo.nombre << "' registrado correctamente (ID " << id << ").\n";
    else if (r == RES_DUPLICADO) cout << "ERROR: La cedula '" << nuevo.cedula << "' ya esta registrada.\n";
    else cout << "ERROR: No se pudo registrar al mecanico '" << nuevo.nombre << "'. " << textoResultado(r) << "\n";
}

void menuBuscarMecanico() {
    int id;
    cout << "\n--- BUSCAR MECANICO POR ID ---\n";
    if (!verificarHay(contarMecanicosTotales(), "No hay mecanicos registrados.",
                      "Primero registre uno con la opcion 1.")) return;
    if (!pedirEntero("ID del mecanico: ", &id, 1, INT_MAX)) { cout << "Busqueda cancelada.\n"; return; }
    Mecanico m;
    Resultado r = obtenerMecanicoPorID(id, &m);
    if (r == RES_OK || r == RES_MECANICO_DE_BAJA) mostrarMecanico(m);
    else if (r == RES_MECANICO_NO_EXISTE) cout << "ERROR: No existe un mecanico con ID " << id << ".\n";
    else cout << "ERROR: " << textoResultado(r) << "\n";
}

void menuListarMecanicos() {
    cout << "\n--- LISTA DE MECANICOS (activos y dados de baja) ---\n";
    if (!verificarHay(contarMecanicosTotales(), "No hay mecanicos registrados.",
                      "Primero registre uno con la opcion 1.")) return;
    mostrarMecanicosPaginados();
}

void menuDarBajaMecanico() {
    int id;
    cout << "\n--- DAR DE BAJA MECANICO (escriba CANCELAR para abortar) ---\n";
    if (!verificarHay(contarMecanicosActivos(), "No hay mecanicos activos.",
                      "Primero registre uno con la opcion 1.")) return;

    mostrarMecanicosPaginados();
    if (!pedirEntero("ID del mecanico a dar de baja: ", &id, 1, INT_MAX)) { cout << "Operacion cancelada.\n"; return; }
    Mecanico m;
    Resultado r = obtenerMecanicoPorID(id, &m);
    if (r == RES_MECANICO_NO_EXISTE) { cout << "ERROR: No existe un mecanico con ID " << id << ".\n"; return; }
    if (r == RES_MECANICO_DE_BAJA) { cout << "ERROR: El mecanico '" << m.nombre << "' ya esta dado de baja.\n"; return; }
    if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }

    int abiertas = contarOrdenesAbiertasDeMecanico(id);
    if (abiertas > 0) {
        cout << "ERROR: El mecanico '" << m.nombre << "' tiene " << abiertas
             << (abiertas == 1 ? " orden ABIERTA" : " ordenes ABIERTAS") << " y no puede darse de baja. Cierrelas o anulelas primero.\n";
        return;
    }
    mostrarMecanico(m);
    cout << "Un mecanico dado de baja no recibe ordenes nuevas, pero sus ordenes cerradas y anuladas conservan su nombre.\n";
    if (!pedirConfirmacion("Dar de baja a este mecanico? (S/N): ")) { cout << "Operacion cancelada.\n"; return; }

    r = darDeBajaMecanico(id);
    if (r == RES_OK) cout << "Mecanico '" << m.nombre << "' dado de baja correctamente.\n";
    else if (r == RES_MECANICO_CON_ORDENES_ABIERTAS) cout << "ERROR: El mecanico '" << m.nombre << "' tiene ordenes ABIERTAS.\n";
    else cout << "ERROR: No se pudo dar de baja al mecanico '" << m.nombre << "'. " << textoResultado(r) << "\n";
}

void submenuMecanicos() {
    const char* opciones[] = {"1. Registrar mecanico", "2. Buscar mecanico por ID", "3. Listar mecanicos",
                              "4. Dar de baja mecanico", "0. Volver al menu principal"};
    int op = -1;
    while (op != 0 && !entradaTerminada) {
        char sub[60];
        snprintf(sub, sizeof(sub), "Mecanicos activos: %d", noNegativo(contarMecanicosActivos()));
        limpiarPantalla();
        imprimirCajaMenu("GESTION DE MECANICOS", sub, opciones, 5);
        op = pedirOpcion("Seleccione una opcion: ", 4);
        if (entradaTerminada) break;
        if (op != 0) limpiarPantalla();
        switch (op) {
            case 1: menuRegistrarMecanico(); break;
            case 2: menuBuscarMecanico(); break;
            case 3: menuListarMecanicos(); break;
            case 4: menuDarBajaMecanico(); break;
        }
        if (op != 0) pausar();
    }
}

// --------------------------------------------------------------------
//  4.6 REPUESTOS (presentacion)
// --------------------------------------------------------------------
void mostrarRepuesto(const Repuesto& r) {
    cout << "  Repuesto     : " << r.codigo << " - " << r.nombre;
    if (r.id > 0) cout << "  (ID " << r.id << ")";
    cout << "\n";
    cout << "  Marca        : " << r.marca << "\n";
    printf("  Precio       : %.2f\n", r.precio);
    cout << "  Stock        : " << r.stock << " (minimo " << r.stockMinimo << ")";
    if (repuestoEnBajoStock(&r)) cout << "  ** BAJO STOCK **";
    cout << "\n";
    if (r.fechaCreacion != 0) {
        char fecha[11];
        formatearFecha(r.fechaCreacion, fecha);
        cout << "  Registrado   : " << fecha << "\n";
    }
}

void mostrarTablaRepuestos(const Repuesto lista[], int cantidad) {
    const int anchos[] = {4, 10, 28, 14, 10, 7, 7, 14};
    const bool der[]   = {true, false, false, false, true, true, true, false};
    const char* titulos[] = {"ID", "Codigo", "Nombre", "Marca", "Precio", "Stock", "Min", "Estado"};

    imprimirBorde("+", "+", "+", anchos, 8);
    imprimirFila(titulos, anchos, der, 8);
    imprimirBorde("+", "+", "+", anchos, 8);
    for (int i = 0; i < cantidad; i++) {
        const Repuesto& r = lista[i];
        char id[20], precio[30], stock[20], minimo[20];
        snprintf(id, sizeof(id), "%d", r.id);
        snprintf(precio, sizeof(precio), "%.2f", r.precio);
        snprintf(stock, sizeof(stock), "%d", r.stock);
        snprintf(minimo, sizeof(minimo), "%d", r.stockMinimo);
        const char* estado = repuestoEnBajoStock(&r) ? "BAJO STOCK" : "OK";
        const char* fila[] = {id, r.codigo, r.nombre, r.marca, precio, stock, minimo, estado};
        imprimirFila(fila, anchos, der, 8);
    }
    imprimirBorde("+", "+", "+", anchos, 8);
}

// Repuestos activos cuyo codigo o nombre contiene 'texto' (vacio = todos), paginados
void mostrarRepuestosPaginados(const char* texto) {
    int saltar = 0;
    while (true) {
        Repuesto pagina[MAX_RESULTADOS];
        int cantidad;
        bool hayMas;
        Resultado r = buscarRepuestos(texto, pagina, &cantidad, &hayMas, saltar);
        if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }
        if (cantidad == 0) {
            if (saltar == 0) cout << "No se encontraron repuestos.\n";
            return;
        }
        mostrarTablaRepuestos(pagina, cantidad);
        saltar += cantidad;
        if (!hayMas) { cout << "Total de repuestos mostrados: " << saltar << "\n"; return; }
        cout << "Se muestran " << saltar << " repuestos; HAY MAS resultados.\n";
        if (!pedirConfirmacion("Ver los siguientes? (S/N): ")) return;
    }
}

void menuRegistrarRepuesto() {
    Repuesto nuevo;
    memset(&nuevo, 0, sizeof(nuevo));
    cout << "\n--- REGISTRAR REPUESTO (escriba CANCELAR para abortar) ---\n";

    bool completo = true;
    while (completo) {                      // el codigo debe ser unico entre los activos
        completo = pedirDato("Codigo: ", nuevo.codigo, sizeof(nuevo.codigo), DATO_CODIGO);
        if (!completo) break;
        Repuesto existente;
        Resultado r = obtenerRepuestoPorCodigo(nuevo.codigo, &existente);
        if (r == RES_REPUESTO_NO_EXISTE) break;
        if (r == RES_OK) {
            cout << "ERROR: El codigo '" << nuevo.codigo << "' ya esta registrado.\n";
            continue;
        }
        cout << "ERROR: " << textoResultado(r) << "\n";
        completo = false;
    }
    completo = completo &&
        pedirDato("Nombre: ", nuevo.nombre, sizeof(nuevo.nombre), DATO_TEXTO) &&
        pedirDato("Marca: ", nuevo.marca, sizeof(nuevo.marca), DATO_TEXTO) &&
        pedirDecimal("Precio unitario: ", &nuevo.precio, true) &&
        pedirEntero("Stock inicial: ", &nuevo.stock, 0, INT_MAX) &&
        pedirEntero("Stock minimo: ", &nuevo.stockMinimo, 0, INT_MAX);

    if (!completo) {
        cout << "Registro cancelado. No se creo ningun repuesto.\n";
        return;
    }
    cout << "\nDatos a guardar:\n";
    mostrarRepuesto(nuevo);
    if (!pedirConfirmacion("Guardar este repuesto? (S/N): ")) {
        cout << "Registro cancelado. No se creo ningun repuesto.\n";
        return;
    }
    int id = 0;
    Resultado r = registrarRepuesto(nuevo, &id);
    if (r == RES_OK) cout << "Repuesto '" << nuevo.codigo << " - " << nuevo.nombre << "' registrado correctamente (ID " << id << ").\n";
    else if (r == RES_DUPLICADO) cout << "ERROR: El codigo '" << nuevo.codigo << "' ya esta registrado.\n";
    else cout << "ERROR: No se pudo registrar el repuesto '" << nuevo.codigo << "'. " << textoResultado(r) << "\n";
}

void menuBuscarRepuesto() {
    cout << "\n--- BUSCAR REPUESTO ---\n";
    if (!verificarHay(contarRepuestosActivos(), "No hay repuestos registrados.",
                      "Primero registre uno con la opcion 1.")) return;

    cout << "1. Por ID\n2. Por codigo o nombre (parcial)\n0. Volver\n";
    int op = pedirOpcion("Seleccione una opcion: ", 2);

    if (op == 1) {
        int id;
        if (!pedirEntero("ID del repuesto: ", &id, 1, INT_MAX)) { cout << "Busqueda cancelada.\n"; return; }
        Repuesto r;
        Resultado res = obtenerRepuestoPorID(id, &r);
        if (res == RES_OK) mostrarRepuesto(r);
        else if (res == RES_REPUESTO_ELIMINADO) cout << "ERROR: El repuesto con ID " << id << " fue eliminado (codigo " << r.codigo << ").\n";
        else if (res == RES_REPUESTO_NO_EXISTE) cout << "ERROR: No existe un repuesto con ID " << id << ".\n";
        else cout << "ERROR: " << textoResultado(res) << "\n";
    } else if (op == 2) {
        char texto[100];
        if (!pedirDato("Codigo o nombre (o parte): ", texto, sizeof(texto), DATO_TEXTO)) { cout << "Busqueda cancelada.\n"; return; }
        mostrarRepuestosPaginados(texto);
    }
}

void menuEntradaMercancia() {
    char codigo[20];
    int cantidad;
    cout << "\n--- ENTRADA DE MERCANCIA (escriba CANCELAR para abortar) ---\n";
    if (!verificarHay(contarRepuestosActivos(), "No hay repuestos registrados.",
                      "Primero registre uno con la opcion 1.")) return;

    if (!pedirDato("Codigo del repuesto: ", codigo, sizeof(codigo), DATO_CODIGO)) { cout << "Operacion cancelada.\n"; return; }
    Repuesto r;
    Resultado res = obtenerRepuestoPorCodigo(codigo, &r);
    if (res == RES_REPUESTO_NO_EXISTE) { cout << "ERROR: No existe un repuesto con el codigo '" << codigo << "'.\n"; return; }
    if (res != RES_OK) { cout << "ERROR: " << textoResultado(res) << "\n"; return; }
    cout << "Stock actual de '" << r.nombre << "': " << r.stock << "\n";
    if (!pedirEntero("Cantidad a ingresar: ", &cantidad, 1, INT_MAX)) { cout << "Operacion cancelada.\n"; return; }
    if (!pedirConfirmacion("Registrar la entrada? (S/N): ")) { cout << "Operacion cancelada.\n"; return; }

    res = registrarEntradaRepuesto(r.id, cantidad);
    if (res == RES_OK) {
        Repuesto actualizado;
        if (obtenerRepuestoPorID(r.id, &actualizado) == RES_OK)
            cout << "Entrada registrada. Stock de '" << actualizado.nombre << "': " << actualizado.stock << ".\n";
        else cout << "Entrada registrada.\n";
    } else {
        cout << "ERROR: No se pudo registrar la entrada de '" << r.nombre << "'. " << textoResultado(res) << "\n";
    }
}

void menuCambiarPrecio() {
    char codigo[20];
    float nuevoPrecio;
    cout << "\n--- CAMBIAR PRECIO (escriba CANCELAR para abortar) ---\n";
    if (!verificarHay(contarRepuestosActivos(), "No hay repuestos registrados.",
                      "Primero registre uno con la opcion 1.")) return;

    if (!pedirDato("Codigo del repuesto: ", codigo, sizeof(codigo), DATO_CODIGO)) { cout << "Operacion cancelada.\n"; return; }
    Repuesto r;
    Resultado res = obtenerRepuestoPorCodigo(codigo, &r);
    if (res == RES_REPUESTO_NO_EXISTE) { cout << "ERROR: No existe un repuesto con el codigo '" << codigo << "'.\n"; return; }
    if (res != RES_OK) { cout << "ERROR: " << textoResultado(res) << "\n"; return; }
    printf("Precio actual de '%s': %.2f\n", r.nombre, r.precio);
    if (!pedirDecimal("Nuevo precio: ", &nuevoPrecio, true)) { cout << "Operacion cancelada.\n"; return; }
    if (!pedirConfirmacion("Actualizar el precio? (S/N): ")) { cout << "Operacion cancelada.\n"; return; }

    res = cambiarPrecioRepuesto(r.id, nuevoPrecio);
    if (res == RES_OK) printf("Precio de '%s' actualizado a %.2f (las ordenes ya registradas no cambian).\n", r.nombre, nuevoPrecio);
    else cout << "ERROR: No se pudo cambiar el precio de '" << r.nombre << "'. " << textoResultado(res) << "\n";
}

void menuEliminarRepuesto() {
    char codigo[20];
    cout << "\n--- ELIMINAR REPUESTO (escriba CANCELAR para abortar) ---\n";
    if (!verificarHay(contarRepuestosActivos(), "No hay repuestos registrados.",
                      "Primero registre uno con la opcion 1.")) return;

    if (!pedirDato("Codigo del repuesto: ", codigo, sizeof(codigo), DATO_CODIGO)) { cout << "Operacion cancelada.\n"; return; }
    Repuesto r;
    Resultado res = obtenerRepuestoPorCodigo(codigo, &r);
    if (res == RES_REPUESTO_NO_EXISTE) { cout << "ERROR: No existe un repuesto con el codigo '" << codigo << "'.\n"; return; }
    if (res != RES_OK) { cout << "ERROR: " << textoResultado(res) << "\n"; return; }

    int enUso = repuestoEstaEnUso(r.id);
    if (enUso < 0) { cout << "ERROR: " << textoResultado(RES_ERROR_ARCHIVO) << "\n"; return; }
    if (enUso > 0) {
        cout << "ERROR: El repuesto '" << r.nombre << "' aparece en ordenes de servicio y no puede eliminarse.\n";
        return;
    }
    mostrarRepuesto(r);
    if (!pedirConfirmacion("Eliminar este repuesto? (S/N): ")) { cout << "Eliminacion cancelada.\n"; return; }

    res = eliminarRepuesto(r.id);
    if (res == RES_OK) cout << "Repuesto '" << r.nombre << "' eliminado correctamente.\n";
    else cout << "ERROR: No se pudo eliminar el repuesto '" << r.nombre << "'. " << textoResultado(res) << "\n";
}

void menuListarRepuestos() {
    cout << "\n--- INVENTARIO DE REPUESTOS ---\n";
    if (!verificarHay(contarRepuestosActivos(), "No hay repuestos registrados.",
                      "Primero registre uno con la opcion 1.")) return;
    mostrarRepuestosPaginados("");
}

void submenuInventario() {
    const char* opciones[] = {"1. Registrar repuesto", "2. Buscar repuesto", "3. Registrar entrada de mercancia",
                              "4. Listar repuestos", "5. Cambiar precio", "6. Eliminar repuesto",
                              "0. Volver al menu principal"};
    int op = -1;
    while (op != 0 && !entradaTerminada) {
        char sub[60];
        snprintf(sub, sizeof(sub), "Repuestos registrados: %d", noNegativo(contarRepuestosActivos()));
        limpiarPantalla();
        imprimirCajaMenu("INVENTARIO DE REPUESTOS", sub, opciones, 7);
        op = pedirOpcion("Seleccione una opcion: ", 6);
        if (entradaTerminada) break;
        if (op != 0) limpiarPantalla();
        switch (op) {
            case 1: menuRegistrarRepuesto(); break;
            case 2: menuBuscarRepuesto(); break;
            case 3: menuEntradaMercancia(); break;
            case 4: menuListarRepuestos(); break;
            case 5: menuCambiarPrecio(); break;
            case 6: menuEliminarRepuesto(); break;
        }
        if (op != 0) pausar();
    }
}


// --------------------------------------------------------------------
//  4.7 ORDENES DE SERVICIO (presentacion)
// --------------------------------------------------------------------


// Dibuja el detalle completo de una orden. Los nombres del vehiculo, del
// mecanico (aunque este de baja) y de cada repuesto se leen desde sus archivos.
void mostrarOrden(const OrdenServicio& o) {
    Vehiculo v;
    Resultado rv = obtenerVehiculoPorID(o.idVehiculo, &v);
    bool hayVeh = (rv == RES_OK || rv == RES_VEHICULO_ELIMINADO);
    Mecanico m;
    Resultado rm = obtenerMecanicoPorID(o.idMecanico, &m);
    bool hayMec = (rm == RES_OK || rm == RES_MECANICO_DE_BAJA);
    char linea[500];

    cout << "+"; repetir("-", ANCHO_CAJA); cout << "+\n";
    snprintf(linea, sizeof(linea), "ORDEN DE SERVICIO #%d", o.id);
    imprimirLineaCaja(linea);
    cout << "+"; repetir("-", ANCHO_CAJA); cout << "+\n";

    snprintf(linea, sizeof(linea), "Estado   : %-8s Apertura: %s", o.estado, o.fechaApertura);
    imprimirLineaCaja(linea);
    snprintf(linea, sizeof(linea), "Cierre   : %s", o.fechaCierre[0] == '\0' ? "---" : o.fechaCierre);
    imprimirLineaCaja(linea);
    if (hayVeh) {
        snprintf(linea, sizeof(linea), "Vehiculo : %s - %s %s %d (%d km)", v.placa, v.marca, v.modelo, v.anio, o.kilometrajeIngreso);
        imprimirLineaCaja(linea);
        snprintf(linea, sizeof(linea), "Dueno    : %s", v.nombrePropietario);
        imprimirLineaCaja(linea);
    }
    if (hayMec) {
        snprintf(linea, sizeof(linea), "Mecanico : %s (%s)%s", m.nombre, m.especialidad, m.eliminado ? " - DADO DE BAJA" : "");
        imprimirLineaCaja(linea);
    }
    snprintf(linea, sizeof(linea), "Falla    : %s", o.descripcionFalla);
    imprimirLineaCaja(linea);

    DetalleOrden lineas[MAX_LINEAS_ORDEN];
    int n = 0;
    Resultado rl = obtenerLineasDeOrden(o.id, lineas, &n);
    if (rl != RES_OK) { imprimirLineaCaja("(no se pudieron leer las lineas de detalle)"); n = 0; }

    const int anchos[] = {10, 27, 6, 9, 8};
    const bool der[]   = {false, false, true, true, true};
    const char* titulos[] = {"Codigo", "Repuesto", "Cant", "P.Unit", "Total"};

    if (n == 0) {
        cout << "+"; repetir("-", ANCHO_CAJA); cout << "+\n";
        imprimirLineaCaja("(sin repuestos registrados)");
        cout << "+"; repetir("-", 55); cout << "+"; repetir("-", 8); cout << "+\n";
    } else {
        imprimirBorde("+", "+", "+", anchos, 5);
        imprimirFila(titulos, anchos, der, 5);
        imprimirBorde("+", "+", "+", anchos, 5);
        for (int i = 0; i < n; i++) {
            Repuesto r;
            Resultado rr = obtenerRepuestoPorID(lineas[i].idRepuesto, &r);
            bool hayRep = (rr == RES_OK || rr == RES_REPUESTO_ELIMINADO);
            char cant[20], pu[30], tot[30];
            snprintf(cant, sizeof(cant), "%d", lineas[i].cantidad);
            snprintf(pu, sizeof(pu), "%.2f", lineas[i].precioUnitario);
            snprintf(tot, sizeof(tot), "%.2f", lineas[i].cantidad * lineas[i].precioUnitario);
            const char* fila[] = {hayRep ? r.codigo : "?", hayRep ? r.nombre : "(repuesto no encontrado)", cant, pu, tot};
            imprimirFila(fila, anchos, der, 5);
        }
        cout << "+"; repetir("-", 10); cout << "+"; repetir("-", 27); cout << "+"; repetir("-", 6);
        cout << "+"; repetir("-", 9); cout << "+"; repetir("-", 8); cout << "+\n";
    }

    const int anchosResumen[] = {55, 8};
    const bool derResumen[]   = {false, true};
    float totalRep = sumarLineas(lineas, n);
    char totRep[30], totMano[30], totTotal[30];
    snprintf(totRep, sizeof(totRep), "%.2f", totalRep);
    snprintf(totMano, sizeof(totMano), "%.2f", o.manoObra);
    snprintf(totTotal, sizeof(totTotal), "%.2f", o.manoObra + totalRep);
    const char* f1[] = {"Repuestos", totRep};
    const char* f2[] = {"Mano de obra", totMano};
    const char* f3[] = {"TOTAL", totTotal};
    imprimirFila(f1, anchosResumen, derResumen, 2);
    imprimirFila(f2, anchosResumen, derResumen, 2);
    imprimirFila(f3, anchosResumen, derResumen, 2);
    imprimirBorde("+", "+", "+", anchosResumen, 2);
    cout << "(Lineas de detalle: " << n << " | maximo por orden: " << MAX_LINEAS_ORDEN << ")\n";
}

void mostrarTablaOrdenes(const OrdenServicio lista[], int cantidad) {
    const int anchos[] = {8, 10, 32, 28, 12, 10};
    const bool der[]   = {true, false, false, false, false, true};
    const char* titulos[] = {"Orden", "Estado", "Vehiculo", "Mecanico", "Apertura", "Total"};

    imprimirBorde("+", "+", "+", anchos, 6);
    imprimirFila(titulos, anchos, der, 6);
    imprimirBorde("+", "+", "+", anchos, 6);
    for (int i = 0; i < cantidad; i++) {
        const OrdenServicio& o = lista[i];
        Vehiculo v;
        Resultado rv = obtenerVehiculoPorID(o.idVehiculo, &v);
        Mecanico m;
        Resultado rm = obtenerMecanicoPorID(o.idMecanico, &m);
        char num[20], veh[150], mec[150], total[30];
        snprintf(num, sizeof(num), "#%d", o.id);
        if (rv == RES_OK || rv == RES_VEHICULO_ELIMINADO) snprintf(veh, sizeof(veh), "%s - %s %s", v.placa, v.marca, v.modelo);
        else snprintf(veh, sizeof(veh), "(no encontrado)");
        if (rm == RES_OK) snprintf(mec, sizeof(mec), "%s", m.nombre);
        else if (rm == RES_MECANICO_DE_BAJA) snprintf(mec, sizeof(mec), "%s (BAJA)", m.nombre);
        else snprintf(mec, sizeof(mec), "(no encontrado)");
        float totalRep, totalOrden;
        if (calcularTotalesOrden(&o, &totalRep, &totalOrden) == RES_OK) snprintf(total, sizeof(total), "%.2f", totalOrden);
        else snprintf(total, sizeof(total), "?");
        const char* fila[] = {num, o.estado, veh, mec, o.fechaApertura, total};
        imprimirFila(fila, anchos, der, 6);
    }
    imprimirBorde("+", "+", "+", anchos, 6);
}

// Lista ordenes (filtradas por estado y/o vehiculo), paginadas
void mostrarOrdenesPaginadas(const char* estado, int idVehiculo) {
    int saltar = 0;
    while (true) {
        OrdenServicio pagina[MAX_RESULTADOS];
        int cantidad;
        bool hayMas;
        Resultado r = listarOrdenes(estado, idVehiculo, pagina, &cantidad, &hayMas, saltar);
        if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }
        if (cantidad == 0) {
            if (saltar == 0) cout << "No hay ordenes para mostrar.\n";
            return;
        }
        mostrarTablaOrdenes(pagina, cantidad);
        saltar += cantidad;
        if (!hayMas) { cout << "Total de ordenes mostradas: " << saltar << "\n"; return; }
        cout << "Se muestran " << saltar << " ordenes; HAY MAS resultados.\n";
        if (!pedirConfirmacion("Ver las siguientes? (S/N): ")) return;
    }
}

// Pide el numero de una orden y la lee. Retorna false (con mensaje) si se
// cancela o no existe.
bool pedirOrden(OrdenServicio* o) {
    int idOrden;
    if (!pedirEntero("Numero de orden: ", &idOrden, 1, INT_MAX)) { cout << "Operacion cancelada.\n"; return false; }
    Resultado r = obtenerOrdenPorID(idOrden, o);
    if (r == RES_OK) return true;
    if (r == RES_ORDEN_NO_EXISTE) cout << "ERROR: No existe la orden #" << idOrden << ".\n";
    else cout << "ERROR: " << textoResultado(r) << "\n";
    return false;
}

void menuAbrirOrden() {
    char placa[10], falla[200];
    int idMecanico, km;
    cout << "\n--- ABRIR ORDEN DE SERVICIO (escriba CANCELAR para abortar) ---\n";
    if (!verificarHay(contarVehiculosActivos(), "No hay vehiculos registrados.",
                      "Primero registre uno en Gestion de Vehiculos.")) return;
    if (!verificarHay(contarMecanicosActivos(), "No hay mecanicos activos.",
                      "Primero registre uno en Gestion de Mecanicos.")) return;

    if (!pedirDato("Placa del vehiculo: ", placa, sizeof(placa), DATO_PLACA)) { cout << "Operacion cancelada.\n"; return; }
    Vehiculo v;
    Resultado r = obtenerVehiculoPorPlaca(placa, &v);
    if (r == RES_VEHICULO_NO_EXISTE) { cout << "ERROR: No existe un vehiculo con la placa '" << placa << "'.\n"; return; }
    if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }
    cout << "Vehiculo: " << v.placa << " - " << v.marca << " " << v.modelo << " (" << v.kilometraje
         << " km), propietario " << v.nombrePropietario << "\n";

    int abierta = buscarOrdenAbiertaDeVehiculo(v.id);
    if (abierta > 0) { cout << "ERROR: El vehiculo '" << v.placa << "' ya tiene la orden #" << abierta << " ABIERTA.\n"; return; }

    mostrarMecanicosPaginados();
    if (!pedirEntero("ID del mecanico: ", &idMecanico, 1, INT_MAX)) { cout << "Operacion cancelada.\n"; return; }
    Mecanico m;
    r = obtenerMecanicoPorID(idMecanico, &m);
    if (r == RES_MECANICO_NO_EXISTE) { cout << "ERROR: No existe un mecanico con ID " << idMecanico << ".\n"; return; }
    if (r == RES_MECANICO_DE_BAJA) { cout << "ERROR: El mecanico '" << m.nombre << "' esta dado de baja y no recibe ordenes nuevas.\n"; return; }
    if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }

    if (!pedirEntero("Kilometraje de ingreso: ", &km, 0, INT_MAX) ||
        !pedirDato("Falla reportada: ", falla, sizeof(falla), DATO_TEXTO)) {
        cout << "Operacion cancelada.\n";
        return;
    }
    cout << "Se abrira una orden para '" << v.placa << "' con el mecanico " << m.nombre << " (" << km << " km).\n";
    if (!pedirConfirmacion("Abrir esta orden? (S/N): ")) { cout << "Operacion cancelada.\n"; return; }

    int idNuevo = 0;
    r = abrirOrden(v.id, m.id, km, falla, &idNuevo);
    switch (r) {
        case RES_OK:
            cout << "Orden #" << idNuevo << " abierta para el vehiculo '" << v.placa << "' con el mecanico " << m.nombre << ".\n";
            break;
        case RES_VEHICULO_CON_ORDEN_ABIERTA:
            cout << "ERROR: El vehiculo '" << v.placa << "' ya tiene una orden ABIERTA.\n";
            break;
        case RES_MECANICO_LLENO:
            cout << "ERROR: El mecanico '" << m.nombre << "' ya tiene " << MAX_ORDENES_ABIERTAS << " ordenes abiertas.\n";
            break;
        case RES_KM_MENOR:
            cout << "ERROR: El kilometraje " << km << " es menor al ultimo registrado del vehiculo '"
                 << v.placa << "' (" << v.kilometraje << " km).\n";
            break;
        default:
            cout << "ERROR: No se pudo abrir la orden. " << textoResultado(r) << "\n";
    }
}

void menuAgregarRepuestoAOrden() {
    int cantidad;
    char codigo[20];
    cout << "\n--- AGREGAR REPUESTO A ORDEN (escriba CANCELAR para abortar) ---\n";
    if (!verificarHay(contarOrdenesPorEstado("ABIERTA"), "No hay ordenes ABIERTAS.",
                      "Primero abra una orden.")) return;
    if (!verificarHay(contarRepuestosActivos(), "No hay repuestos registrados.",
                      "Primero registre uno en el Inventario de Repuestos.")) return;

    OrdenServicio o;
    if (!pedirOrden(&o)) return;
    if (strcmp(o.estado, "ABIERTA") != 0) {
        cout << "ERROR: La orden #" << o.id << " esta " << o.estado << " y no puede modificarse.\n";
        return;
    }
    if (!pedirDato("Codigo del repuesto: ", codigo, sizeof(codigo), DATO_CODIGO)) { cout << "Operacion cancelada.\n"; return; }
    Repuesto rep;
    Resultado r = obtenerRepuestoPorCodigo(codigo, &rep);
    if (r == RES_REPUESTO_NO_EXISTE) { cout << "ERROR: No existe un repuesto con el codigo '" << codigo << "'.\n"; return; }
    if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }
    cout << "Stock disponible de '" << rep.nombre << "': " << rep.stock << "\n";

    if (!pedirEntero("Cantidad: ", &cantidad, 1, INT_MAX)) { cout << "Operacion cancelada.\n"; return; }

    r = agregarRepuestoAOrden(o.id, rep.id, cantidad);
    switch (r) {
        case RES_OK: {
            Repuesto actual;
            int restante = (obtenerRepuestoPorID(rep.id, &actual) == RES_OK) ? actual.stock : rep.stock - cantidad;
            cout << "Se agregaron " << cantidad << " unidad(es) de '" << rep.nombre << "' a la orden #" << o.id
                 << ". Stock restante: " << restante << ".\n";
            break;
        }
        case RES_ORDEN_NO_ABIERTA:
            cout << "ERROR: La orden #" << o.id << " no esta ABIERTA y no puede modificarse.\n";
            break;
        case RES_STOCK_INSUFICIENTE:
            cout << "ERROR: Stock insuficiente de '" << rep.nombre << "'. Disponible: " << rep.stock
                 << ", solicitado: " << cantidad << ".\n";
            break;
        case RES_LIMITE_LINEAS:
            cout << "ERROR: La orden #" << o.id << " ya tiene el maximo de " << MAX_LINEAS_ORDEN
                 << " lineas de detalle. La linea no se agrego.\n";
            break;
        default:
            cout << "ERROR: No se pudo agregar el repuesto a la orden #" << o.id << ". " << textoResultado(r) << "\n";
    }
}

void menuQuitarRepuestoDeOrden() {
    char codigo[20];
    cout << "\n--- QUITAR REPUESTO DE ORDEN (escriba CANCELAR para abortar) ---\n";
    if (!verificarHay(contarOrdenesPorEstado("ABIERTA"), "No hay ordenes ABIERTAS.",
                      "Primero abra una orden.")) return;

    OrdenServicio o;
    if (!pedirOrden(&o)) return;
    if (strcmp(o.estado, "ABIERTA") != 0) {
        cout << "ERROR: La orden #" << o.id << " esta " << o.estado << " y no puede modificarse.\n";
        return;
    }
    int nLineas = contarLineasActivas(o.id);
    if (nLineas == 0) { cout << "ERROR: La orden #" << o.id << " no tiene repuestos para quitar.\n"; return; }
    mostrarOrden(o);

    if (!pedirDato("Codigo del repuesto a quitar: ", codigo, sizeof(codigo), DATO_CODIGO)) { cout << "Operacion cancelada.\n"; return; }
    Repuesto rep;
    Resultado r = obtenerRepuestoPorCodigo(codigo, &rep);
    if (r == RES_REPUESTO_NO_EXISTE) { cout << "ERROR: No existe un repuesto con el codigo '" << codigo << "'.\n"; return; }
    if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }

    DetalleOrden linea;
    int idLinea = buscarLineaActiva(o.id, rep.id, &linea);
    int unidades = (idLinea > 0) ? linea.cantidad : 0;       // la linea desaparecera: guardamos la cantidad

    r = quitarRepuestoDeOrden(o.id, rep.id);
    switch (r) {
        case RES_OK: {
            Repuesto actual;
            int stock = (obtenerRepuestoPorID(rep.id, &actual) == RES_OK) ? actual.stock : rep.stock + unidades;
            cout << "Se quito '" << rep.nombre << "' de la orden #" << o.id << " (" << unidades
                 << " unidad(es) devueltas al inventario). Stock actual: " << stock << ".\n";
            break;
        }
        case RES_ORDEN_NO_ABIERTA:
            cout << "ERROR: La orden #" << o.id << " no esta ABIERTA y no puede modificarse.\n";
            break;
        case RES_LINEA_NO_EXISTE:
            cout << "ERROR: El repuesto '" << rep.nombre << "' no esta en la orden #" << o.id << ".\n";
            break;
        default:
            cout << "ERROR: No se pudo quitar el repuesto de la orden #" << o.id << ". " << textoResultado(r) << "\n";
    }
}

void menuCerrarOrden() {
    float manoObra;
    cout << "\n--- CERRAR ORDEN (escriba CANCELAR para abortar) ---\n";
    if (!verificarHay(contarOrdenesPorEstado("ABIERTA"), "No hay ordenes ABIERTAS.",
                      "Primero abra una orden.")) return;

    OrdenServicio o;
    if (!pedirOrden(&o)) return;
    if (strcmp(o.estado, "ABIERTA") != 0) {
        cout << "ERROR: La orden #" << o.id << " esta " << o.estado << " y no puede modificarse.\n";
        return;
    }
    if (!pedirDecimal("Mano de obra: ", &manoObra, false)) { cout << "Operacion cancelada.\n"; return; }
    if (!pedirConfirmacion("Cerrar esta orden? Ya no podra modificarse (S/N): ")) { cout << "Operacion cancelada.\n"; return; }

    Resultado r = cerrarOrden(o.id, manoObra);
    switch (r) {
        case RES_OK: {
            OrdenServicio cerrada;
            float totalRep = 0, total = manoObra;
            if (obtenerOrdenPorID(o.id, &cerrada) == RES_OK) calcularTotalesOrden(&cerrada, &totalRep, &total);
            printf("Orden #%d cerrada correctamente. Total: %.2f\n", o.id, total);
            break;
        }
        case RES_ORDEN_SIN_CONTENIDO:
            cout << "ERROR: La orden #" << o.id << " no tiene repuestos ni mano de obra y no puede cerrarse.\n";
            break;
        default:
            cout << "ERROR: No se pudo cerrar la orden #" << o.id << ". " << textoResultado(r) << "\n";
    }
}

void menuAnularOrden() {
    cout << "\n--- ANULAR ORDEN (escriba CANCELAR para abortar) ---\n";
    int anulables = contarOrdenesPorEstado("ABIERTA");
    int cerradas = contarOrdenesPorEstado("CERRADA");
    if (anulables >= 0 && cerradas >= 0) anulables += cerradas;
    else anulables = -1;
    if (!verificarHay(anulables, "No hay ordenes ABIERTAS ni CERRADAS que anular.",
                      "Las ordenes anuladas no pueden anularse otra vez.")) return;

    OrdenServicio o;
    if (!pedirOrden(&o)) return;
    if (strcmp(o.estado, "ABIERTA") != 0 && strcmp(o.estado, "CERRADA") != 0) {
        cout << "ERROR: La orden #" << o.id << " esta " << o.estado << " y no puede anularse.\n";
        return;
    }
    DetalleOrden lineas[MAX_LINEAS_ORDEN];
    int n = 0;
    obtenerLineasDeOrden(o.id, lineas, &n);
    int unidades = 0;
    for (int i = 0; i < n; i++) unidades += lineas[i].cantidad;

    cout << "La orden #" << o.id << " esta " << o.estado << ". Al anularla se devolveran "
         << unidades << " unidad(es) al inventario.\n";
    if (!pedirConfirmacion("Anular esta orden? (S/N): ")) { cout << "Operacion cancelada.\n"; return; }

    Resultado r = anularOrden(o.id);
    if (r == RES_OK) {
        OrdenServicio actual;
        if (obtenerOrdenPorID(o.id, &actual) == RES_OK)
            cout << "Orden #" << o.id << " anulada. Estado actual: " << actual.estado << ".\n";
        else cout << "Orden #" << o.id << " anulada.\n";
    } else {
        cout << "ERROR: No se pudo anular la orden #" << o.id << ". " << textoResultado(r) << "\n";
    }
}

void menuVerDetalleOrden() {
    cout << "\n--- DETALLE DE ORDEN ---\n";
    if (!verificarHay(contarOrdenesTotales(), "No hay ordenes registradas.",
                      "Primero abra una orden.")) return;
    OrdenServicio o;
    if (!pedirOrden(&o)) return;
    mostrarOrden(o);
}

void menuListarOrdenes() {
    cout << "\n--- LISTAR ORDENES ---\n";
    if (!verificarHay(contarOrdenesTotales(), "No hay ordenes registradas.",
                      "Primero abra una orden.")) return;

    cout << "1. Todas\n2. Abiertas\n3. Cerradas\n4. Anuladas\n5. Historial de un vehiculo\n0. Volver\n";
    int op = pedirOpcion("Seleccione una opcion: ", 5);
    if (op == 0) return;

    if (op == 1) mostrarOrdenesPaginadas(nullptr, 0);
    else if (op == 2) mostrarOrdenesPaginadas("ABIERTA", 0);
    else if (op == 3) mostrarOrdenesPaginadas("CERRADA", 0);
    else if (op == 4) mostrarOrdenesPaginadas("ANULADA", 0);
    else {
        char placa[10];
        if (!pedirDato("Placa del vehiculo: ", placa, sizeof(placa), DATO_PLACA)) { cout << "Operacion cancelada.\n"; return; }
        Vehiculo v;
        Resultado r = obtenerVehiculoPorPlaca(placa, &v);
        if (r == RES_VEHICULO_NO_EXISTE) { cout << "ERROR: No existe un vehiculo con la placa '" << placa << "'.\n"; return; }
        if (r != RES_OK) { cout << "ERROR: " << textoResultado(r) << "\n"; return; }
        mostrarOrdenesPaginadas(nullptr, v.id);
    }
}

void submenuOrdenes() {
    const char* opciones[] = {"1. Abrir orden", "2. Agregar repuesto a orden", "3. Quitar repuesto de orden",
                              "4. Cerrar orden", "5. Anular orden", "6. Ver detalle de orden",
                              "7. Listar ordenes", "0. Volver al menu principal"};
    int op = -1;
    while (op != 0 && !entradaTerminada) {
        char sub[80];
        snprintf(sub, sizeof(sub), "Ordenes: %d (abiertas: %d)", noNegativo(contarOrdenesTotales()),
                 noNegativo(contarOrdenesPorEstado("ABIERTA")));
        limpiarPantalla();
        imprimirCajaMenu("ORDENES DE SERVICIO", sub, opciones, 8);
        op = pedirOpcion("Seleccione una opcion: ", 7);
        if (entradaTerminada) break;
        if (op != 0) limpiarPantalla();
        switch (op) {
            case 1: menuAbrirOrden(); break;
            case 2: menuAgregarRepuestoAOrden(); break;
            case 3: menuQuitarRepuestoDeOrden(); break;
            case 4: menuCerrarOrden(); break;
            case 5: menuAnularOrden(); break;
            case 6: menuVerDetalleOrden(); break;
            case 7: menuListarOrdenes(); break;
        }
        if (op != 0) pausar();
    }
}

int main () {
    
    cout << "El programa funciona correctamente." << endl;
    return 0;
}