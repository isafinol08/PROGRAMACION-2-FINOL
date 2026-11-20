# Carpeta pruebas/

Cada `entrada_X.txt` es **una ejecucion** del programa y esta escrito al estilo del Proyecto 1: contiene **solo lo que se escribe** (opciones de menu, `S`/`N`, datos)
mas lineas que empiezan con `#` (encabezado del caso de uso y comentario de cada paso). El programa ignora esas lineas pero las repite en la salida.
`salida_X.txt` es lo que mostro el programa.

| Archivos | Contenido |
|---|---|
| `entrada_0_poblar.txt` / `salida_0_poblar.txt` | Genera la carpeta `datos/` pre-poblada (8 vehiculos, 4 mecanicos, 12 repuestos, 10 ordenes) |
| `entrada_1a`, `1b` | Caso de uso 1: persistencia entre ejecuciones |
| `entrada_2` | Caso de uso 2: acceso directo por ID |
| `entrada_3a`, `3b`, `3c` | Caso de uso 3: la orden sobrevive con su detalle |
| `entrada_4a`, `4b` | Caso de uso 4: orden con 15 lineas |
| `entrada_5a`, `5b` | Caso de uso 5: anulacion con reversion en disco |
| `entrada_6` | Caso de uso 6: baja de mecanico e historial |
| `entrada_7a`, `7b` | Caso de uso 7: integridad tras una restauracion parcial |
| `entrada_8` | Caso de uso 8: reportes |
| `entrada_9a`, `9b`, `9c` | Validaciones y manejo de errores (datos invalidos, archivos inexistentes, archivo danado) |

Para volver a generar todas las salidas (desde la carpeta del proyecto, despues de compilar):

```
bash pruebas/ejecutar_pruebas.sh ./taller          # casos de uso 1 a 8
bash pruebas/ejecutar_validaciones.sh ./taller     # validaciones 9a, 9b y 9c
```

Ambos scripts trabajan en una carpeta temporal y **no modifican** `datos/`.
