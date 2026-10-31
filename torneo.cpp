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
    int indice;
    //Numero de la partida que se juega ahora en esta mesa y los dos jugadores
    int partida;
    int jugador_puma;
    int jugador_perros;
    //Hilo del juez de esta mesa, y si ya se lanzó (para saber si hay que hacerle join)
    pthread_t hilo_juez;
    bool hilo_lanzado;
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

//Datos del servidor compartidos por todos los hilos

//Protege llegadas, quien_llego y mesa_libre
static pthread_mutex_t mutex_servidor;
//Hace que cada mensaje se imprima completo
//El mutex de la mesa o el del servidor se toman antes que el de la consola, y con la consola tomada nunca se pide otro mutex (para evitar deadlock)
static pthread_mutex_t mutex_consola;
//Cuenta los clientes que ya llegaron y todavía no se emparejan
static sem_t sem_llegadas;
//Cuenta las mesas libres, partiendo en M
static sem_t sem_mesas;
//Cuantos clientes han llegado
static int llegadas = 0;
//quien_llego[k] es el número de jugador que tuvo la llegada k
static int quien_llego[max_jugadores];
//mesa_libre[i] es true si la mesa i no esta jugando
static bool mesa_libre[max_mesas];
//Mesas del torneo
static Mesa mesas[max_mesas];
//Numero de cada cliente y su hilo
static int ids_clientes[max_jugadores];
static pthread_t hilos_clientes[max_jugadores];

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


//Preparación del tablero

//Deja el tablero sin ninguna pieza y sin capturas
static void vaciar_tablero(Tablero &t){
    for(int c = 0; c < num_nodos; c++){
        t.ocupante[c] = ninguno;
    }
    for(int p = 0; p < num_piezas; p++){
        t.pos[p] = ninguno;
    }
    t.capturas = 0;
}

//Pone la pieza p en la casilla c
static void colocar_pieza(Tablero &t, int p, int c){
    t.pos[p] = c;
    t.ocupante[c] = p;
}

//Pone la pieza p en la casilla c, pero solo si la casilla existe y esta libre, devuelve false si no se pudo
static bool colocar_pieza_valida(Tablero &t, int p, int c){
    if(c < 0 || c >= num_nodos)
      return false;
    if(t.ocupante[c] != ninguno)
      return false;
    colocar_pieza(t, p, c);
    return true;
}

//Arma la disposición inicial de las piezas. Si config.txt trajo una, usa esa, si no, usa la de por defecto: el puma en el trono y los perros abajo (filas 3 y 4 completas y los dos extremos de la fila 2). Devuelve false si la disposición es invalida (casilla que no existe o repetida)
static bool colocar_inicial(Tablero &t, Config &cfg){
    vaciar_tablero(t);
    int casilla_puma = 0;
    int casillas_perros[num_perros] = {13, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27};
    //Si el archivo de configuración trajo una disposición, reemplaza a la de por defecto
    if(cfg.hay_disposicion){
        casilla_puma = cfg.casilla_puma;
        for(int i = 0; i < num_perros; i++){
            casillas_perros[i] = cfg.casillas_perros[i];
        }
    }
    if(!colocar_pieza_valida(t, 0, casilla_puma))
      return false;
    for(int i = 0; i < num_perros; i++){
        if(!colocar_pieza_valida(t, i + 1, casillas_perros[i]))
          return false;
    }
    return true;
}

//Reglas de movimiento y captura

//Devuelve true si la casilla b es vecina de la casilla a
static bool son_vecinas(int a, int b){
    for(int k = 0; k < num_vecinos[a]; k++){
        if(vecinos[a][k] == b)
          return true;
    }
    return false;
}

//Un paso simple es válido si la pieza sigue en el tablero, el destino está vacío y es vecino de la casilla donde esta la pieza
static bool paso_valido(Tablero &t, int pieza, int destino){
    if(t.pos[pieza] == ninguno)
      return false;
    if(t.ocupante[destino] != ninguno)
      return false;
    return son_vecinas(t.pos[pieza], destino);
}

//Si el puma puede saltar hasta "destino" por encima de un perro, devuelve la casilla de ese perro. Si no puede, devuelve ninguno
static int perro_saltado(Tablero &t, int destino){
    int origen = t.pos[0];
    //El puma tiene que estar en el tablero y el destino tiene que estar vacío
    if(origen == ninguno || t.ocupante[destino] != ninguno)
      return ninguno;
    //Revisa los saltos posibles desde donde esta el puma
    for(int k = 0; k < num_saltos[origen]; k++){
        if(salto_destino[origen][k] == destino){
            int medio = salto_medio[origen][k];
            //Solo se salta si en el medio hay un perro (piezas 1 a 12)
            if(t.ocupante[medio] >= 1)
              return medio;
        }
    }
    return ninguno;
}

//Mueve la pieza a una casilla destino
static void mover_pieza(Tablero &t, int pieza, int destino){
    t.ocupante[t.pos[pieza]] = ninguno;
    t.pos[pieza] = destino;
    t.ocupante[destino] = pieza;
}

//El puma salta hasta "destino" y captura al perro que estaba en casilla_perro
static void capturar_perro(Tablero &t, int casilla_perro, int destino){
    int perro = t.ocupante[casilla_perro];
    t.pos[perro] = ninguno;
    t.ocupante[casilla_perro] = ninguno;
    mover_pieza(t, 0, destino);
    t.capturas++;
}

//El puma esta acorralado si no puede dar ningún paso ni ningún salto
static bool puma_acorralado(Tablero &t){
    int origen = t.pos[0];
    //Si alguna casilla vecina esta vacía, puede dar un paso
    for(int k = 0; k < num_vecinos[origen]; k++){
        if(t.ocupante[vecinos[origen][k]] == ninguno)
          return false;
    }
    //Si puede saltar sobre algún perro, tampoco esta acorralado
    for(int k = 0; k < num_saltos[origen]; k++){
        if(perro_saltado(t, salto_destino[origen][k]) != ninguno)
          return false;
    }
    return true;
}

//Revisa las condiciones de victoria
static Resultado revisar_ganador(Tablero &t){
    if(t.capturas >= capturas_para_ganar)
      return GanaPuma;
    if(puma_acorralado(t))
      return GananPerros;
    return SinGanador;
}

