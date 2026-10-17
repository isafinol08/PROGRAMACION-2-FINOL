// =====================================================================
//  POKÉDEX INTERACTIVA EN C++
//  Lee "pokemon_150_registros.csv" y permite consultar los datos.
//  Compilar: g++ -std=c++17 -Wall -o pokedex.exe pokedex.cpp
// =====================================================================
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <iomanip>
#include <cctype>

#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

using namespace std;

// ---------------------------------------------------------------------
//  Modelo de datos
//  Columnas del CSV:
//  Nº Pokedex,Nombre,Tipo 1,Tipo 2,Total,HP,Ataque,Defensa,
//  Ataque Esp.,Defensa Esp.,Velocidad,Generación,Legendario
// ---------------------------------------------------------------------
struct Pokemon {
    int numero;
    string nombre;
    string tipo1;
    string tipo2;      // puede estar vacío
    int total;
    int hp;
    int ataque;
    int defensa;
    int ataqueEsp;
    int defensaEsp;
    int velocidad;
    int generacion;
    bool legendario;
};

const string ARCHIVO_CSV = "pokemon_150_registros.csv";

// ---------------------------------------------------------------------
//  Utilidades de texto
// ---------------------------------------------------------------------
string aMinusculas(string s) {
    for (size_t i = 0; i < s.size(); i++)
        s[i] = static_cast<char>(tolower(static_cast<unsigned char>(s[i])));
    return s;
}

// Quita espacios, tabuladores y '\r' al inicio y al final
string recortar(const string& s) {
    size_t ini = 0;
    size_t fin = s.size();
    while (ini < fin && isspace(static_cast<unsigned char>(s[ini]))) ini++;
    while (fin > ini && isspace(static_cast<unsigned char>(s[fin - 1]))) fin--;
    return s.substr(ini, fin - ini);
}

// Convierte nombres de tipo en español (o inglés) al inglés usado en el CSV
string normalizarTipo(const string& entrada) {
    string t = aMinusculas(recortar(entrada));
    struct Alias { const char* es; const char* en; };
    const Alias tabla[] = {
        {"agua", "water"},       {"fuego", "fire"},      {"planta", "grass"},
        {"electrico", "electric"}, {"eléctrico", "electric"}, {"hielo", "ice"},
        {"lucha", "fighting"},   {"veneno", "poison"},   {"tierra", "ground"},
        {"volador", "flying"},   {"psiquico", "psychic"}, {"psíquico", "psychic"},
        {"bicho", "bug"},        {"roca", "rock"},       {"fantasma", "ghost"},
        {"dragon", "dragon"},    {"dragón", "dragon"},   {"acero", "steel"},
        {"hada", "fairy"},       {"normal", "normal"}
    };
    for (const Alias& a : tabla)
        if (t == a.es) return a.en;
    return t;  // ya viene en inglés (o no existe)
}

// ---------------------------------------------------------------------
//  Lectura del CSV (fstream + sstream + getline con ',')
// ---------------------------------------------------------------------
bool cargarCSV(const string& ruta, vector<Pokemon>& lista) {
    ifstream archivo(ruta);

    if (!archivo.is_open()) {                       // validación obligatoria
        cout << "Error: No se pudo abrir el archivo '" << ruta << "'." << endl;
        return false;
    }

    string linea;
    getline(archivo, linea);                         // saltar encabezado

    int numLinea = 1;
    int descartadas = 0;

    while (getline(archivo, linea)) {
        numLinea++;
        linea = recortar(linea);                     // elimina '\r' si lo hay
        if (linea.empty()) continue;

        stringstream ss(linea);
        string campo[13];
        int leidos = 0;
        while (leidos < 13 && getline(ss, campo[leidos], ',')) leidos++;

        if (leidos < 13) {                           // línea incompleta
            cout << "Aviso: línea " << numLinea << " incompleta, se omite." << endl;
            descartadas++;
            continue;
        }

        try {
            Pokemon p;
            p.numero     = stoi(campo[0]);
            p.nombre     = recortar(campo[1]);
            p.tipo1      = recortar(campo[2]);
            p.tipo2      = recortar(campo[3]);
            p.total      = stoi(campo[4]);
            p.hp         = stoi(campo[5]);
            p.ataque     = stoi(campo[6]);
            p.defensa    = stoi(campo[7]);
            p.ataqueEsp  = stoi(campo[8]);
            p.defensaEsp = stoi(campo[9]);
            p.velocidad  = stoi(campo[10]);
            p.generacion = stoi(campo[11]);
            p.legendario = (aMinusculas(recortar(campo[12])) == "true");
            lista.push_back(p);
        } catch (const exception&) {                 // stoi falló
            cout << "Aviso: datos inválidos en la línea " << numLinea
                 << ", se omite." << endl;
            descartadas++;
        }
    }

    archivo.close();

    if (descartadas > 0)
        cout << descartadas << " línea(s) descartada(s)." << endl;

    return !lista.empty();
}

// ---------------------------------------------------------------------
//  Presentación
// ---------------------------------------------------------------------
void mostrarPokemon(const Pokemon& p) {
    cout << "\n+------------------------------------------+\n";
    cout << "  #" << setw(3) << setfill('0') << p.numero << setfill(' ')
         << "  " << p.nombre << (p.legendario ? "  [LEGENDARIO]" : "") << "\n";
    cout << "+------------------------------------------+\n";
    cout << "  Tipo:           " << p.tipo1;
    if (!p.tipo2.empty()) cout << " / " << p.tipo2;
    cout << "\n";
    cout << "  Generación:     " << p.generacion << "\n";
    cout << "  HP:             " << p.hp << "\n";
    cout << "  Ataque:         " << p.ataque << "\n";
    cout << "  Defensa:        " << p.defensa << "\n";
    cout << "  Ataque Esp.:    " << p.ataqueEsp << "\n";
    cout << "  Defensa Esp.:   " << p.defensaEsp << "\n";
    cout << "  Velocidad:      " << p.velocidad << "\n";
    cout << "  TOTAL:          " << p.total << "\n";
    cout << "+------------------------------------------+\n";
}

void mostrarFilaResumen(const Pokemon& p) {
    string tipos = p.tipo1 + (p.tipo2.empty() ? "" : "/" + p.tipo2);
    cout << "  #" << setw(3) << setfill('0') << p.numero << setfill(' ')
         << "  " << left << setw(14) << p.nombre
         << setw(18) << tipos << right
         << "Total: " << p.total << "\n";
}

// ---------------------------------------------------------------------
//  Entrada de datos robusta (siempre con getline para no romper el buffer)
// ---------------------------------------------------------------------
string leerLinea(const string& mensaje) {
    string s;
    cout << mensaje;
    getline(cin, s);
    if (!cin) {                 // fin de entrada (Ctrl+Z / Ctrl+D)
        cout << "\nEntrada cerrada. Saliendo...\n";
        exit(0);
    }
    return recortar(s);
}

bool leerEntero(const string& mensaje, int& valor) {
    string s = leerLinea(mensaje);
    try {
        size_t pos = 0;
        valor = stoi(s, &pos);
        return pos == s.size();   // rechaza "12abc"
    } catch (const exception&) {
        return false;
    }
}

// ---------------------------------------------------------------------
//  Opciones del menú
// ---------------------------------------------------------------------
// Opción 1: búsqueda por número de Pokédex
void opcionBuscarPorNumero(const vector<Pokemon>& lista) {
    int numero;
    if (!leerEntero("Número de Pokédex: ", numero)) {
        cout << "Entrada no válida. Debe ser un número entero.\n";
        return;
    }
    for (const Pokemon& p : lista) {
        if (p.numero == numero) {
            mostrarPokemon(p);
            return;
        }
    }
    cout << "No existe ningún Pokémon con el número " << numero << ".\n";
}

// Opción 2: búsqueda por nombre (exacta o parcial, sin distinguir mayúsculas)
void opcionBuscarPorNombre(const vector<Pokemon>& lista) {
    string texto = aMinusculas(leerLinea("Nombre (o parte del nombre): "));
    if (texto.empty()) {
        cout << "No escribiste nada.\n";
        return;
    }

    // 1) Coincidencia exacta
    for (const Pokemon& p : lista) {
        if (aMinusculas(p.nombre) == texto) {
            mostrarPokemon(p);
            return;
        }
    }

    // 2) Coincidencia parcial
    vector<const Pokemon*> coincidencias;
    for (const Pokemon& p : lista)
        if (aMinusculas(p.nombre).find(texto) != string::npos)
            coincidencias.push_back(&p);

    if (coincidencias.empty()) {
        cout << "No se encontró ningún Pokémon que contenga \"" << texto << "\".\n";
    } else if (coincidencias.size() <= 3) {
        cout << coincidencias.size() << " coincidencia(s):\n";
        for (const Pokemon* p : coincidencias) mostrarPokemon(*p);
    } else {
        cout << coincidencias.size() << " coincidencias (usa la opción 1 para ver el detalle):\n";
        for (const Pokemon* p : coincidencias) mostrarFilaResumen(*p);
    }
}

// Opción 3: listar por tipo (en tipo 1 o tipo 2)
void opcionListarPorTipo(const vector<Pokemon>& lista) {
    cout << "Tipos: Normal, Fuego, Agua, Planta, Eléctrico, Hielo, Lucha, Veneno,\n"
            "       Tierra, Volador, Psíquico, Bicho, Roca, Fantasma, Dragón, Acero, Hada\n"
            "       (también se aceptan en inglés: Fire, Water, Grass...)\n";
    string tipo = normalizarTipo(leerLinea("Tipo: "));
    if (tipo.empty()) {
        cout << "No escribiste nada.\n";
        return;
    }

    int contador = 0;
    cout << "\nPokémon de tipo \"" << tipo << "\":\n";
    for (const Pokemon& p : lista) {
        if (aMinusculas(p.tipo1) == tipo || aMinusculas(p.tipo2) == tipo) {
            mostrarFilaResumen(p);
            contador++;
        }
    }
    if (contador == 0)
        cout << "  (ninguno; revisa que el tipo esté bien escrito)\n";
    else
        cout << "Total: " << contador << " Pokémon.\n";
}

// Opciones 4 y 5: Top 3 por el total de estadísticas
void mostrarTop3(const vector<Pokemon>& lista, bool masFuertes) {
    vector<const Pokemon*> ptrs;
    for (const Pokemon& p : lista) ptrs.push_back(&p);

    size_t n = ptrs.size() < 3 ? ptrs.size() : 3;

    partial_sort(ptrs.begin(), ptrs.begin() + n, ptrs.end(),
        [masFuertes](const Pokemon* a, const Pokemon* b) {
            if (a->total != b->total)
                return masFuertes ? a->total > b->total : a->total < b->total;
            return a->numero < b->numero;       // desempate estable
        });

    cout << "\n=== TOP " << n << (masFuertes ? " MÁS FUERTES" : " MÁS DÉBILES")
         << " (suma total de estadísticas) ===\n";
    for (size_t i = 0; i < n; i++) {
        cout << "\nPuesto " << (i + 1) << ":";
        mostrarPokemon(*ptrs[i]);
    }
}

void mostrarMenu() {
    cout << "\n========================================\n"
            "              POKÉDEX\n"
            "========================================\n"
            " 1. Ver Pokémon por número de Pokédex\n"
            " 2. Buscar Pokémon por nombre\n"
            " 3. Listar Pokémon por tipo\n"
            " 4. Top 3 Pokémon más fuertes\n"
            " 5. Top 3 Pokémon más débiles\n"
            " 6. Salir\n"
            "========================================\n";
}

// ---------------------------------------------------------------------
//  main
// ---------------------------------------------------------------------
int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);    // para tildes y símbolos (♀ ♂) en la consola
#endif

    string ruta = (argc > 1) ? argv[1] : ARCHIVO_CSV;

    vector<Pokemon> pokedex;
    if (!cargarCSV(ruta, pokedex)) {
        cout << "Verifica que el CSV esté en la carpeta desde la que ejecutas el programa.\n";
        return 1;
    }
    cout << "Pokédex cargada: " << pokedex.size() << " Pokémon.\n";

    int opcion = 0;
    do {
        mostrarMenu();
        if (!leerEntero("Elige una opción (1-6): ", opcion)) opcion = 0;

        switch (opcion) {
            case 1: opcionBuscarPorNumero(pokedex); break;
            case 2: opcionBuscarPorNombre(pokedex); break;
            case 3: opcionListarPorTipo(pokedex);   break;
            case 4: mostrarTop3(pokedex, true);     break;
            case 5: mostrarTop3(pokedex, false);    break;
            case 6: cout << "\n¡Hasta pronto, entrenador!\n"; break;
            default: cout << "Opción no válida. Elige un número del 1 al 6.\n";
        }
    } while (opcion != 6);

    return 0;
}