// ====================================================================
//  PROYECTO 1 - SISTEMA DE GESTION DE TALLER MECANICO
//  Programacion 2 - 2026-C
//
//  Todo el programa esta en UN SOLO archivo (sin archivos .h).
//
//  Compilar : g++ -std=c++11 -Wall -Wextra -o taller taller.cpp
//  Ejecutar : ./taller
//  Pruebas  : ./taller < pruebas/entrada.txt > pruebas/salida.txt
//
//  ORGANIZACION DEL ARCHIVO (de arriba hacia abajo):
//    1. Estructuras de datos
//    2. CAPA DE LOGICA        (sin cin / cout)
//    3. CAPA DE PRESENTACION  (cin / cout / menus)
//    4. main
// ====================================================================

#include <iostream>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <climits>
#include <ctime>
#include <string>   // SOLO se usa para leer texto temporalmente del teclado

#ifdef _WIN32
  #include <io.h>
  #define ES_TERMINAL() _isatty(0)
#else
  #include <unistd.h>
  #define ES_TERMINAL() isatty(0)
#endif

using namespace std;

// ====================================================================
//  CONSTANTES
// ====================================================================
const int CAPACIDAD_INICIAL          = 4;     // capacidad inicial de cada arreglo
const int CAPACIDAD_DETALLES_INICIAL = 2;     // capacidad inicial de los detalles de una orden
const int ANIO_MINIMO                = 1950;
const int ANIO_MAXIMO                = 2027;
const int MAX_ORDENES_ABIERTAS       = 3;     // por mecanico

// ====================================================================
//  1. ESTRUCTURAS DE DATOS
// ====================================================================
struct Taller {
    char nombre[100];
    char rif[20];
    char direccion[200];
    char telefono[20];
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
    char fechaRegistro[11];            // YYYY-MM-DD
};

struct Mecanico {
    int  id;
    char nombre[100];
    char cedula[20];
    char especialidad[20];             // MOTOR, FRENOS, ELECTRICIDAD, SUSPENSION, GENERAL
    char fechaIngreso[11];             // YYYY-MM-DD
};

struct Repuesto {
    int   id;
    char  codigo[20];
    char  nombre[100];
    char  marca[50];
    float precio;
    int   stock;
    int   stockMinimo;
    char  fechaRegistro[11];
};

// Una linea de la orden: que repuesto, cuantos y A QUE PRECIO se agrego
struct DetalleOrden {
    int   idRepuesto;
    int   cantidad;
    float precioUnitario;              // precio AL MOMENTO de agregarlo
};

struct OrdenServicio {
    int  id;
    int  idVehiculo;
    int  idMecanico;
    char fechaApertura[11];
    char fechaCierre[11];              // vacia mientras este ABIERTA
    char estado[12];                   // "ABIERTA", "CERRADA" o "ANULADA"
    char descripcionFalla[200];
    int  kilometrajeIngreso;
    float manoObra;

    // Arreglo dinamico PROPIO de cada orden (memoria anidada)
    DetalleOrden* detalles;
    int numDetalles;
    int capacidadDetalles;
};

struct SistemaTaller {
    Taller taller;

    Vehiculo*      vehiculos; int numVehiculos; int capacidadVehiculos;
    Mecanico*      mecanicos; int numMecanicos; int capacidadMecanicos;
    Repuesto*      repuestos; int numRepuestos; int capacidadRepuestos;
    OrdenServicio* ordenes;   int numOrdenes;   int capacidadOrdenes;

    int siguienteIdVehiculo;
    int siguienteIdMecanico;
    int siguienteIdRepuesto;
    int siguienteIdOrden;
};

// Codigos de resultado: las funciones de logica que pueden fallar por
// varios motivos devuelven uno de estos valores. La capa de presentacion
// los traduce a mensajes de error para el usuario.
enum Resultado {
    RES_OK,
    RES_DATO_INVALIDO,
    RES_DUPLICADO,
    RES_VEHICULO_NO_EXISTE,
    RES_MECANICO_NO_EXISTE,
    RES_REPUESTO_NO_EXISTE,
    RES_ORDEN_NO_EXISTE,
    RES_CANTIDAD_INVALIDA,
    RES_PRECIO_INVALIDO,
    RES_VEHICULO_CON_ORDEN_ABIERTA,
    RES_MECANICO_LLENO,
    RES_KM_MENOR,
    RES_ORDEN_NO_ABIERTA,
    RES_ORDEN_NO_ANULABLE,
    RES_STOCK_INSUFICIENTE,
    RES_LINEA_NO_EXISTE,
    RES_ORDEN_SIN_CONTENIDO,
    RES_REPUESTO_EN_USO
};


// ====================================================================
// ====================================================================
//  2. CAPA DE LOGICA  (aqui NO se usa cin ni cout)
// ====================================================================
// ====================================================================

// --------------------------------------------------------------------
//  2.1 Utilidades generales
// --------------------------------------------------------------------

// Copia 'origen' en 'destino' sin pasarse del tamanio del arreglo.
void copiarTexto(char* destino, const char* origen, int tam) {
    strncpy(destino, origen, tam - 1);
    destino[tam - 1] = '\0';
}

// true si el texto esta vacio o solo tiene espacios
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

// Escribe la fecha de hoy como YYYY-MM-DD (destino debe tener 11 bytes)
void obtenerFechaActual(char* destino) {
    time_t ahora = time(nullptr);
    tm* t = localtime(&ahora);
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%04d-%02d-%02d", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);
    copiarTexto(destino, buffer, 11);
}

// Formato YYYY-MM-DD con valores reales (meses 1-12, dias segun el mes, bisiestos)
bool esFechaValida(const char* f) {
    if (strlen(f) != 10) return false;
    for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
            if (f[i] != '-') return false;
        } else if (!isdigit((unsigned char)f[i])) {
            return false;
        }
    }
    int anio = atoi(f);        // atoi se detiene en el primer '-'
    int mes  = atoi(f + 5);
    int dia  = atoi(f + 8);
    if (anio < 1900 || mes < 1 || mes > 12 || dia < 1) return false;
    int diasDelMes[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool bisiesto = (anio % 4 == 0 && anio % 100 != 0) || (anio % 400 == 0);
    int maxDia = diasDelMes[mes - 1];
    if (mes == 2 && bisiesto) maxDia = 29;
    return dia <= maxDia;
}

// Placa: alfanumerica, 6 o 7 caracteres
bool esPlacaValida(const char* p) {
    int n = (int)strlen(p);
    if (n < 6 || n > 7) return false;
    for (int i = 0; i < n; i++)
        if (!isalnum((unsigned char)p[i])) return false;
    return true;
}

// Cedula: solo digitos, con prefijo opcional V-, E- o J-
bool esCedulaValida(const char* c) {
    int i = 0;
    if (strlen(c) >= 2 && c[1] == '-') {
        char pref = (char)toupper((unsigned char)c[0]);
        if (pref == 'V' || pref == 'E' || pref == 'J') i = 2;
    }
    if (c[i] == '\0') return false;          // no hay digitos
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

// --------------------------------------------------------------------
//  2.2 Inicializacion, redimensionamiento y liberacion de memoria
// --------------------------------------------------------------------

// Reserva cada arreglo con capacidad 4, contadores en 0 e IDs desde 1.
void inicializarSistema(SistemaTaller* s, Taller datosTaller) {
    s->taller = datosTaller;

    s->capacidadVehiculos = CAPACIDAD_INICIAL;
    s->numVehiculos = 0;
    s->vehiculos = new Vehiculo[CAPACIDAD_INICIAL]();   // los () ponen todo en cero

    s->capacidadMecanicos = CAPACIDAD_INICIAL;
    s->numMecanicos = 0;
    s->mecanicos = new Mecanico[CAPACIDAD_INICIAL]();

    s->capacidadRepuestos = CAPACIDAD_INICIAL;
    s->numRepuestos = 0;
    s->repuestos = new Repuesto[CAPACIDAD_INICIAL]();

    s->capacidadOrdenes = CAPACIDAD_INICIAL;
    s->numOrdenes = 0;
    s->ordenes = new OrdenServicio[CAPACIDAD_INICIAL]();

    s->siguienteIdVehiculo = 1;
    s->siguienteIdMecanico = 1;
    s->siguienteIdRepuesto = 1;
    s->siguienteIdOrden = 1;
}

// Libera TODO: primero los detalles de cada orden (memoria anidada),
// y despues los arreglos principales.
void liberarSistema(SistemaTaller* s) {
    for (int i = 0; i < s->numOrdenes; i++) {
        delete[] s->ordenes[i].detalles;     // arreglo propio de cada orden
        s->ordenes[i].detalles = nullptr;
    }
    delete[] s->ordenes;
    delete[] s->vehiculos;
    delete[] s->mecanicos;
    delete[] s->repuestos;
    s->ordenes = nullptr;
    s->vehiculos = nullptr;
    s->mecanicos = nullptr;
    s->repuestos = nullptr;
    s->numOrdenes = s->numVehiculos = s->numMecanicos = s->numRepuestos = 0;
    s->capacidadOrdenes = s->capacidadVehiculos = s->capacidadMecanicos = s->capacidadRepuestos = 0;
}

// Cuando un arreglo se llena (num == capacidad) se crea otro del doble de
// tamanio, se copian los elementos, se libera el viejo y se apunta al nuevo.
void redimensionarVehiculos(SistemaTaller* s) {
    int nuevaCapacidad = s->capacidadVehiculos * 2;
    Vehiculo* nuevo = new Vehiculo[nuevaCapacidad]();
    for (int i = 0; i < s->numVehiculos; i++) nuevo[i] = s->vehiculos[i];
    delete[] s->vehiculos;
    s->vehiculos = nuevo;
    s->capacidadVehiculos = nuevaCapacidad;
}

void redimensionarMecanicos(SistemaTaller* s) {
    int nuevaCapacidad = s->capacidadMecanicos * 2;
    Mecanico* nuevo = new Mecanico[nuevaCapacidad]();
    for (int i = 0; i < s->numMecanicos; i++) nuevo[i] = s->mecanicos[i];
    delete[] s->mecanicos;
    s->mecanicos = nuevo;
    s->capacidadMecanicos = nuevaCapacidad;
}

void redimensionarRepuestos(SistemaTaller* s) {
    int nuevaCapacidad = s->capacidadRepuestos * 2;
    Repuesto* nuevo = new Repuesto[nuevaCapacidad]();
    for (int i = 0; i < s->numRepuestos; i++) nuevo[i] = s->repuestos[i];
    delete[] s->repuestos;
    s->repuestos = nuevo;
    s->capacidadRepuestos = nuevaCapacidad;
}

// OJO: al copiar una OrdenServicio (nuevo[i] = s->ordenes[i]) se copia el
// PUNTERO 'detalles', no el arreglo de detalles. El bloque de detalles NO se
// mueve ni se duplica: la orden nueva apunta al mismo bloque que la vieja.
// Como OrdenServicio no tiene destructor, delete[] sobre el arreglo viejo
// libera solo las ordenes, no sus detalles. Asi los detalles sobreviven.
void redimensionarOrdenes(SistemaTaller* s) {
    int nuevaCapacidad = s->capacidadOrdenes * 2;
    OrdenServicio* nuevo = new OrdenServicio[nuevaCapacidad]();
    for (int i = 0; i < s->numOrdenes; i++) nuevo[i] = s->ordenes[i];
    delete[] s->ordenes;
    s->ordenes = nuevo;
    s->capacidadOrdenes = nuevaCapacidad;
}

// Redimensiona el arreglo de detalles de UNA orden.
void redimensionarDetalles(OrdenServicio* o) {
    int nuevaCapacidad = o->capacidadDetalles * 2;
    DetalleOrden* nuevo = new DetalleOrden[nuevaCapacidad]();
    for (int i = 0; i < o->numDetalles; i++) nuevo[i] = o->detalles[i];
    delete[] o->detalles;
    o->detalles = nuevo;
    o->capacidadDetalles = nuevaCapacidad;
}

// --------------------------------------------------------------------
//  2.3 Consultas sobre ordenes (las usan los otros modulos)
// --------------------------------------------------------------------
OrdenServicio* buscarOrdenPorID(SistemaTaller* s, int id) {
    for (int i = 0; i < s->numOrdenes; i++)
        if (s->ordenes[i].id == id) return &s->ordenes[i];
    return nullptr;
}

// Orden ABIERTA de un vehiculo (solo puede haber una), o nullptr
OrdenServicio* buscarOrdenAbiertaDeVehiculo(SistemaTaller* s, int idVehiculo) {
    for (int i = 0; i < s->numOrdenes; i++)
        if (s->ordenes[i].idVehiculo == idVehiculo && strcmp(s->ordenes[i].estado, "ABIERTA") == 0)
            return &s->ordenes[i];
    return nullptr;
}

// Cantidad de ordenes de un vehiculo en cualquier estado
int contarOrdenesDeVehiculo(SistemaTaller* s, int idVehiculo) {
    int n = 0;
    for (int i = 0; i < s->numOrdenes; i++)
        if (s->ordenes[i].idVehiculo == idVehiculo) n++;
    return n;
}

int contarOrdenesAbiertasDeMecanico(SistemaTaller* s, int idMecanico) {
    int n = 0;
    for (int i = 0; i < s->numOrdenes; i++)
        if (s->ordenes[i].idMecanico == idMecanico && strcmp(s->ordenes[i].estado, "ABIERTA") == 0) n++;
    return n;
}

// Linea de la orden que corresponde a ese repuesto, o nullptr
DetalleOrden* buscarDetalleEnOrden(OrdenServicio* o, int idRepuesto) {
    for (int i = 0; i < o->numDetalles; i++)
        if (o->detalles[i].idRepuesto == idRepuesto) return &o->detalles[i];
    return nullptr;
}

// --------------------------------------------------------------------
//  2.4 VEHICULOS
// --------------------------------------------------------------------

Vehiculo* buscarVehiculoPorID(SistemaTaller* s, int id) {
    for (int i = 0; i < s->numVehiculos; i++)
        if (s->vehiculos[i].id == id) return &s->vehiculos[i];
    return nullptr;
}

Vehiculo* buscarVehiculoPorPlaca(SistemaTaller* s, const char* placa) {
    for (int i = 0; i < s->numVehiculos; i++)
        if (strcmp(s->vehiculos[i].placa, placa) == 0) return &s->vehiculos[i];
    return nullptr;
}

// Agrega un vehiculo. Valida todo ANTES de modificar nada.
// Retorna puntero al vehiculo creado o nullptr si falla alguna validacion.
// OJO: el puntero apunta DENTRO de s->vehiculos; si luego se agregan mas
// vehiculos y el arreglo se redimensiona, ese puntero deja de ser valido.
Vehiculo* agregarVehiculo(SistemaTaller* s, Vehiculo datos) {
    if (esVacio(datos.marca) || esVacio(datos.modelo)) return nullptr;
    if (esVacio(datos.nombrePropietario)) return nullptr;
    if (!esPlacaValida(datos.placa)) return nullptr;
    if (!esCedulaValida(datos.cedulaPropietario)) return nullptr;
    if (!esAnioValido(datos.anio)) return nullptr;
    if (datos.kilometraje < 0) return nullptr;
    if (buscarVehiculoPorPlaca(s, datos.placa) != nullptr) return nullptr;   // placa duplicada

    if (s->numVehiculos == s->capacidadVehiculos) redimensionarVehiculos(s);

    Vehiculo* nuevo = &s->vehiculos[s->numVehiculos];
    *nuevo = datos;                                  // copia todos los campos
    nuevo->id = s->siguienteIdVehiculo;
    s->siguienteIdVehiculo++;
    obtenerFechaActual(nuevo->fechaRegistro);
    s->numVehiculos++;
    return nuevo;
}

// Vehiculos cuyo propietario contiene la subcadena. El llamador hace delete[].
// Si no hay coincidencias retorna nullptr (y *cantidad = 0).
Vehiculo** buscarVehiculosPorPropietario(SistemaTaller* s, const char* subcadena, int* cantidad) {
    *cantidad = 0;
    for (int i = 0; i < s->numVehiculos; i++)
        if (contieneTexto(s->vehiculos[i].nombrePropietario, subcadena)) (*cantidad)++;
    if (*cantidad == 0) return nullptr;

    Vehiculo** resultado = new Vehiculo*[*cantidad];
    int k = 0;
    for (int i = 0; i < s->numVehiculos; i++)
        if (contieneTexto(s->vehiculos[i].nombrePropietario, subcadena))
            resultado[k++] = &s->vehiculos[i];
    return resultado;
}

// Arreglo de punteros a todos los vehiculos. El llamador hace delete[].
Vehiculo** listarVehiculos(SistemaTaller* s, int* cantidad) {
    *cantidad = s->numVehiculos;
    if (s->numVehiculos == 0) return nullptr;
    Vehiculo** resultado = new Vehiculo*[s->numVehiculos];
    for (int i = 0; i < s->numVehiculos; i++) resultado[i] = &s->vehiculos[i];
    return resultado;
}

// Actualiza marca, modelo, anio y datos del propietario.
// No toca id, placa, kilometraje ni fecha de registro.
bool actualizarVehiculo(SistemaTaller* s, int id, Vehiculo datos) {
    Vehiculo* v = buscarVehiculoPorID(s, id);
    if (v == nullptr) return false;
    if (esVacio(datos.marca) || esVacio(datos.modelo)) return false;
    if (esVacio(datos.nombrePropietario)) return false;
    if (!esCedulaValida(datos.cedulaPropietario)) return false;
    if (!esAnioValido(datos.anio)) return false;

    copiarTexto(v->marca, datos.marca, sizeof(v->marca));
    copiarTexto(v->modelo, datos.modelo, sizeof(v->modelo));
    v->anio = datos.anio;
    copiarTexto(v->nombrePropietario, datos.nombrePropietario, sizeof(v->nombrePropietario));
    copiarTexto(v->cedulaPropietario, datos.cedulaPropietario, sizeof(v->cedulaPropietario));
    copiarTexto(v->telefonoPropietario, datos.telefonoPropietario, sizeof(v->telefonoPropietario));
    return true;
}

// Elimina fisicamente (desplaza los siguientes una posicion a la izquierda).
// Falla si no existe o si tiene ordenes en cualquier estado.
bool eliminarVehiculo(SistemaTaller* s, int id) {
    int pos = -1;
    for (int i = 0; i < s->numVehiculos; i++)
        if (s->vehiculos[i].id == id) { pos = i; break; }
    if (pos == -1) return false;
    if (contarOrdenesDeVehiculo(s, id) > 0) return false;

    for (int i = pos; i < s->numVehiculos - 1; i++)
        s->vehiculos[i] = s->vehiculos[i + 1];
    s->numVehiculos--;
    return true;
}

// --------------------------------------------------------------------
//  2.5 MECANICOS
//  Patron: las funciones que crean retornan el puntero a la entidad
//  (o nullptr) y dejan el MOTIVO del fallo en *motivo.
// --------------------------------------------------------------------

Mecanico* buscarMecanicoPorID(SistemaTaller* s, int id) {
    for (int i = 0; i < s->numMecanicos; i++)
        if (s->mecanicos[i].id == id) return &s->mecanicos[i];
    return nullptr;
}

Mecanico* buscarMecanicoPorCedula(SistemaTaller* s, const char* cedula) {
    for (int i = 0; i < s->numMecanicos; i++)
        if (strcmp(s->mecanicos[i].cedula, cedula) == 0) return &s->mecanicos[i];
    return nullptr;
}

// Registra un mecanico. Cedula unica, especialidad permitida, fecha valida.
Mecanico* agregarMecanico(SistemaTaller* s, Mecanico datos, Resultado* motivo) {
    *motivo = RES_OK;
    if (esVacio(datos.nombre) || !esCedulaValida(datos.cedula) ||
        !esEspecialidadValida(datos.especialidad) || !esFechaValida(datos.fechaIngreso)) {
        *motivo = RES_DATO_INVALIDO;
        return nullptr;
    }
    if (buscarMecanicoPorCedula(s, datos.cedula) != nullptr) {
        *motivo = RES_DUPLICADO;
        return nullptr;
    }
    if (s->numMecanicos == s->capacidadMecanicos) redimensionarMecanicos(s);

    Mecanico* nuevo = &s->mecanicos[s->numMecanicos];
    *nuevo = datos;
    nuevo->id = s->siguienteIdMecanico;
    s->siguienteIdMecanico++;
    s->numMecanicos++;
    return nuevo;
}

// Arreglo de punteros a todos los mecanicos. El llamador hace delete[].
Mecanico** listarMecanicos(SistemaTaller* s, int* cantidad) {
    *cantidad = s->numMecanicos;
    if (s->numMecanicos == 0) return nullptr;
    Mecanico** resultado = new Mecanico*[s->numMecanicos];
    for (int i = 0; i < s->numMecanicos; i++) resultado[i] = &s->mecanicos[i];
    return resultado;
}

// --------------------------------------------------------------------
//  2.6 REPUESTOS (inventario)
// --------------------------------------------------------------------

Repuesto* buscarRepuestoPorID(SistemaTaller* s, int id) {
    for (int i = 0; i < s->numRepuestos; i++)
        if (s->repuestos[i].id == id) return &s->repuestos[i];
    return nullptr;
}

Repuesto* buscarRepuestoPorCodigo(SistemaTaller* s, const char* codigo) {
    for (int i = 0; i < s->numRepuestos; i++)
        if (strcmp(s->repuestos[i].codigo, codigo) == 0) return &s->repuestos[i];
    return nullptr;
}

// Repuestos cuyo codigo o nombre contiene el texto. El llamador hace delete[].
Repuesto** buscarRepuestosPorTexto(SistemaTaller* s, const char* texto, int* cantidad) {
    *cantidad = 0;
    for (int i = 0; i < s->numRepuestos; i++)
        if (contieneTexto(s->repuestos[i].codigo, texto) || contieneTexto(s->repuestos[i].nombre, texto))
            (*cantidad)++;
    if (*cantidad == 0) return nullptr;

    Repuesto** resultado = new Repuesto*[*cantidad];
    int k = 0;
    for (int i = 0; i < s->numRepuestos; i++)
        if (contieneTexto(s->repuestos[i].codigo, texto) || contieneTexto(s->repuestos[i].nombre, texto))
            resultado[k++] = &s->repuestos[i];
    return resultado;
}

// Registra un repuesto. Codigo unico, precio > 0, stock >= 0, minimo >= 0.
Repuesto* agregarRepuesto(SistemaTaller* s, Repuesto datos, Resultado* motivo) {
    *motivo = RES_OK;
    if (esVacio(datos.codigo) || esVacio(datos.nombre) || esVacio(datos.marca) ||
        datos.precio <= 0 || datos.stock < 0 || datos.stockMinimo < 0) {
        *motivo = RES_DATO_INVALIDO;
        return nullptr;
    }
    if (buscarRepuestoPorCodigo(s, datos.codigo) != nullptr) {
        *motivo = RES_DUPLICADO;
        return nullptr;
    }
    if (s->numRepuestos == s->capacidadRepuestos) redimensionarRepuestos(s);

    Repuesto* nuevo = &s->repuestos[s->numRepuestos];
    *nuevo = datos;
    nuevo->id = s->siguienteIdRepuesto;
    s->siguienteIdRepuesto++;
    obtenerFechaActual(nuevo->fechaRegistro);
    s->numRepuestos++;
    return nuevo;
}

// Suma unidades al stock (cantidad > 0).
Resultado registrarEntradaRepuesto(SistemaTaller* s, int idRepuesto, int cantidad) {
    Repuesto* r = buscarRepuestoPorID(s, idRepuesto);
    if (r == nullptr) return RES_REPUESTO_NO_EXISTE;
    if (cantidad <= 0) return RES_CANTIDAD_INVALIDA;
    r->stock += cantidad;
    return RES_OK;
}

// Arreglo de punteros a todos los repuestos. El llamador hace delete[].
Repuesto** listarRepuestos(SistemaTaller* s, int* cantidad) {
    *cantidad = s->numRepuestos;
    if (s->numRepuestos == 0) return nullptr;
    Repuesto** resultado = new Repuesto*[s->numRepuestos];
    for (int i = 0; i < s->numRepuestos; i++) resultado[i] = &s->repuestos[i];
    return resultado;
}

bool repuestoEnBajoStock(const Repuesto* r) {
    return r->stock <= r->stockMinimo;
}

// Cambia el precio vigente. NO afecta ordenes ya registradas, porque cada
// linea de orden guarda su propio precioUnitario.
Resultado cambiarPrecioRepuesto(SistemaTaller* s, int idRepuesto, float nuevoPrecio) {
    Repuesto* r = buscarRepuestoPorID(s, idRepuesto);
    if (r == nullptr) return RES_REPUESTO_NO_EXISTE;
    if (nuevoPrecio <= 0) return RES_PRECIO_INVALIDO;
    r->precio = nuevoPrecio;
    return RES_OK;
}

// true si el repuesto aparece en alguna linea de cualquier orden
// (incluidas las ANULADAS, que conservan sus lineas como historial)
bool repuestoEstaEnUso(SistemaTaller* s, int idRepuesto) {
    for (int i = 0; i < s->numOrdenes; i++)
        if (buscarDetalleEnOrden(&s->ordenes[i], idRepuesto) != nullptr) return true;
    return false;
}

// Elimina fisicamente el repuesto solo si no esta en ninguna orden.
Resultado eliminarRepuesto(SistemaTaller* s, int idRepuesto) {
    int pos = -1;
    for (int i = 0; i < s->numRepuestos; i++)
        if (s->repuestos[i].id == idRepuesto) { pos = i; break; }
    if (pos == -1) return RES_REPUESTO_NO_EXISTE;
    if (repuestoEstaEnUso(s, idRepuesto)) return RES_REPUESTO_EN_USO;

    for (int i = pos; i < s->numRepuestos - 1; i++)
        s->repuestos[i] = s->repuestos[i + 1];
    s->numRepuestos--;
    return RES_OK;
}

// --------------------------------------------------------------------
//  2.7 ORDENES DE SERVICIO
// --------------------------------------------------------------------

// Total de repuestos = suma de cantidad * precioUnitario de cada linea
float calcularTotalRepuestos(const OrdenServicio* o) {
    float total = 0;
    for (int i = 0; i < o->numDetalles; i++)
        total += o->detalles[i].cantidad * o->detalles[i].precioUnitario;
    return total;
}

// El total NO se guarda: se calcula siempre a partir de las lineas.
float calcularTotalOrden(const OrdenServicio* o) {
    return o->manoObra + calcularTotalRepuestos(o);
}

// Abre una orden. Valida todo antes de modificar nada.
// Retorna la orden creada o nullptr con el motivo en *motivo.
OrdenServicio* abrirOrden(SistemaTaller* s, int idVehiculo, int idMecanico,
                          int kmIngreso, const char* falla, Resultado* motivo) {
    *motivo = RES_OK;

    Vehiculo* v = buscarVehiculoPorID(s, idVehiculo);
    if (v == nullptr) { *motivo = RES_VEHICULO_NO_EXISTE; return nullptr; }
    Mecanico* m = buscarMecanicoPorID(s, idMecanico);
    if (m == nullptr) { *motivo = RES_MECANICO_NO_EXISTE; return nullptr; }
    if (esVacio(falla)) { *motivo = RES_DATO_INVALIDO; return nullptr; }

    if (buscarOrdenAbiertaDeVehiculo(s, idVehiculo) != nullptr) {
        *motivo = RES_VEHICULO_CON_ORDEN_ABIERTA;
        return nullptr;
    }
    if (contarOrdenesAbiertasDeMecanico(s, idMecanico) >= MAX_ORDENES_ABIERTAS) {
        *motivo = RES_MECANICO_LLENO;
        return nullptr;
    }
    if (kmIngreso < v->kilometraje) { *motivo = RES_KM_MENOR; return nullptr; }

    if (s->numOrdenes == s->capacidadOrdenes) redimensionarOrdenes(s);

    OrdenServicio* o = &s->ordenes[s->numOrdenes];
    o->id = s->siguienteIdOrden;
    s->siguienteIdOrden++;
    o->idVehiculo = idVehiculo;
    o->idMecanico = idMecanico;
    obtenerFechaActual(o->fechaApertura);
    o->fechaCierre[0] = '\0';
    copiarTexto(o->estado, "ABIERTA", sizeof(o->estado));
    copiarTexto(o->descripcionFalla, falla, sizeof(o->descripcionFalla));
    o->kilometrajeIngreso = kmIngreso;
    o->manoObra = 0;

    // Arreglo de detalles propio de esta orden: capacidad inicial 2
    o->capacidadDetalles = CAPACIDAD_DETALLES_INICIAL;
    o->numDetalles = 0;
    o->detalles = new DetalleOrden[o->capacidadDetalles]();

    s->numOrdenes++;
    v->kilometraje = kmIngreso;      // al abrir se actualiza el km del vehiculo
    return o;
}

// Agrega (o acumula) un repuesto en una orden ABIERTA y descuenta el stock.
Resultado agregarRepuestoAOrden(SistemaTaller* s, int idOrden, int idRepuesto, int cantidad) {
    OrdenServicio* o = buscarOrdenPorID(s, idOrden);
    if (o == nullptr) return RES_ORDEN_NO_EXISTE;
    if (strcmp(o->estado, "ABIERTA") != 0) return RES_ORDEN_NO_ABIERTA;
    Repuesto* r = buscarRepuestoPorID(s, idRepuesto);
    if (r == nullptr) return RES_REPUESTO_NO_EXISTE;
    if (cantidad <= 0) return RES_CANTIDAD_INVALIDA;
    if (r->stock < cantidad) return RES_STOCK_INSUFICIENTE;

    DetalleOrden* linea = buscarDetalleEnOrden(o, idRepuesto);
    if (linea != nullptr) {
        // Ya estaba en la orden: se acumula en la misma linea
        // (se conserva el precio con el que se creo la linea).
        linea->cantidad += cantidad;
    } else {
        if (o->numDetalles == o->capacidadDetalles) redimensionarDetalles(o);
        DetalleOrden* nueva = &o->detalles[o->numDetalles];
        nueva->idRepuesto = idRepuesto;
        nueva->cantidad = cantidad;
        nueva->precioUnitario = r->precio;      // precio vigente en este momento
        o->numDetalles++;
    }
    r->stock -= cantidad;
    return RES_OK;
}

// Quita la LINEA COMPLETA de un repuesto y devuelve sus unidades al inventario.
Resultado quitarRepuestoDeOrden(SistemaTaller* s, int idOrden, int idRepuesto) {
    OrdenServicio* o = buscarOrdenPorID(s, idOrden);
    if (o == nullptr) return RES_ORDEN_NO_EXISTE;
    if (strcmp(o->estado, "ABIERTA") != 0) return RES_ORDEN_NO_ABIERTA;

    int pos = -1;
    for (int i = 0; i < o->numDetalles; i++)
        if (o->detalles[i].idRepuesto == idRepuesto) { pos = i; break; }
    if (pos == -1) return RES_LINEA_NO_EXISTE;

    Repuesto* r = buscarRepuestoPorID(s, idRepuesto);
    if (r != nullptr) r->stock += o->detalles[pos].cantidad;     // devolver al inventario

    for (int i = pos; i < o->numDetalles - 1; i++)
        o->detalles[i] = o->detalles[i + 1];
    o->numDetalles--;
    return RES_OK;
}

// Cierra una orden ABIERTA registrando la mano de obra.
Resultado cerrarOrden(SistemaTaller* s, int idOrden, float manoObra) {
    OrdenServicio* o = buscarOrdenPorID(s, idOrden);
    if (o == nullptr) return RES_ORDEN_NO_EXISTE;
    if (strcmp(o->estado, "ABIERTA") != 0) return RES_ORDEN_NO_ABIERTA;
    if (manoObra < 0) return RES_DATO_INVALIDO;
    if (o->numDetalles == 0 && manoObra <= 0) return RES_ORDEN_SIN_CONTENIDO;

    o->manoObra = manoObra;
    copiarTexto(o->estado, "CERRADA", sizeof(o->estado));
    obtenerFechaActual(o->fechaCierre);
    return RES_OK;
}

// Anula una orden ABIERTA o CERRADA y devuelve TODAS sus unidades al inventario.
// Las lineas se conservan en la orden como historial.
Resultado anularOrden(SistemaTaller* s, int idOrden) {
    OrdenServicio* o = buscarOrdenPorID(s, idOrden);
    if (o == nullptr) return RES_ORDEN_NO_EXISTE;
    if (strcmp(o->estado, "ABIERTA") != 0 && strcmp(o->estado, "CERRADA") != 0)
        return RES_ORDEN_NO_ANULABLE;

    for (int i = 0; i < o->numDetalles; i++) {
        Repuesto* r = buscarRepuestoPorID(s, o->detalles[i].idRepuesto);
        if (r != nullptr) r->stock += o->detalles[i].cantidad;
    }
    copiarTexto(o->estado, "ANULADA", sizeof(o->estado));
    return RES_OK;
}

// Ordenes por estado ("ABIERTA", "CERRADA", "ANULADA"). Con estado == nullptr
// retorna todas. El llamador hace delete[] (solo del arreglo de punteros).
OrdenServicio** listarOrdenesPorEstado(SistemaTaller* s, const char* estado, int* cantidad) {
    *cantidad = 0;
    for (int i = 0; i < s->numOrdenes; i++)
        if (estado == nullptr || strcmp(s->ordenes[i].estado, estado) == 0) (*cantidad)++;
    if (*cantidad == 0) return nullptr;

    OrdenServicio** resultado = new OrdenServicio*[*cantidad];
    int k = 0;
    for (int i = 0; i < s->numOrdenes; i++)
        if (estado == nullptr || strcmp(s->ordenes[i].estado, estado) == 0)
            resultado[k++] = &s->ordenes[i];
    return resultado;
}

// Historial de ordenes de un vehiculo. El llamador hace delete[].
OrdenServicio** listarOrdenesPorVehiculo(SistemaTaller* s, int idVehiculo, int* cantidad) {
    *cantidad = contarOrdenesDeVehiculo(s, idVehiculo);
    if (*cantidad == 0) return nullptr;

    OrdenServicio** resultado = new OrdenServicio*[*cantidad];
    int k = 0;
    for (int i = 0; i < s->numOrdenes; i++)
        if (s->ordenes[i].idVehiculo == idVehiculo) resultado[k++] = &s->ordenes[i];
    return resultado;
}


// ====================================================================
// ====================================================================
//  3. CAPA DE PRESENTACION  (cin / cout / menus)
//     Estas funciones NO modifican datos directamente: siempre llaman
//     a las funciones de la capa de logica.
// ====================================================================