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



int main () {
    
    cout << "El programa funciona correctamente." << endl;
    return 0;
}