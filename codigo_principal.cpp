// ==================================================
//  SISTEMA DE GESTION DE TALLER MECANICO.
// ==================================================
#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <limits>
#include <sstream>
#include <algorithm>
#include <cctype>
using namespace std;

// ============================================================================
//  DECLARACIONES ADELANTADAS (PROTOTIPOS)
// ============================================================================
class Vehiculo;
class Taller;

void limpiarBuffer();
string aMayusculas(string s);
string aMinusculas(string s);
bool contiene(const string& texto, const string& sub);
int leerEntero(const string& msg, int min, int max);
string leerTexto(const string& msg, bool permitirVacio = false);
string leerFecha(const string& msg);
bool confirmar(const string& msg);
void linea(char c = '-', int n = 78);

void mostrarVehiculo(Vehiculo* v);
void mostrarListaVehiculos(const vector<Vehiculo*>& lista);
void menuRegistrarVehiculo(Taller& t);
void menuBuscarVehiculo(Taller& t);
void menuActualizarVehiculo(Taller& t);
void menuEliminarVehiculo(Taller& t);
void menuListarVehiculos(Taller& t);
void menuGestionVehiculos(Taller& t);

// ============================================================================
//  SECCION 1: UTILIDADES DE ENTRADA Y FORMATO
// ============================================================================

void limpiarBuffer() {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

string aMayusculas(string s) {
    for (size_t i = 0; i < s.size(); i++)
        s[i] = toupper(static_cast<unsigned char>(s[i]));
    return s;
}

string aMinusculas(string s) {
    for (size_t i = 0; i < s.size(); i++)
        s[i] = tolower(static_cast<unsigned char>(s[i]));
    return s;
}

bool contiene(const string& texto, const string& sub) {
    return aMinusculas(texto).find(aMinusculas(sub)) != string::npos;
}

int leerEntero(const string& msg, int min, int max) {
    int v;
    while (true) {
        cout << msg;
        if (cin >> v) {
            limpiarBuffer();
            if (v >= min && v <= max) return v;
            cout << "  [!] Valor fuera de rango (" << min << " - " << max << ").\n";
        } else {
            cout << "  [!] Entrada invalida: se esperaba un numero entero.\n";
            limpiarBuffer();
        }
    }
}

string leerTexto(const string& msg, bool permitirVacio) {
    string s;
    while (true) {
        cout << msg;
        getline(cin, s);
        if (permitirVacio || !s.empty()) return s;
        cout << "  [!] El campo no puede quedar vacio.\n";
    }
}

string leerFecha(const string& msg) {
    string s;
    while (true) {
        cout << msg;
        getline(cin, s);
        if (s.size() == 10 && s[4] == '-' && s[7] == '-') {
            bool ok = true;
            for (int i = 0; i < 10; i++) {
                if (i == 4 || i == 7) continue;
                if (!isdigit(static_cast<unsigned char>(s[i]))) { ok = false; break; }
            }
            if (ok) {
                int mes = atoi(s.substr(5, 2).c_str());
                int dia = atoi(s.substr(8, 2).c_str());
                if (mes >= 1 && mes <= 12 && dia >= 1 && dia <= 31) return s;
            }
        }
        cout << "  [!] Formato invalido. Use AAAA-MM-DD (ej: 2026-10-14).\n";
    }
}

bool confirmar(const string& msg) {
    string r;
    while (true) {
        cout << msg << " (S/N): ";
        getline(cin, r);
        if (r == "s" || r == "S") return true;
        if (r == "n" || r == "N") return false;
        cout << "  [!] Responda S o N.\n";
    }
}

void linea(char c, int n) { cout << string(n, c) << "\n"; }

// ============================================================================
//  SECCION 2: CLASE DEL DOMINIO (VEHICULO)
// ============================================================================

class Vehiculo {
public:
    int id;
    string placa;
    string marca;
    string modelo;
    int anio;
    int kilometraje;
    string nombrePropietario;
    string cedulaPropietario;
    string telefonoPropietario;
    string fechaRegistro;

    Vehiculo() : id(0), anio(0), kilometraje(0) {}

    string descripcion() const { return marca + " " + modelo; }
};

// ============================================================================
//  SECCION 3: CLASE GESTORA (TALLER - VEHICULOS)
// ============================================================================

class Taller {
private:
    string nombre;
    string rif;
    string direccion;
    string telefono;

    vector<Vehiculo> vehiculos;
    int sigIdVehiculo;

public:
    Taller() : sigIdVehiculo(1) {
        nombre = "Autoservicio El Lago";
        rif = "J-30123456-7";
        direccion = "Av. Bella Vista, Maracaibo";
        telefono = "0261-1234567";
    }

    string getNombre() const { return nombre; }
    string getRif() const { return rif; }
    string getDireccion() const { return direccion; }
    string getTelefono() const { return telefono; }

    const vector<Vehiculo>& getVehiculos() const { return vehiculos; }

    Vehiculo* agregarVehiculo(const Vehiculo& datos) {
        if (datos.placa.size() < 6 || datos.placa.size() > 7) return nullptr;
        if (datos.anio < 1950 || datos.anio > 2027) return nullptr;
        if (datos.kilometraje < 0) return nullptr;
        if (buscarVehiculoPorPlaca(datos.placa) != nullptr) return nullptr;

        Vehiculo v = datos;
        v.id = sigIdVehiculo++;
        vehiculos.push_back(v);
        return &vehiculos.back();
    }

    Vehiculo* buscarVehiculoPorId(int id) {
        for (size_t i = 0; i < vehiculos.size(); i++)
            if (vehiculos[i].id == id) return &vehiculos[i];
        return nullptr;
    }

    Vehiculo* buscarVehiculoPorPlaca(const string& placa) {
        for (size_t i = 0; i < vehiculos.size(); i++)
            if (vehiculos[i].placa == placa) return &vehiculos[i];
        return nullptr;
    }

    vector<Vehiculo*> buscarVehiculosPorPropietario(const string& sub) {
        vector<Vehiculo*> res;
        for (size_t i = 0; i < vehiculos.size(); i++)
            if (contiene(vehiculos[i].nombrePropietario, sub))
                res.push_back(&vehiculos[i]);
        return res;
    }

    bool actualizarVehiculo(int id, const Vehiculo& datos) {
        Vehiculo* v = buscarVehiculoPorId(id);
        if (v == nullptr) return false;
        if (datos.marca.empty() || datos.modelo.empty()) return false;
        if (datos.anio < 1950 || datos.anio > 2027) return false;

        v->marca = datos.marca;
        v->modelo = datos.modelo;
        v->anio = datos.anio;
        v->nombrePropietario = datos.nombrePropietario;
        v->cedulaPropietario = datos.cedulaPropietario;
        v->telefonoPropietario = datos.telefonoPropietario;
        return true;
    }

    bool eliminarVehiculo(int id) {
        int pos = -1;
        for (size_t i = 0; i < vehiculos.size(); i++)
            if (vehiculos[i].id == id) { pos = static_cast<int>(i); break; }
        if (pos == -1) return false;

        vehiculos.erase(vehiculos.begin() + pos);
        return true;
    }

    int contarOrdenesDeVehiculo(int idVehiculo) const {
        return 0; // Sin ordenes activas en esta version reducida
    }
};

// ============================================================================
//  SECCION 4: CAPA DE PRESENTACION Y MENUS
// ============================================================================

void mostrarVehiculo(Vehiculo* v) {
    if (v == nullptr) return;
    cout << "  ID " << v->id << " | Placa " << v->placa << " | " << v->descripcion()
         << " " << v->anio << " | " << v->kilometraje << " km | "
         << v->nombrePropietario << " (C.I. " << v->cedulaPropietario << ")\n";
}

void mostrarListaVehiculos(const vector<Vehiculo*>& lista) {
    linea('=');
    cout << "  LISTADO DE VEHICULOS\n";
    linea('=');
    cout << left << setw(5) << "ID" << setw(10) << "Placa" << setw(22) << "Vehiculo"
         << setw(7) << "Anio" << setw(9) << "Km" << "Propietario\n";
    linea('-');
    for (size_t i = 0; i < lista.size(); i++) {
        Vehiculo* v = lista[i];
        cout << left << setw(5) << v->id << setw(10) << v->placa
             << setw(22) << v->descripcion().substr(0, 21) << setw(7) << v->anio
             << setw(9) << v->kilometraje << v->nombrePropietario << "\n";
    }
    linea('-');
    cout << "  Total de vehiculos: " << lista.size() << "\n";
}

void menuRegistrarVehiculo(Taller& t) {
    linea('=');
    cout << "  REGISTRO DE VEHICULO\n";
    linea('=');

    Vehiculo v;
    v.placa = leerTexto("  Placa (6-7 caracteres): ");
    v.marca = leerTexto("  Marca: ");
    v.modelo = leerTexto("  Modelo: ");
    v.anio = leerEntero("  Anio (1950-2027): ", 1950, 2027);
    v.kilometraje = leerEntero("  Kilometraje: ", 0, 2000000);
    v.nombrePropietario = leerTexto("  Nombre del propietario: ");
    v.cedulaPropietario = leerTexto("  Cedula del propietario: ");
    v.telefonoPropietario = leerTexto("  Telefono del propietario: ");
    v.fechaRegistro = leerFecha("  Fecha de registro (AAAA-MM-DD): ");

    if (!confirmar("  Confirmar registro del vehiculo")) {
        cout << "  Registro cancelado.\n";
        return;
    }

    Vehiculo* creado = t.agregarVehiculo(v);
    if (creado == nullptr) {
        cout << "  ERROR: no se pudo registrar. Verifique placa duplicada,\n";
        cout << "         anio fuera de rango o kilometraje negativo.\n";
        return;
    }
    cout << "  [OK] Vehiculo registrado: " << creado->placa << " - "
         << creado->descripcion() << "\n";
}

void menuBuscarVehiculo(Taller& t) {
    cout << "  1. Por placa   2. Por propietario (parcial)\n";
    int op = leerEntero("  Opcion: ", 1, 2);

    if (op == 1) {
        string placa = leerTexto("  Placa: ");
        Vehiculo* v = t.buscarVehiculoPorPlaca(placa);
        if (v == nullptr) { cout << "  ERROR: no existe el vehiculo '" << placa << "'.\n"; return; }
        mostrarVehiculo(v);
    } else {
        string sub = leerTexto("  Subcadena del propietario: ");
        vector<Vehiculo*> res = t.buscarVehiculosPorPropietario(sub);
        if (res.empty()) { cout << "  No se encontraron coincidencias.\n"; return; }
        mostrarListaVehiculos(res);
    }
}

void menuActualizarVehiculo(Taller& t) {
    int id = leerEntero("  ID del vehiculo: ", 1, 1000000);
    Vehiculo* v = t.buscarVehiculoPorId(id);
    if (v == nullptr) { cout << "  ERROR: no existe el vehiculo con ID " << id << ".\n"; return; }

    mostrarVehiculo(v);
    Vehiculo datos;
    datos.marca = leerTexto("  Nueva marca: ");
    datos.modelo = leerTexto("  Nuevo modelo: ");
    datos.anio = leerEntero("  Nuevo anio (1950-2027): ", 1950, 2027);
    datos.nombrePropietario = leerTexto("  Nombre del propietario: ");
    datos.cedulaPropietario = leerTexto("  Cedula: ");
    datos.telefonoPropietario = leerTexto("  Telefono: ");

    if (!confirmar("  Confirmar actualizacion")) return;

    if (t.actualizarVehiculo(id, datos))
        cout << "  [OK] Vehiculo actualizado.\n";
    else
        cout << "  ERROR: datos invalidos.\n";
}

void menuEliminarVehiculo(Taller& t) {
    int id = leerEntero("  ID del vehiculo: ", 1, 1000000);
    Vehiculo* v = t.buscarVehiculoPorId(id);
    if (v == nullptr) { cout << "  ERROR: no existe el vehiculo con ID " << id << ".\n"; return; }

    mostrarVehiculo(v);
    if (!confirmar("  Confirmar eliminacion")) return;

    if (!t.eliminarVehiculo(id)) {
        cout << "  ERROR: No se pudo eliminar el vehiculo.\n";
        return;
    }
    cout << "  [OK] Vehiculo eliminado.\n";
}

void menuListarVehiculos(Taller& t) {
    vector<Vehiculo*> lista;
    const vector<Vehiculo>& vs = t.getVehiculos();
    for (size_t i = 0; i < vs.size(); i++) lista.push_back(const_cast<Vehiculo*>(&vs[i]));
    mostrarListaVehiculos(lista);
}

void menuGestionVehiculos(Taller& t) {
    while (true) {
        linea('=');
        cout << "  GESTION DE VEHICULOS\n";
        linea('=');
        cout << "  1. Registrar vehiculo\n";
        cout << "  2. Buscar vehiculo\n";
        cout << "  3. Actualizar vehiculo\n";
        cout << "  4. Eliminar vehiculo\n";
        cout << "  5. Listar vehiculos\n";
        cout << "  0. Volver al menu principal\n";
        int op = leerEntero("  Seleccione una opcion: ", 0, 5);
        switch (op) {
            case 1: menuRegistrarVehiculo(t); break;
            case 2: menuBuscarVehiculo(t); break;
            case 3: menuActualizarVehiculo(t); break;
            case 4: menuEliminarVehiculo(t); break;
            case 5: menuListarVehiculos(t); break;
            case 0: return;
        }
        cout << "\n";
    }
}

int main() {
    Taller t;

    cout << "\n";
    linea('=');
    cout << "  SISTEMA DE GESTION DE TALLER MECANICO (MODULO VEHICULOS)\n";
    linea('=');
    cout << "  Taller    : " << t.getNombre() << "\n";
    cout << "  RIF       : " << t.getRif() << "\n";
    cout << "  Direccion : " << t.getDireccion() << "\n";
    cout << "  Telefono  : " << t.getTelefono() << "\n";
    linea('=');

    while (true) {
        linea('=');
        cout << "  MENU PRINCIPAL\n";
        linea('=');
        cout << "  1. Gestion de Vehiculos\n";
        cout << "  0. Salir\n";
        linea('-');
        int op = leerEntero("  Seleccione una opcion: ", 0, 1);

        if (op == 0) {
            if (confirmar("  Salir del sistema (los datos en memoria se pierden)")) {
                cout << "  Gracias por usar el sistema. Hasta luego!\n";
                break;
            }
            continue;
        }

        if (op == 1) {
            menuGestionVehiculos(t);
        }
        cout << "\n";
    }

    return 0;
}
// ============================================================================
// MENU (repuestos y ordenes )
// ============================================================================













// ============================================================================
//  MENU (clientes) 
// ===========================================================================