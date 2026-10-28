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

//Estado de una partida, quien esta en cada casilla y donde esta cada pieza
struct Tablero{
    //ocupante[o] es el numero de la pieza que esta en la casilla c
    int ocupante[num_nodos];
    //pos[p] es la casilla donde esta la pieza p
    int pos[num_piezas];
    //Perros capturados hasta ahora
    int capturas;
};

//Resultado de revisar si alguien gano
enum Resultado{SinGanador, GanaPuma, GananPerros};

//Parámetros del torneo que se pueden cambiar con config.txt
struct Config{
    //Retardo mínimo y máximo en ms entre los intentos de movimiento de una pieza
    int retardo_min;
    int retardo_max;
    //Semilla de los números aleatorios, para poder repetir una prueba
    unsigned int semilla;
    //true si config.txt trajo una disposición inicial de las piezas
    bool hay_disposicion;
    //Casilla inicial del puma y de cada uno de los 12 perros
    int casilla_puma;
    int casillas_perros[num_perros];
};

//Una solicitud de movimiento, una pieza pide ir a una casilla
struct Solicitud{
    int pieza;
    int destino;
};

//Declaración adelantada ArgPieza, necesita nombrar a Mesa antes de que Mesa se defina
struct Mesa;

//Lo que recibe el hilo de cada pieza, a que mesa pertenece y cual pieza es
struct ArgPieza{
    Mesa *mesa;
    int pieza;
};

//Una mesa de juego, el tablero y todo lo necesario para sincronizar a las 13 piezas y al juez (ambos hilos)
struct Mesa{
    //Numero de mesa, para identificarla en el log
    int numero;
    //Copia de la configuración, solo se lee después de crearse
    Config cfg;
    //El tablero, solo se toca con el mutex tomado
    Tablero tablero;
    //Mutex de la mesa; protege el tablero, la cola de solicitudes y los datos de abajo
    pthread_mutex_t mutex;
    //Variable de condición, el juez duerme hasta que llegue una solicitud
    pthread_cond_t hay_solicitud;
    //Cola circular con las solicitudes de movimiento que esperan al juez
    Solicitud cola[max_solicitudes];
    //Posición de la solicitud mas antigua de la cola
    int cola_cabeza;
    //Solicitudes que hay en la cola
    int cola_cantidad;
    //true cuando la partida ya terminó
    bool terminada;
    //Muestra quien ganó, SinGanador si se esta jugando
    Resultado resultado;
    //Los 13 hilos de las piezas y los argumentos que recibe cada uno
    pthread_t hilos[num_piezas];
    ArgPieza args[num_piezas];
};

//Construcción de las tablas del tablero

//Anota que la casilla b es vecina de la casilla a
static void agregar_vecino(int a, int b){
    vecinos[a][num_vecinos[a]] = b;
    num_vecinos[a]++;
}

//Anota que desde la casilla a se puede saltar por encima de "medio" hasta "destino"
static void agregar_salto(int a, int medio, int destino){
    salto_medio[a][num_saltos[a]] = medio;
    salto_destino[a][num_saltos[a]] = destino;
    num_saltos[a]++;
}

//Llena las tablas de vecinos y saltos recorriendo cada linea del tablero
static void construir_tablero(){
    for(int l = 0; l < num_lineas; l++){
        //Cuenta cuantas casillas tiene esta linea
        int largo = 0;
        while(lineas[l][largo] != -1){
            largo++;
        }
        //Dos casillas seguidas en la linea son vecinas (en los dos sentidos)
        for(int i = 0; i + 1 < largo; i++){
            agregar_vecino(lineas[l][i], lineas[l][i + 1]);
            agregar_vecino(lineas[l][i + 1], lineas[l][i]);
        }
        //Tres casillas seguidas forman un salto (en los dos sentidos)
        for(int i = 0; i + 2 < largo; i++){
            agregar_salto(lineas[l][i], lineas[l][i + 1], lineas[l][i + 2]);
            agregar_salto(lineas[l][i + 2], lineas[l][i + 1], lineas[l][i]);
        }
    }
}



