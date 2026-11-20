#!/bin/bash
# Pruebas de validaciones y manejo de errores (seccion 11 del enunciado).
# Usa una COPIA de datos/ en una carpeta temporal: NO modifica la carpeta datos/ del proyecto.
# Uso (desde la carpeta del proyecto):  bash pruebas/ejecutar_validaciones.sh [ejecutable]
EXE=$(realpath "${1:-./taller}")
PRUEBAS=$(cd "$(dirname "$0")" && pwd)
ORIGEN=$(dirname "$PRUEBAS")/datos
TMP=$(mktemp -d)
cd "$TMP" || exit 1

echo "9a) validaciones de formato y de negocio (sobre datos/ pre-poblados)"
cp -r "$ORIGEN" datos
"$EXE" < "$PRUEBAS/entrada_9a.txt" > "$PRUEBAS/salida_9a.txt" || echo "FALLO en 9a"

echo "9b) archivos inexistentes al iniciar (se borran vehiculos.bin y ordenes.bin)"
rm -rf datos && cp -r "$ORIGEN" datos && rm datos/vehiculos.bin datos/ordenes.bin
"$EXE" < "$PRUEBAS/entrada_9b.txt" > "$PRUEBAS/salida_9b.txt" || echo "FALLO en 9b"

echo "9c) archivo danado (mecanicos.bin con 5 bytes): debe avisar el archivo y terminar con error"
rm -rf datos && cp -r "$ORIGEN" datos && head -c 5 "$ORIGEN/mecanicos.bin" > datos/mecanicos.bin
"$EXE" < "$PRUEBAS/entrada_9c.txt" > "$PRUEBAS/salida_9c.txt"
echo "codigo de salida del programa: $?   (se esperaba 1)" | tee -a "$PRUEBAS/salida_9c.txt"

cd / && rm -rf "$TMP"
echo "Listo. Salidas en: $PRUEBAS"
