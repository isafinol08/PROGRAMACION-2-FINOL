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
// ====================================================================

// --------------------------------------------------------------------
//  3.1 Lectura de datos del usuario
// --------------------------------------------------------------------

bool entradaTerminada = false;      // se pone en true si se acaba la entrada (Ctrl+D / fin de archivo)

enum TipoDato { DATO_TEXTO, DATO_PLACA, DATO_CEDULA, DATO_ESPECIALIDAD, DATO_FECHA, DATO_CODIGO };

// Quita espacios (y saltos de linea \r) al inicio y al final
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
        if (!linea.empty() && linea[0] == '#') {        // comentario: se imprime y se sigue
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
// 'tam' es el tamanio del arreglo destino.
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

// Pide un entero entre minimo y maximo. Si el usuario escribe texto, avisa y vuelve a pedir.
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
            if (texto[i] == ',') texto[i] = '.';          // acepta coma decimal
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
    int op = 0;
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
        op = (int)n;
        return op;
    }
}

// Limpia la terminal. Solo actua cuando el programa se usa de forma
// interactiva; si la entrada viene de un archivo (./taller < entrada.txt)
// no hace nada, para que salida.txt quede completo.
void limpiarPantalla() {
    if (!ES_TERMINAL()) return;
#ifdef _WIN32
    int r = system("cls");
#else
    int r = system("clear");
#endif
    (void)r;
}

// Espera un ENTER para que el usuario alcance a leer el resultado antes de
// que se limpie la pantalla. Tambien solo actua en modo interactivo.
void pausar() {
    if (!ES_TERMINAL()) return;
    cout << "\nPresione ENTER para continuar...";
    string descarte;
    if (!getline(cin, descarte)) entradaTerminada = true;
}

// --------------------------------------------------------------------
//  3.2 Utilidades de dibujo (cajas y tablas)
//  Los caracteres con tilde ocupan 2 bytes en UTF-8; por eso se mide el
//  ancho VISIBLE (se cuentan solo los bytes que inician un caracter).
// --------------------------------------------------------------------

int longitudVisible(const char* t) {
    int n = 0;
    for (; *t != '\0'; t++)
        if (((unsigned char)*t & 0xC0) != 0x80) n++;
    return n;
}

// Copia como maximo 'maxVisible' caracteres visibles de origen en destino
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

// Imprime 'texto' ocupando exactamente 'ancho' columnas (recorta o rellena con espacios)
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

// Linea horizontal de una tabla: izq + --- + medio + --- + ... + der
void imprimirBorde(const char* izq, const char* medio, const char* der, const int* anchos, int n) {
    cout << izq;
    for (int i = 0; i < n; i++) {
        repetir("-", anchos[i]);
        if (i < n - 1) cout << medio;
    }
    cout << der << "\n";
}

// Fila de una tabla. Cada celda tiene 1 espacio de margen a cada lado.
void imprimirFila(const char* const* celdas, const int* anchos, const bool* derecha, int n) {
    for (int i = 0; i < n; i++) {
        cout << "| ";
        imprimirPad(celdas[i], anchos[i] - 2, derecha[i]);
        cout << " ";
    }
    cout << "|\n";
}

// Menus: una linea de "=" arriba y abajo del titulo, y las opciones debajo
const int ANCHO_MENU = 45;

void imprimirCajaMenu(const char* titulo, const char* subtitulo, const char* const* opciones, int n) {
    cout << "\n";
    repetir("=", ANCHO_MENU); cout << "\n";
    int izq = (ANCHO_MENU - longitudVisible(titulo)) / 2;
    repetir(" ", izq); cout << titulo << "\n";
    if (subtitulo != nullptr) {
        izq = (ANCHO_MENU - longitudVisible(subtitulo)) / 2;
        repetir(" ", izq); cout << subtitulo << "\n";
    }
    repetir("=", ANCHO_MENU); cout << "\n";
    for (int i = 0; i < n; i++) cout << opciones[i] << "\n";
    repetir("=", ANCHO_MENU); cout << "\n";
}

// Caja del detalle de orden (ancho interior 64)
const int ANCHO_CAJA = 64;

void imprimirLineaCaja(const char* texto) {
    cout << "| "; imprimirPad(texto, ANCHO_CAJA - 2, false); cout << " |\n";
}

// --------------------------------------------------------------------
//  3.3 VEHICULOS (presentacion)
// --------------------------------------------------------------------

void mostrarVehiculo(Vehiculo* v) {
    cout << "  Vehiculo    : " << v->placa << " - " << v->marca << " " << v->modelo << " " << v->anio;
    if (v->id > 0) cout << "  (ID " << v->id << ")";
    cout << "\n";
    cout << "  Kilometraje : " << v->kilometraje << " km\n";
    cout << "  Propietario : " << v->nombrePropietario << "  |  Cedula: " << v->cedulaPropietario
         << "  |  Tel: " << v->telefonoPropietario << "\n";
    if (v->fechaRegistro[0] != '\0') cout << "  Registrado  : " << v->fechaRegistro << "\n";
}

void mostrarListaVehiculos(Vehiculo** vehiculos, int cantidad) {
    if (cantidad == 0) {
        cout << "No hay vehiculos para mostrar.\n";
        return;
    }
    const int anchos[] = {4, 10, 22, 6, 10, 20};
    const bool der[]   = {true, false, false, true, true, false};
    const char* titulos[] = {"ID", "Placa", "Vehiculo", "Anio", "Km", "Propietario"};

    imprimirBorde("+", "+", "+", anchos, 6);
    imprimirFila(titulos, anchos, der, 6);
    imprimirBorde("+", "+", "+", anchos, 6);
    for (int i = 0; i < cantidad; i++) {
        Vehiculo* v = vehiculos[i];
        char id[20], nombreVeh[120], anio[20], km[20];
        snprintf(id, sizeof(id), "%d", v->id);
        snprintf(nombreVeh, sizeof(nombreVeh), "%s %s", v->marca, v->modelo);
        snprintf(anio, sizeof(anio), "%d", v->anio);
        snprintf(km, sizeof(km), "%d", v->kilometraje);
        const char* fila[] = {id, v->placa, nombreVeh, anio, km, v->nombrePropietario};
        imprimirFila(fila, anchos, der, 6);
    }
    imprimirBorde("+", "+", "+", anchos, 6);
    cout << "Total de vehiculos: " << cantidad << "\n";
}

// Pide una placa que NO exista todavia (se vuelve a pedir si esta repetida)
bool pedirPlacaNueva(SistemaTaller* s, char* destino, int tam) {
    while (true) {
        if (!pedirDato("Placa: ", destino, tam, DATO_PLACA)) return false;
        if (buscarVehiculoPorPlaca(s, destino) == nullptr) return true;
        cout << "ERROR: La placa '" << destino << "' ya esta registrada.\n";
    }
}

void menuRegistrarVehiculo(SistemaTaller* s) {
    Vehiculo nuevo;
    memset(&nuevo, 0, sizeof(nuevo));     // todo en cero: id = 0, textos vacios

    cout << "\n--- REGISTRAR VEHICULO (escriba CANCELAR en cualquier momento para abortar) ---\n";

    // Los && se evaluan en orden y se detienen en el primer false (CANCELAR)
    bool completo =
        pedirPlacaNueva(s, nuevo.placa, sizeof(nuevo.placa)) &&
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
    mostrarVehiculo(&nuevo);
    if (!pedirConfirmacion("Guardar este vehiculo? (S/N): ")) {
        cout << "Registro cancelado. No se creo ningun vehiculo.\n";
        return;
    }

    Vehiculo* creado = agregarVehiculo(s, nuevo);
    if (creado == nullptr)
        cout << "ERROR: No se pudo registrar el vehiculo '" << nuevo.placa << "' (datos invalidos).\n";
    else
        cout << "Vehiculo '" << creado->placa << "' registrado correctamente (ID " << creado->id << ").\n";
}

void menuBuscarVehiculo(SistemaTaller* s) {
    cout << "\n--- BUSCAR VEHICULO ---\n";
    cout << "1. Por placa exacta\n2. Por propietario (nombre parcial)\n3. Por ID\n0. Volver\n";
    int op = pedirOpcion("Seleccione una opcion: ", 3);

    if (op == 1) {
        char placa[10];
        if (!pedirDato("Placa: ", placa, sizeof(placa), DATO_PLACA)) { cout << "Busqueda cancelada.\n"; return; }
        Vehiculo* v = buscarVehiculoPorPlaca(s, placa);
        if (v == nullptr) cout << "ERROR: No existe un vehiculo con la placa '" << placa << "'.\n";
        else mostrarVehiculo(v);
    } else if (op == 2) {
        char texto[100];
        if (!pedirDato("Nombre del propietario (o parte): ", texto, sizeof(texto), DATO_TEXTO)) { cout << "Busqueda cancelada.\n"; return; }
        int cantidad;
        Vehiculo** lista = buscarVehiculosPorPropietario(s, texto, &cantidad);
        if (cantidad == 0) cout << "No se encontraron vehiculos cuyo propietario contenga '" << texto << "'.\n";
        else mostrarListaVehiculos(lista, cantidad);
        delete[] lista;       // se libera SOLO el arreglo de punteros
    } else if (op == 3) {
        int id;
        if (!pedirEntero("ID del vehiculo: ", &id, 1, INT_MAX)) { cout << "Busqueda cancelada.\n"; return; }
        Vehiculo* v = buscarVehiculoPorID(s, id);
        if (v == nullptr) cout << "ERROR: No existe un vehiculo con ID " << id << ".\n";
        else mostrarVehiculo(v);
    }
}

void menuActualizarVehiculo(SistemaTaller* s) {
    char placa[10];
    cout << "\n--- ACTUALIZAR VEHICULO (escriba CANCELAR para abortar) ---\n";
    if (!pedirDato("Placa del vehiculo a actualizar: ", placa, sizeof(placa), DATO_PLACA)) {
        cout << "Actualizacion cancelada.\n";
        return;
    }
    Vehiculo* v = buscarVehiculoPorPlaca(s, placa);
    if (v == nullptr) {
        cout << "ERROR: No existe un vehiculo con la placa '" << placa << "'.\n";
        return;
    }
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
    if (!completo) {
        cout << "Actualizacion cancelada. No se modifico nada.\n";
        return;
    }
    if (!pedirConfirmacion("Actualizar este vehiculo? (S/N): ")) {
        cout << "Actualizacion cancelada. No se modifico nada.\n";
        return;
    }
    if (actualizarVehiculo(s, v->id, datos)) {
        cout << "Vehiculo '" << v->placa << "' actualizado correctamente.\n";
        mostrarVehiculo(v);
    } else {
        cout << "ERROR: No se pudo actualizar el vehiculo '" << v->placa << "' (datos invalidos).\n";
    }
}

void menuEliminarVehiculo(SistemaTaller* s) {
    char placa[10];
    cout << "\n--- ELIMINAR VEHICULO ---\n";
    if (!pedirDato("Placa del vehiculo a eliminar: ", placa, sizeof(placa), DATO_PLACA)) {
        cout << "Eliminacion cancelada.\n";
        return;
    }
    Vehiculo* v = buscarVehiculoPorPlaca(s, placa);
    if (v == nullptr) {
        cout << "ERROR: No existe un vehiculo con la placa '" << placa << "'.\n";
        return;
    }
    // La capa de logica solo responde true/false; para explicar el motivo
    // consultamos cuantas ordenes tiene.
    int nOrdenes = contarOrdenesDeVehiculo(s, v->id);
    if (nOrdenes > 0) {
        cout << "ERROR: El vehiculo '" << v->placa << "' tiene " << nOrdenes
             << (nOrdenes == 1 ? " orden registrada" : " ordenes registradas") << " y no puede eliminarse.\n";
        return;
    }
    mostrarVehiculo(v);
    if (!pedirConfirmacion("Eliminar este vehiculo? (S/N): ")) {
        cout << "Eliminacion cancelada.\n";
        return;
    }
    char placaGuardada[10];
    copiarTexto(placaGuardada, v->placa, sizeof(placaGuardada));     // v dejara de ser valido
    if (eliminarVehiculo(s, v->id)) cout << "Vehiculo '" << placaGuardada << "' eliminado correctamente.\n";
    else cout << "ERROR: No se pudo eliminar el vehiculo '" << placaGuardada << "'.\n";
}

void menuListarVehiculos(SistemaTaller* s) {
    cout << "\n--- LISTA DE VEHICULOS ---\n";
    int cantidad;
    Vehiculo** lista = listarVehiculos(s, &cantidad);
    mostrarListaVehiculos(lista, cantidad);
    cout << "(Capacidad interna del arreglo de vehiculos: " << s->capacidadVehiculos << ")\n";
    delete[] lista;
}

void submenuVehiculos(SistemaTaller* s) {
    const char* opciones[] = {"1. Registrar vehiculo", "2. Buscar vehiculo", "3. Actualizar vehiculo",
                              "4. Eliminar vehiculo", "5. Listar vehiculos", "0. Volver al menu principal"};
    int op = -1;
    while (op != 0 && !entradaTerminada) {
        limpiarPantalla();
        imprimirCajaMenu("GESTION DE VEHICULOS", nullptr, opciones, 6);
        op = pedirOpcion("Seleccione una opcion: ", 5);
        if (entradaTerminada) break;
        if (op != 0) limpiarPantalla();
        switch (op) {
            case 1: menuRegistrarVehiculo(s); break;
            case 2: menuBuscarVehiculo(s); break;
            case 3: menuActualizarVehiculo(s); break;
            case 4: menuEliminarVehiculo(s); break;
            case 5: menuListarVehiculos(s); break;
        }
        if (op != 0) pausar();
    }
}

// --------------------------------------------------------------------
//  3.4 MECANICOS (presentacion)
// --------------------------------------------------------------------

void mostrarMecanico(SistemaTaller* s, Mecanico* m) {
    cout << "  Mecanico      : " << m->nombre << "  (ID " << m->id << ")\n";
    cout << "  Cedula        : " << m->cedula << "\n";
    cout << "  Especialidad  : " << m->especialidad << "\n";
    cout << "  Fecha ingreso : " << m->fechaIngreso << "\n";
    cout << "  Ordenes abiertas: " << contarOrdenesAbiertasDeMecanico(s, m->id) << "\n";
}

void mostrarListaMecanicos(SistemaTaller* s, Mecanico** lista, int cantidad) {
    if (cantidad == 0) {
        cout << "No hay mecanicos registrados.\n";
        return;
    }
    const int anchos[] = {4, 26, 14, 16, 12, 10};
    const bool der[]   = {true, false, false, false, false, true};
    const char* titulos[] = {"ID", "Nombre", "Cedula", "Especialidad", "Ingreso", "Abiertas"};

    imprimirBorde("+", "+", "+", anchos, 6);
    imprimirFila(titulos, anchos, der, 6);
    imprimirBorde("+", "+", "+", anchos, 6);
    for (int i = 0; i < cantidad; i++) {
        char id[20], abiertas[20];
        snprintf(id, sizeof(id), "%d", lista[i]->id);
        snprintf(abiertas, sizeof(abiertas), "%d", contarOrdenesAbiertasDeMecanico(s, lista[i]->id));
        const char* fila[] = {id, lista[i]->nombre, lista[i]->cedula, lista[i]->especialidad,
                              lista[i]->fechaIngreso, abiertas};
        imprimirFila(fila, anchos, der, 6);
    }
    imprimirBorde("+", "+", "+", anchos, 6);
    cout << "Total de mecanicos: " << cantidad << "\n";
}

void menuRegistrarMecanico(SistemaTaller* s) {
    Mecanico nuevo;
    memset(&nuevo, 0, sizeof(nuevo));
    cout << "\n--- REGISTRAR MECANICO (escriba CANCELAR para abortar) ---\n";

    bool completo = pedirDato("Nombre completo: ", nuevo.nombre, sizeof(nuevo.nombre), DATO_TEXTO);
    // La cedula debe ser unica: si ya existe, se vuelve a pedir
    while (completo) {
        completo = pedirDato("Cedula: ", nuevo.cedula, sizeof(nuevo.cedula), DATO_CEDULA);
        if (!completo) break;
        Mecanico* existente = buscarMecanicoPorCedula(s, nuevo.cedula);
        if (existente == nullptr) break;
        cout << "ERROR: La cedula '" << nuevo.cedula << "' ya pertenece al mecanico '" << existente->nombre << "'.\n";
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
    Resultado motivo;
    Mecanico* creado = agregarMecanico(s, nuevo, &motivo);
    if (creado != nullptr) {
        cout << "Mecanico '" << creado->nombre << "' registrado correctamente (ID " << creado->id << ").\n";
    } else if (motivo == RES_DUPLICADO) {
        cout << "ERROR: La cedula '" << nuevo.cedula << "' ya esta registrada.\n";
    } else {
        cout << "ERROR: No se pudo registrar al mecanico '" << nuevo.nombre << "' (datos invalidos).\n";
    }
}

void menuBuscarMecanico(SistemaTaller* s) {
    int id;
    cout << "\n--- BUSCAR MECANICO POR ID ---\n";
    if (!pedirEntero("ID del mecanico: ", &id, 1, INT_MAX)) { cout << "Busqueda cancelada.\n"; return; }
    Mecanico* m = buscarMecanicoPorID(s, id);
    if (m == nullptr) cout << "ERROR: No existe un mecanico con ID " << id << ".\n";
    else mostrarMecanico(s, m);
}

void menuListarMecanicos(SistemaTaller* s) {
    cout << "\n--- LISTA DE MECANICOS ---\n";
    int cantidad;
    Mecanico** lista = listarMecanicos(s, &cantidad);
    mostrarListaMecanicos(s, lista, cantidad);
    delete[] lista;
}

void submenuMecanicos(SistemaTaller* s) {
    const char* opciones[] = {"1. Registrar mecanico", "2. Buscar mecanico por ID",
                              "3. Listar mecanicos", "0. Volver al menu principal"};
    int op = -1;
    while (op != 0 && !entradaTerminada) {
        limpiarPantalla();
        imprimirCajaMenu("GESTION DE MECANICOS", nullptr, opciones, 4);
        op = pedirOpcion("Seleccione una opcion: ", 3);
        if (entradaTerminada) break;
        if (op != 0) limpiarPantalla();
        switch (op) {
            case 1: menuRegistrarMecanico(s); break;
            case 2: menuBuscarMecanico(s); break;
            case 3: menuListarMecanicos(s); break;
        }
        if (op != 0) pausar();
    }
}

// --------------------------------------------------------------------
//  3.5 REPUESTOS (presentacion)
// --------------------------------------------------------------------

void mostrarRepuesto(Repuesto* r) {
    cout << "  Repuesto     : " << r->codigo << " - " << r->nombre << "  (ID " << r->id << ")\n";
    cout << "  Marca        : " << r->marca << "\n";
    printf("  Precio       : %.2f\n", r->precio);
    cout << "  Stock        : " << r->stock << " (minimo " << r->stockMinimo << ")";
    if (repuestoEnBajoStock(r)) cout << "  ** BAJO STOCK **";
    cout << "\n";
    if (r->fechaRegistro[0] != '\0') cout << "  Registrado   : " << r->fechaRegistro << "\n";
}

void mostrarListaRepuestos(Repuesto** lista, int cantidad) {
    if (cantidad == 0) {
        cout << "No hay repuestos para mostrar.\n";
        return;
    }
    const int anchos[] = {4, 10, 28, 14, 10, 7, 7, 14};
    const bool der[]   = {true, false, false, false, true, true, true, false};
    const char* titulos[] = {"ID", "Codigo", "Nombre", "Marca", "Precio", "Stock", "Min", "Estado"};

    imprimirBorde("+", "+", "+", anchos, 8);
    imprimirFila(titulos, anchos, der, 8);
    imprimirBorde("+", "+", "+", anchos, 8);
    for (int i = 0; i < cantidad; i++) {
        Repuesto* r = lista[i];
        char id[20], precio[30], stock[20], minimo[20];
        snprintf(id, sizeof(id), "%d", r->id);
        snprintf(precio, sizeof(precio), "%.2f", r->precio);
        snprintf(stock, sizeof(stock), "%d", r->stock);
        snprintf(minimo, sizeof(minimo), "%d", r->stockMinimo);
        const char* estado = repuestoEnBajoStock(r) ? "BAJO STOCK" : "OK";
        const char* fila[] = {id, r->codigo, r->nombre, r->marca, precio, stock, minimo, estado};
        imprimirFila(fila, anchos, der, 8);
    }
    imprimirBorde("+", "+", "+", anchos, 8);
    cout << "Total de repuestos: " << cantidad << "\n";
}

void menuRegistrarRepuesto(SistemaTaller* s) {
    Repuesto nuevo;
    memset(&nuevo, 0, sizeof(nuevo));
    cout << "\n--- REGISTRAR REPUESTO (escriba CANCELAR para abortar) ---\n";

    bool completo = true;
    while (completo) {                      // el codigo debe ser unico
        completo = pedirDato("Codigo: ", nuevo.codigo, sizeof(nuevo.codigo), DATO_CODIGO);
        if (!completo) break;
        if (buscarRepuestoPorCodigo(s, nuevo.codigo) == nullptr) break;
        cout << "ERROR: El codigo '" << nuevo.codigo << "' ya esta registrado.\n";
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
    mostrarRepuesto(&nuevo);
    if (!pedirConfirmacion("Guardar este repuesto? (S/N): ")) {
        cout << "Registro cancelado. No se creo ningun repuesto.\n";
        return;
    }
    Resultado motivo;
    Repuesto* creado = agregarRepuesto(s, nuevo, &motivo);
    if (creado != nullptr) {
        cout << "Repuesto '" << creado->codigo << " - " << creado->nombre << "' registrado correctamente (ID " << creado->id << ").\n";
    } else if (motivo == RES_DUPLICADO) {
        cout << "ERROR: El codigo '" << nuevo.codigo << "' ya esta registrado.\n";
    } else {
        cout << "ERROR: No se pudo registrar el repuesto '" << nuevo.codigo << "' (datos invalidos).\n";
    }
}

void menuBuscarRepuesto(SistemaTaller* s) {
    cout << "\n--- BUSCAR REPUESTO ---\n";
    cout << "1. Por ID\n2. Por codigo o nombre (parcial)\n0. Volver\n";
    int op = pedirOpcion("Seleccione una opcion: ", 2);

    if (op == 1) {
        int id;
        if (!pedirEntero("ID del repuesto: ", &id, 1, INT_MAX)) { cout << "Busqueda cancelada.\n"; return; }
        Repuesto* r = buscarRepuestoPorID(s, id);
        if (r == nullptr) cout << "ERROR: No existe un repuesto con ID " << id << ".\n";
        else mostrarRepuesto(r);
    } else if (op == 2) {
        char texto[100];
        if (!pedirDato("Codigo o nombre (o parte): ", texto, sizeof(texto), DATO_TEXTO)) { cout << "Busqueda cancelada.\n"; return; }
        int cantidad;
        Repuesto** lista = buscarRepuestosPorTexto(s, texto, &cantidad);
        if (cantidad == 0) cout << "No se encontraron repuestos que contengan '" << texto << "'.\n";
        else mostrarListaRepuestos(lista, cantidad);
        delete[] lista;
    }
}

void menuEntradaMercancia(SistemaTaller* s) {
    char codigo[20];
    int cantidad;
    cout << "\n--- ENTRADA DE MERCANCIA (escriba CANCELAR para abortar) ---\n";
    if (!pedirDato("Codigo del repuesto: ", codigo, sizeof(codigo), DATO_CODIGO)) { cout << "Operacion cancelada.\n"; return; }
    Repuesto* r = buscarRepuestoPorCodigo(s, codigo);
    if (r == nullptr) { cout << "ERROR: No existe un repuesto con el codigo '" << codigo << "'.\n"; return; }
    cout << "Stock actual de '" << r->nombre << "': " << r->stock << "\n";
    if (!pedirEntero("Cantidad a ingresar: ", &cantidad, 1, INT_MAX)) { cout << "Operacion cancelada.\n"; return; }
    if (!pedirConfirmacion("Registrar la entrada? (S/N): ")) { cout << "Operacion cancelada.\n"; return; }

    Resultado res = registrarEntradaRepuesto(s, r->id, cantidad);
    if (res == RES_OK) cout << "Entrada registrada. Stock de '" << r->nombre << "': " << r->stock << ".\n";
    else cout << "ERROR: No se pudo registrar la entrada de '" << r->nombre << "'.\n";
}

void menuCambiarPrecio(SistemaTaller* s) {
    char codigo[20];
    float nuevoPrecio;
    cout << "\n--- CAMBIAR PRECIO (escriba CANCELAR para abortar) ---\n";
    if (!pedirDato("Codigo del repuesto: ", codigo, sizeof(codigo), DATO_CODIGO)) { cout << "Operacion cancelada.\n"; return; }
    Repuesto* r = buscarRepuestoPorCodigo(s, codigo);
    if (r == nullptr) { cout << "ERROR: No existe un repuesto con el codigo '" << codigo << "'.\n"; return; }
    printf("Precio actual de '%s': %.2f\n", r->nombre, r->precio);
    if (!pedirDecimal("Nuevo precio: ", &nuevoPrecio, true)) { cout << "Operacion cancelada.\n"; return; }
    if (!pedirConfirmacion("Actualizar el precio? (S/N): ")) { cout << "Operacion cancelada.\n"; return; }

    Resultado res = cambiarPrecioRepuesto(s, r->id, nuevoPrecio);
    if (res == RES_OK) printf("Precio de '%s' actualizado a %.2f (las ordenes ya registradas no cambian).\n", r->nombre, r->precio);
    else cout << "ERROR: No se pudo cambiar el precio de '" << r->nombre << "'.\n";
}

void menuEliminarRepuesto(SistemaTaller* s) {
    char codigo[20];
    cout << "\n--- ELIMINAR REPUESTO (escriba CANCELAR para abortar) ---\n";
    if (!pedirDato("Codigo del repuesto: ", codigo, sizeof(codigo), DATO_CODIGO)) { cout << "Operacion cancelada.\n"; return; }
    Repuesto* r = buscarRepuestoPorCodigo(s, codigo);
    if (r == nullptr) { cout << "ERROR: No existe un repuesto con el codigo '" << codigo << "'.\n"; return; }

    if (repuestoEstaEnUso(s, r->id)) {
        cout << "ERROR: El repuesto '" << r->nombre << "' aparece en ordenes de servicio y no puede eliminarse.\n";
        return;
    }
    mostrarRepuesto(r);
    if (!pedirConfirmacion("Eliminar este repuesto? (S/N): ")) { cout << "Eliminacion cancelada.\n"; return; }

    char nombre[100];
    copiarTexto(nombre, r->nombre, sizeof(nombre));      // r dejara de ser valido
    Resultado res = eliminarRepuesto(s, r->id);
    if (res == RES_OK) cout << "Repuesto '" << nombre << "' eliminado correctamente.\n";
    else cout << "ERROR: No se pudo eliminar el repuesto '" << nombre << "'.\n";
}

void menuListarRepuestos(SistemaTaller* s) {
    cout << "\n--- INVENTARIO DE REPUESTOS ---\n";
    int cantidad;
    Repuesto** lista = listarRepuestos(s, &cantidad);
    mostrarListaRepuestos(lista, cantidad);
    delete[] lista;
}

void submenuInventario(SistemaTaller* s) {
    const char* opciones[] = {"1. Registrar repuesto", "2. Buscar repuesto", "3. Registrar entrada de mercancia",
                              "4. Listar repuestos", "5. Cambiar precio", "6. Eliminar repuesto",
                              "0. Volver al menu principal"};
    int op = -1;
    while (op != 0 && !entradaTerminada) {
        limpiarPantalla();
        imprimirCajaMenu("INVENTARIO DE REPUESTOS", nullptr, opciones, 7);
        op = pedirOpcion("Seleccione una opcion: ", 6);
        if (entradaTerminada) break;
        if (op != 0) limpiarPantalla();
        switch (op) {
            case 1: menuRegistrarRepuesto(s); break;
            case 2: menuBuscarRepuesto(s); break;
            case 3: menuEntradaMercancia(s); break;
            case 4: menuListarRepuestos(s); break;
            case 5: menuCambiarPrecio(s); break;
            case 6: menuEliminarRepuesto(s); break;
        }
        if (op != 0) pausar();
    }
}

// --------------------------------------------------------------------
//  3.6 ORDENES DE SERVICIO (presentacion)
// --------------------------------------------------------------------



// ====================================================================
//  4. main
// ====================================================================
int main() {
    SistemaTaller sistema;                 // el sistema vive en la pila; sus arreglos, en el heap

    Taller datosTaller;
    memset(&datosTaller, 0, sizeof(datosTaller));
    copiarTexto(datosTaller.nombre, "Autoservicio El Lago", sizeof(datosTaller.nombre));
    copiarTexto(datosTaller.rif, "J-12345678-9", sizeof(datosTaller.rif));
    copiarTexto(datosTaller.direccion, "Av. 5 de Julio, Maracaibo", sizeof(datosTaller.direccion));
    copiarTexto(datosTaller.telefono, "0261-7000000", sizeof(datosTaller.telefono));

    inicializarSistema(&sistema, datosTaller);
    liberarSistema(&sistema);
    cout << "\nHasta pronto.\n";
    return 0;
}