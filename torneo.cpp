#include <iostream>
#include <fstream> 
#include <string>
#include <cstdlib>
#include <ctime>
#include <pthread.h>
#include <unistd.h>
using namespace std;

//Tablero y reglas

//Constantes

//Casillas del tablero
#define num_nodos 28
//Cantidad de perros
#define num_perros 12
//Piezas en total
#define num_piezas 13
//Valor de casilla sin pieza, que ya no esta en el tablero, o no hay salto
#define ninguno -1
//Máximo de vecinos
#define max_vecinos 4
//Perros que el puma debe capturar para ganar
#define capturas_para_ganar 6
//Cantidad de lineas rectas que tiene el tablero
#define num_lineas 13
//Espacio por linea (6 casillas como máximo + 1 marca de fin (-1))
#define largo_linea 7
//Retardos por defecto entre movimientos en milisegundos
#define retardo_min_defecto 50
#define retardo_max_defecto 300
//Solicitudes de movimiento que caben esperando en la cola de una mesa
#define max_solicitudes 32

static const int lineas[num_lineas][largo_linea]={{3, 4, 5, 6, 7, -1},{8, 9, 10, 11, 12, -1},{13, 14, 15, 16, 17, -1},{18, 19, 20, 21, 22, -1},{23, 24, 25, 26, 27, -1},{3, 8, 13, 18, 23, -1},{1, 4, 9, 14, 19, 24, -1},{0, 5, 10, 15, 20, 25, -1},{2, 6, 11, 16, 21, 26, -1},{7, 12, 17, 22, 27, -1},{0, 1, 3, -1},{0, 2, 7, -1},{1, 2, -1}};

//vecinos[a][k] es la casilla k vecina de la casilla a
static int vecinos[num_nodos][max_vecinos];
//cuantas vecinas tiene cada casilla
static int num_vecinos[num_nodos];
//Un salto se describe con 3 casillas, se sale de "a", se pasa por encima de salto_medio[a][k] y se cae en salto_destino[a][k]
static int salto_medio[num_nodos][max_vecinos];
static int salto_destino[num_nodos][max_vecinos];
//cuantos saltos posibles tiene cada casilla
static int num_saltos[num_nodos];

