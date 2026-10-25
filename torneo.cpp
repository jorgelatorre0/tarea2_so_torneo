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

