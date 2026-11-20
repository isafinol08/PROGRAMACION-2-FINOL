#!/bin/bash
# Ejecuta todos los casos de uso en orden, partiendo de datos vacios.
# Trabaja en una carpeta temporal: NO toca la carpeta datos/ del proyecto.
# Uso (desde la carpeta del proyecto):  ./pruebas/ejecutar_pruebas.sh [ejecutable]
EXE=$(realpath "${1:-./taller}")
PRUEBAS=$(cd "$(dirname "$0")" && pwd)
TMP=$(mktemp -d)
cd "$TMP" || exit 1

for n in 1a 1b 2 3a 3b 3c 4a 4b 5a 5b 6 7a; do
    "$EXE" < "$PRUEBAS/entrada_$n.txt" > "$PRUEBAS/salida_$n.txt" || echo "FALLO en $n"
done

# CU7 paso 3: reemplazar manualmente vehiculos.bin por la copia del respaldo
mkdir datos_antes && cp datos/*.bin datos_antes/
cp backup_*/vehiculos.bin datos/vehiculos.bin
"$EXE" < "$PRUEBAS/entrada_7b.txt" > "$PRUEBAS/salida_7b.txt" || echo "FALLO en 7b"

# se recupera el estado completo para seguir con el caso 8
cp datos_antes/*.bin datos/
"$EXE" < "$PRUEBAS/entrada_8.txt" > "$PRUEBAS/salida_8.txt" || echo "FALLO en 8"

cd / && rm -rf "$TMP"
echo "Pruebas terminadas. Salidas en: $PRUEBAS"
