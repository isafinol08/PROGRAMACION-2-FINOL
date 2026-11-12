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

void obtenerFechaActual(char* destino) {          // destino: 11 bytes
    time_t ahora = time(nullptr);
    tm* t = localtime(&ahora);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    copiarTexto(destino, buffer, 11);
}

bool esFechaValida(const char* f) {
    if (strlen(f) != 10) return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) { if (f[i] != '-') return false; }
        else if (!isdigit((unsigned char)f[i])) return false;
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

bool esAnioValido(int anio) { return anio >= ANIO_MINIMO && anio <= ANIO_MAXIMO; }

// --------------------------------------------------------------------
//  3.2 Consultas sobre ordenes (se recorre ordenes.bin, un registro a la vez)
// --------------------------------------------------------------------
int contarOrdenesDeVehiculo(int idVehiculo) {                 // todas las ordenes, en cualquier estado
    ifstream f(RUTA_ORDENES, ios::binary);
    if (!f) return -1;
    f.seekg(sizeof(ArchivoHeader));
    OrdenServicio o;
    int n = 0;
    while (f.read((char*)&o, sizeof(o)))
        if (o.idVehiculo == idVehiculo) n++;
    return n;
}

int contarOrdenesAbiertasDeVehiculo(int idVehiculo) {
    ifstream f(RUTA_ORDENES, ios::binary);
    if (!f) return -1;
    f.seekg(sizeof(ArchivoHeader));
    OrdenServicio o;
    int n = 0;
    while (f.read((char*)&o, sizeof(o)))
        if (o.idVehiculo == idVehiculo && strcmp(o.estado, "ABIERTA") == 0) n++;
    return n;
}

int contarOrdenesAbiertasDeMecanico(int idMecanico) {
    ifstream f(RUTA_ORDENES, ios::binary);
    if (!f) return -1;
    f.seekg(sizeof(ArchivoHeader));
    OrdenServicio o;
    int n = 0;
    while (f.read((char*)&o, sizeof(o)))
        if (o.idMecanico == idMecanico && strcmp(o.estado, "ABIERTA") == 0) n++;
    return n;
}

// Retorna 1 si el repuesto aparece en alguna orden, 0 si no, -1 si hubo error de archivo
int repuestoEstaEnUso(int idRepuesto) {
    ifstream f(RUTA_ORDENES, ios::binary);
    if (!f) return -1;
    f.seekg(sizeof(ArchivoHeader));
    OrdenServicio o;
    while (f.read((char*)&o, sizeof(o)))
        for (int i = 0; i < o.numDetalles; i++)
            if (o.detalles[i].idRepuesto == idRepuesto) return 1;
    return 0;
}

// Consultas para decidir que opciones de menu tienen sentido (no abren menus ni imprimen)
bool hayVehiculos()         { return contarVehiculosActivos() > 0; }
bool hayMecanicos()         { return contarMecanicosTotales() > 0; }     // activos o dados de baja
bool hayMecanicosActivos()  { return contarMecanicosActivos() > 0; }
bool hayRepuestos()         { return contarRepuestosActivos() > 0; }
bool hayOrdenes()           { return contarOrdenesActivas() > 0; }
bool hayOrdenesAbiertas() {
    ifstream f(RUTA_ORDENES, ios::binary);
    if (!f) return false;
    f.seekg(sizeof(ArchivoHeader));
    OrdenServicio o;
    while (f.read((char*)&o, sizeof(o)))
        if (strcmp(o.estado, "ABIERTA") == 0) return true;
    return false;
}

// --------------------------------------------------------------------
//  3.3 Vehiculos
// --------------------------------------------------------------------

// Solo encuentra ACTIVOS. RES_ELIMINADO si fue eliminado (el vehiculo igual se entrega en *v).
// Si 'posicion' no es nullptr recibe la posicion en bytes donde se leyo (para depuracion).
Resultado obtenerVehiculoPorID(int id, Vehiculo* v, long* posicion = nullptr) {
    Resultado r = leerVehiculo(id, v, posicion);
    if (r != RES_OK) return r;
    if (v->eliminado) return RES_ELIMINADO;
    return RES_OK;
}

// Busca por placa exacta recorriendo el archivo (un registro en memoria a la vez)
Resultado obtenerVehiculoPorPlaca(const char* placa, Vehiculo* v) {
    char buscada[10];
    copiarTexto(buscada, placa, sizeof(buscada));
    aMayusculas(buscada);
    ifstream f(RUTA_VEHICULOS, ios::binary);
    if (!f) return RES_ERROR_ARCHIVO;
    f.seekg(sizeof(ArchivoHeader));
    Vehiculo actual;
    while (f.read((char*)&actual, sizeof(actual))) {
        if (!actual.eliminado && strcmp(actual.placa, buscada) == 0) {
            *v = actual;
            return RES_OK;
        }
    }
    return RES_NO_EXISTE;
}

// Coincidencia parcial del propietario. Retorna la cantidad (o -1 si hay error de archivo).
// Si hay mas de MAX_RESULTADOS, entrega los primeros y *hayMas = true.
int buscarVehiculosPorPropietario(const char* texto, Vehiculo* resultados, bool* hayMas) {
    *hayMas = false;
    ifstream f(RUTA_VEHICULOS, ios::binary);
    if (!f) return -1;
    f.seekg(sizeof(ArchivoHeader));
    Vehiculo actual;
    int n = 0;
    while (f.read((char*)&actual, sizeof(actual))) {
        if (actual.eliminado || !contieneTexto(actual.nombrePropietario, texto)) continue;
        if (n == MAX_RESULTADOS) { *hayMas = true; break; }
        resultados[n++] = actual;
    }
    return n;
}

int listarVehiculos(Vehiculo* resultados, bool* hayMas) {
    return buscarVehiculosPorPropietario("", resultados, hayMas);   // texto vacio = todos
}

Resultado registrarVehiculo(Vehiculo datos, int* idNuevo) {
    aMayusculas(datos.placa);
    aMayusculas(datos.cedulaPropietario);
    if (!esPlacaValida(datos.placa) || !esAnioValido(datos.anio) || datos.kilometraje < 0 ||
        esVacio(datos.marca) || esVacio(datos.modelo) || esVacio(datos.nombrePropietario) ||
        !esCedulaValida(datos.cedulaPropietario) || esVacio(datos.telefonoPropietario))
        return RES_DATO_INVALIDO;

    Vehiculo existente;
    Resultado r = obtenerVehiculoPorPlaca(datos.placa, &existente);   // placa unica entre activos
    if (r == RES_OK) return RES_DUPLICADO;
    if (r != RES_NO_EXISTE) return r;

    int id = guardarVehiculo(&datos);
    if (id == 0) return RES_ERROR_ARCHIVO;
    *idNuevo = id;
    return RES_OK;
}

// Actualiza marca, modelo, anio y datos del propietario (NO placa, km ni ID)
Resultado actualizarVehiculo(int id, Vehiculo nuevos) {
    Vehiculo v;
    Resultado r = obtenerVehiculoPorID(id, &v);
    if (r != RES_OK) return r;
    aMayusculas(nuevos.cedulaPropietario);
    if (esVacio(nuevos.marca) || esVacio(nuevos.modelo) || !esAnioValido(nuevos.anio) ||
        esVacio(nuevos.nombrePropietario) || !esCedulaValida(nuevos.cedulaPropietario) ||
        esVacio(nuevos.telefonoPropietario))
        return RES_DATO_INVALIDO;
    copiarTexto(v.marca, nuevos.marca, sizeof(v.marca));
    copiarTexto(v.modelo, nuevos.modelo, sizeof(v.modelo));
    v.anio = nuevos.anio;
    copiarTexto(v.nombrePropietario, nuevos.nombrePropietario, sizeof(v.nombrePropietario));
    copiarTexto(v.cedulaPropietario, nuevos.cedulaPropietario, sizeof(v.cedulaPropietario));
    copiarTexto(v.telefonoPropietario, nuevos.telefonoPropietario, sizeof(v.telefonoPropietario));
    return sobrescribirVehiculo(&v);
}

// Borrado logico. Rechazado si el vehiculo tiene ordenes en cualquier estado.
Resultado eliminarVehiculo(int id) {
    Vehiculo v;
    Resultado r = obtenerVehiculoPorID(id, &v);
    if (r != RES_OK) return r;
    int ordenes = contarOrdenesDeVehiculo(id);
    if (ordenes < 0) return RES_ERROR_ARCHIVO;
    if (ordenes > 0) return RES_VEHICULO_CON_ORDENES;
    return marcarVehiculoEliminado(id);
}

// --------------------------------------------------------------------
//  3.4 Mecanicos
// --------------------------------------------------------------------

// RES_OK = activo, RES_ELIMINADO = dado de baja (igual se entrega en *m), RES_NO_EXISTE
Resultado obtenerMecanicoPorID(int id, Mecanico* m) {
    Resultado r = leerMecanico(id, m);
    if (r != RES_OK) return r;
    if (m->eliminado) return RES_ELIMINADO;
    return RES_OK;
}

// Cedula unica entre TODOS los mecanicos (activos y dados de baja)
Resultado existeCedulaMecanico(const char* cedula) {
    ifstream f(RUTA_MECANICOS, ios::binary);
    if (!f) return RES_ERROR_ARCHIVO;
    f.seekg(sizeof(ArchivoHeader));
    Mecanico actual;
    while (f.read((char*)&actual, sizeof(actual)))
        if (strcmp(actual.cedula, cedula) == 0) return RES_OK;
    return RES_NO_EXISTE;
}

Resultado registrarMecanico(Mecanico datos, int* idNuevo) {
    aMayusculas(datos.cedula);
    aMayusculas(datos.especialidad);
    if (esVacio(datos.nombre) || !esCedulaValida(datos.cedula) ||
        !esEspecialidadValida(datos.especialidad) || !esFechaValida(datos.fechaIngreso))
        return RES_DATO_INVALIDO;
    Resultado r = existeCedulaMecanico(datos.cedula);
    if (r == RES_OK) return RES_DUPLICADO;
    if (r != RES_NO_EXISTE) return r;
    int id = guardarMecanico(&datos);
    if (id == 0) return RES_ERROR_ARCHIVO;
    *idNuevo = id;
    return RES_OK;
}

// Lista TODOS (activos y dados de baja). La presentacion los marca.
int listarMecanicos(Mecanico* resultados, bool* hayMas) {
    *hayMas = false;
    ifstream f(RUTA_MECANICOS, ios::binary);
    if (!f) return -1;
    f.seekg(sizeof(ArchivoHeader));
    Mecanico actual;
    int n = 0;
    while (f.read((char*)&actual, sizeof(actual))) {
        if (n == MAX_RESULTADOS) { *hayMas = true; break; }
        resultados[n++] = actual;
    }
    return n;
}

// Baja logica. Rechazada si tiene ordenes ABIERTAS.
Resultado darDeBajaMecanico(int id) {
    Mecanico m;
    Resultado r = obtenerMecanicoPorID(id, &m);
    if (r != RES_OK) return r;
    int abiertas = contarOrdenesAbiertasDeMecanico(id);
    if (abiertas < 0) return RES_ERROR_ARCHIVO;
    if (abiertas > 0) return RES_MECANICO_CON_ORDENES_ABIERTAS;
    return marcarMecanicoEliminado(id);
}

// --------------------------------------------------------------------
//  3.5 Repuestos
// --------------------------------------------------------------------
Resultado obtenerRepuestoPorID(int id, Repuesto* r, long* posicion = nullptr) {
    Resultado res = leerRepuesto(id, r, posicion);
    if (res != RES_OK) return res;
    if (r->eliminado) return RES_ELIMINADO;
    return RES_OK;
}

Resultado obtenerRepuestoPorCodigo(const char* codigo, Repuesto* r) {
    char buscado[20];
    copiarTexto(buscado, codigo, sizeof(buscado));
    aMayusculas(buscado);
    ifstream f(RUTA_REPUESTOS, ios::binary);
    if (!f) return RES_ERROR_ARCHIVO;
    f.seekg(sizeof(ArchivoHeader));
    Repuesto actual;
    while (f.read((char*)&actual, sizeof(actual))) {
        if (!actual.eliminado && strcmp(actual.codigo, buscado) == 0) {
            *r = actual;
            return RES_OK;
        }
    }
    return RES_NO_EXISTE;
}

// Busqueda parcial por codigo o nombre (texto vacio = todos). Solo activos.
int buscarRepuestosPorTexto(const char* texto, Repuesto* resultados, bool* hayMas) {
    *hayMas = false;
    ifstream f(RUTA_REPUESTOS, ios::binary);
    if (!f) return -1;
    f.seekg(sizeof(ArchivoHeader));
    Repuesto actual;
    int n = 0;
    while (f.read((char*)&actual, sizeof(actual))) {
        if (actual.eliminado) continue;
        if (!contieneTexto(actual.codigo, texto) && !contieneTexto(actual.nombre, texto)) continue;
        if (n == MAX_RESULTADOS) { *hayMas = true; break; }
        resultados[n++] = actual;
    }
    return n;
}

int listarRepuestos(Repuesto* resultados, bool* hayMas) {
    return buscarRepuestosPorTexto("", resultados, hayMas);
}

bool repuestoEnBajoStock(const Repuesto& r) { return r.stock <= r.stockMinimo; }

Resultado registrarRepuesto(Repuesto datos, int* idNuevo) {
    aMayusculas(datos.codigo);
    if (esVacio(datos.codigo) || esVacio(datos.nombre) || esVacio(datos.marca)) return RES_DATO_INVALIDO;
    if (datos.precio <= 0) return RES_PRECIO_INVALIDO;
    if (datos.stock < 0 || datos.stockMinimo < 0) return RES_CANTIDAD_INVALIDA;
    Repuesto existente;
    Resultado r = obtenerRepuestoPorCodigo(datos.codigo, &existente);   // codigo unico entre activos
    if (r == RES_OK) return RES_DUPLICADO;
    if (r != RES_NO_EXISTE) return r;
    int id = guardarRepuesto(&datos);
    if (id == 0) return RES_ERROR_ARCHIVO;
    *idNuevo = id;
    return RES_OK;
}

Resultado registrarEntradaRepuesto(int id, int cantidad) {
    if (cantidad <= 0) return RES_CANTIDAD_INVALIDA;
    Repuesto rep;
    Resultado r = obtenerRepuestoPorID(id, &rep);
    if (r != RES_OK) return r;
    if (rep.stock > INT_MAX - cantidad) return RES_CANTIDAD_INVALIDA;
    rep.stock += cantidad;
    return sobrescribirRepuesto(&rep);
}

Resultado cambiarPrecioRepuesto(int id, float nuevoPrecio) {
    if (nuevoPrecio <= 0) return RES_PRECIO_INVALIDO;
    Repuesto rep;
    Resultado r = obtenerRepuestoPorID(id, &rep);
    if (r != RES_OK) return r;
    rep.precio = nuevoPrecio;          // las ordenes ya registradas guardan su propio precio
    return sobrescribirRepuesto(&rep);
}

// Borrado logico. Rechazado si el repuesto aparece en el detalle de alguna orden.
Resultado eliminarRepuesto(int id) {
    Repuesto rep;
    Resultado r = obtenerRepuestoPorID(id, &rep);
    if (r != RES_OK) return r;
    int enUso = repuestoEstaEnUso(id);
    if (enUso < 0) return RES_ERROR_ARCHIVO;
    if (enUso == 1) return RES_REPUESTO_EN_USO;
    return marcarRepuestoEliminado(id);
}

// --------------------------------------------------------------------
//  3.6 Ordenes de servicio
// --------------------------------------------------------------------
      

int main () {    
    cout << "El sistema de gestion de taller ha sido inicializado." << endl;

    return 0;
}
