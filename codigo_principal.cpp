#include <iostream>
#include <string>
#include <vector>
#include <iomanip>
#include <limits>
#include <sstream>
#include <algorithm>
#include <cctype>

//  SECCION 1: UTILIDADES DE ENTRADA Y FORMATO

using namesapce std;

void limpiarBuffer() {
    cin.clear();
    cin.ignore(numeric_limits<streamsize>::max(), '\n');
}

string aMAyusculas(string s){
    for (size_t i = 0; i < s.size(); i++)
        s[i] = toupper(static_cast<unsigned char>(s[i]));
    return s;
}

string aMinusculas(string s){
    for (size_t i = 0; i < s.size(); i++)
        s[i] = toupper(static_cast<unsigned char>(s[i]));
    return s;
}

bool contiene(const string& texto, const string& sub){
    return aMinusculas(texto).find(aMinusculas(sub)) != string::npos;
}

int leerEntero(const string& msg, int min, int max) {
    int v;
    while (true) {
        cout << msg;
        if (cin >> v) {
            limpiarBuffer();
            if (v >= min && v <= max) return v;
            cout << " [!] Valor fuera de rango (" << min << " - " << max << ").\n";
        } else {
            cout << " [!] Entrada inavalida: se esperaba un numero entero.\n";
            limpiarBuffer();
        }
    }
}

string leerFecha(const string& msg) {
    string s;
    while (true) {
        cout << msg;
        getline(cin, s);
        if (s.size() == 10 && s[4] == '-' && s[7] == '-'){
            bool ok = true;
            for (int i = 0; i< 10; i++) {
                if (i == 4 || i == 7) continue;
                if (!isdigit(static_cast<unsigned char>(s[i]))) { ok = false; break; }
            }
            if (ok) {
                int mes = atoi(s.substr(5, 2).c_str());
                int dia = atoi(s.substr(8, 2).c_str());
                if (mes >= 1 && mes <= 12 && dia >= 1 && dia <= 31) return s;
            }
        }
        cout << " [!] Formato invalido. Usa AAAA-MM-DD (ej: 2026-10-).\n";
    }
}

bool confirmar(const string& msg) {
    string r;
    while (true){
        cout << msg << " (S/N): ";
        getline(cin, r);
        if (r == "s" || r == "S") return true;
        if (r == "n" || r == "N") return false;
        cout << " [!] Responda S o N. \n";
    }
}

string dinero(double m) {
    ostringstream oss;
    oss << fixed << setprecision(2) << m;
    return oss.str();
}

void linea(char c = '-', int n = 78) { cout << string(n, c) << "\n"; }