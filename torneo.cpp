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

//Impresión del tablero

//Letra que representa P puma, D perro, . vacia
static char simbolo(Tablero &t, int casilla){
    int p = t.ocupante[casilla];
    if(p == ninguno)
      return '.';
    if(p == 0)
      return 'P';
    return 'D';
}

//Impresión del tablero con la forma de la figura del enunciado
static void imprimir_tablero(Tablero &t){
    cout << "      " << simbolo(t, 0) << endl;
    cout << "    " << simbolo(t, 1) << "   " << simbolo(t, 2) << endl;
    for(int fila = 0; fila < 5; fila++){
        cout << "  ";
        for(int col = 0; col < 5; col++){
            cout << simbolo(t, 3 + fila * 5 + col) << " ";
        }
        cout << endl;
    }
}

//Imprime Puma o Perro 
static void imprimir_pieza(int pieza){
    if(pieza == 0)
      cout << "Puma";
    else
      cout << "Perro " << pieza;
}

//Elige al azar uno de los movimientos validos de la pieza, un paso a una casilla libre si es el puma, también un salto con captura. Devuelve ninguno si no tiene ningún movimiento. "estado" es el generador aleatorio de la pieza. Se llama con el mutex de la mesa tomado, porque lee el tablero.
static int elegir_destino(Tablero &t, int pieza, unsigned int &estado){
    int origen = t.pos[pieza];
    //Un paso por cada vecina y un salto por cada vecina, a lo más 2 * max_vecinos
    int candidatos[2 * max_vecinos];
    int cantidad = 0;
    //Pasos, las casillas vecinas que están libres
    for(int k = 0; k < num_vecinos[origen]; k++){
        if(t.ocupante[vecinos[origen][k]] == ninguno){
            candidatos[cantidad] = vecinos[origen][k];
            cantidad++;
        }
    }
    //Saltos, solo el puma y solo si hay un perro que saltar
    if(pieza == 0){
        for(int k = 0; k < num_saltos[origen]; k++){
            if(perro_saltado(t, salto_destino[origen][k]) != ninguno){
                candidatos[cantidad] = salto_destino[origen][k];
                cantidad++;
            }
        }
    }
    if(cantidad == 0)
      return ninguno;
    //rand_r usa el estado, asi cada pieza tiene su propia secuencia
    return candidatos[rand_r(&estado) % cantidad];
}

//Hilo de una pieza. Esperar un retardo aleatorio, elige un movimiento y PEDIR al juez que lo haga (poniendo una solicitud en la cola). La pieza nunca modifica el tablero, solo lo hace el juez.
static void *hilo_pieza(void *arg){
    //El argumento llega como puntero genérico 
    ArgPieza &a = *(ArgPieza *)arg;
    Mesa &mesa = *a.mesa;
    int pieza = a.pieza;
    //Semilla propia de esta pieza, calculada a partir de la semilla de la configuración
    unsigned int estado = mesa.cfg.semilla + pieza;

    while(true){
        //Retardo aleatorio entre retardo_min y retardo_max, en milisegundos
        int espera = mesa.cfg.retardo_min + rand_r(&estado) % (mesa.cfg.retardo_max - mesa.cfg.retardo_min + 1);
        usleep(espera * 1000);

        pthread_mutex_lock(&mesa.mutex);
        //La pieza termina si la partida acabo o si la capturaron
        if(mesa.terminada || mesa.tablero.pos[pieza] == ninguno){
            pthread_mutex_unlock(&mesa.mutex);
            break;
        }
        int destino = elegir_destino(mesa.tablero, pieza, estado);
        //Si encontró un movimiento y la cola no esta llena, deja su solicitud
        if(destino != ninguno && mesa.cola_cantidad < max_solicitudes){
            int posicion = (mesa.cola_cabeza + mesa.cola_cantidad) % max_solicitudes;
            mesa.cola[posicion].pieza = pieza;
            mesa.cola[posicion].destino = destino;
            mesa.cola_cantidad++;
            //Despierta al juez, que esta durmiendo esperando solicitudes
            pthread_cond_signal(&mesa.hay_solicitud);
        }
        pthread_mutex_unlock(&mesa.mutex);
    }
    return NULL;
}

//El juez revisa una solicitud, si cumple las reglas de adyacencia y captura, la aplica al tablero y muestra el tablero, si no, la rechaza. Se llama con el mutex de la mesa tomado.
static void atender_solicitud(Mesa &mesa, Solicitud &s){
    Tablero &t = mesa.tablero;
    int origen = t.pos[s.pieza];
    //Si la pieza ya fue capturada, no hay nada que hacer
    if(origen == ninguno)
      return;
    cout << "[Mesa " << mesa.numero << "] ";
    imprimir_pieza(s.pieza);
    if(paso_valido(t, s.pieza, s.destino)){
        mover_pieza(t, s.pieza, s.destino);
        cout << " se mueve de " << origen << " a " << s.destino << endl;
    }
    else{
        //Si no es un paso válido, solo el puma puede estar pidiendo un salto
        int casilla_perro = ninguno;
        if(s.pieza == 0)
          casilla_perro = perro_saltado(t, s.destino);
        if(casilla_perro != ninguno){
            int perro = t.ocupante[casilla_perro];
            capturar_perro(t, casilla_perro, s.destino);
            cout << " salta de " << origen << " a " << s.destino << " y captura al Perro " << perro << " (capturas: " << t.capturas << ")" << endl;
        }
        else{
            //La casilla se ocupo después de que la pieza la eligió, otra pieza llego antes
            cout << " queria ir de " << origen << " a " << s.destino << ", pero ya no era posible" << endl;
            return;
        }
    }
    imprimir_tablero(t);
}

//Hilo del juez de una mesa; inicializa todo, lanza las piezas, atiende sus solicitudes de a una, detecta al ganador y deja la mesa limpia.
static void *hilo_juez(void *arg){
    Mesa &mesa = *(Mesa *)arg;
    //1. Inicializa el tablero y las primitivas de sincronización
    colocar_inicial(mesa.tablero, mesa.cfg);
    pthread_mutex_init(&mesa.mutex, NULL);
    pthread_cond_init(&mesa.hay_solicitud, NULL);
    mesa.cola_cabeza = 0;
    mesa.cola_cantidad = 0;
    mesa.terminada = false;
    mesa.resultado = SinGanador;
    mesa.por_abandono = false;
    mesa.abandono = ninguno;
    mesa.ciclos[0] = 0;
    mesa.ciclos[1] = 0;
    //Sortea la falla simulada de esta partida: con probabilidad prob_abandono un jugador abandona, y se sortea cual y en cual de sus ciclos. Sale de la semilla, así se repite.
    unsigned int sorteo = mesa.cfg.semilla + num_piezas;
    bool habra_falla = (rand_r(&sorteo) % 100) < mesa.cfg.prob_abandono;
    int rol_falla = rand_r(&sorteo) % 2;
    mesa.falla_ciclos = 5 + rand_r(&sorteo) % 16;
    mesa.falla_rol = habra_falla ? rol_falla : ninguno;

    //2. Lanza un hilo por cada pieza (la 0 es el puma, las 1 a 12 son los perros)
    int creadas = 0;
    while(creadas < num_piezas){
        mesa.args[creadas].mesa = &mesa;
        mesa.args[creadas].pieza = creadas;
        if(pthread_create(&mesa.hilos[creadas], NULL, hilo_pieza, &mesa.args[creadas]) != 0)
          break;
        creadas++;
    }
    if(creadas == num_piezas){
        //3. Atiende solicitudes hasta que alguien gane
        pthread_mutex_lock(&mesa.mutex);
        pthread_mutex_lock(&mutex_consola);
        cout << "[Mesa " << mesa.numero << "] Comienza la partida " << mesa.partida << ": Jugador " << mesa.jugador_puma << " (Puma) contra Jugador " << mesa.jugador_perros << " (Perros)" << endl;
        imprimir_tablero(mesa.tablero);
        pthread_mutex_unlock(&mutex_consola);
        while(!mesa.terminada){
            //Espera pasiva, pthread_cond_wait suelta el mutex mientras duerme (así las piezas pueden dejar solicitudes) y lo vuelve a tomar al despertar. Se repite en un while porque un hilo puede despertar sin que haya nada que atender. Despierta por una solicitud nueva o porque un jugador falló (abandonó).
            while(mesa.cola_cantidad == 0 && mesa.abandono == ninguno){
                pthread_cond_wait(&mesa.hay_solicitud, &mesa.mutex);
            }
            //Si un jugador falló, el juez lo detecta, aborta la partida y gana su oponente
            if(mesa.abandono != ninguno){
                mesa.por_abandono = true;
                mesa.resultado = (mesa.abandono == 0) ? GananPerros : GanaPuma;
                mesa.terminada = true;
                pthread_mutex_lock(&mutex_consola);
                cout << "[Mesa " << mesa.numero << "] El Jugador " << (mesa.abandono == 0 ? mesa.jugador_puma : mesa.jugador_perros) << " (" << (mesa.abandono == 0 ? "Puma" : "Perros") << ") fallo internamente: se declara ganador a su oponente" << endl;
                pthread_mutex_unlock(&mutex_consola);
            }
            //Saca y atiende todas las solicitudes que hay, de la mas antigua a la mas nueva
            while(mesa.cola_cantidad > 0 && !mesa.terminada){
                Solicitud s = mesa.cola[mesa.cola_cabeza];
                mesa.cola_cabeza = (mesa.cola_cabeza + 1) % max_solicitudes;
                mesa.cola_cantidad--;
                atender_solicitud(mesa, s);
                mesa.resultado = revisar_ganador(mesa.tablero);
                if(mesa.resultado != SinGanador){
                    mesa.terminada = true;
                }
            }
        }
        //Se suelta el mutex antes de esperar a las piezas: ellas necesitan tomarlo para darse cuenta de que la partida termino.
        pthread_mutex_unlock(&mesa.mutex);
    }
    else{
        //El sistema no dio hilos suficientes: esta partida no se puede jugar. Se avisa a las piezas que ya se crearon para que terminen.
        pthread_mutex_lock(&mesa.mutex);
        mesa.terminada = true;
        pthread_mutex_unlock(&mesa.mutex);
        pthread_mutex_lock(&mutex_consola);
        cout << "[Mesa " << mesa.numero << "] No se pudieron crear los hilos de las piezas: la partida "
             << mesa.partida << " no se juega" << endl;
        pthread_mutex_unlock(&mutex_consola);
    }
    //4. Espera a que terminen los 13 hilos de las piezas
    for(int p = 0; p < num_piezas; p++){
        pthread_join(mesa.hilos[p], NULL);
    }

    //5. Anuncia el resultado y destruye las primitivas de sincronización
    if(creadas == num_piezas){
        pthread_mutex_lock(&mutex_consola);
        cout << "[Mesa " << mesa.numero << "] FIN de la partida " << mesa.partida << ": ";
        if(mesa.por_abandono){
            if(mesa.resultado == GanaPuma)
              cout << "gana el Puma, Jugador " << mesa.jugador_puma << ", por abandono del Jugador " << mesa.jugador_perros << " (Perros)" << endl;
            else
              cout << "ganan los Perros, Jugador " << mesa.jugador_perros << ", por abandono del Jugador " << mesa.jugador_puma << " (Puma)" << endl;
        }
        else if(mesa.resultado == GanaPuma)
          cout << "gana el Puma, Jugador " << mesa.jugador_puma << " (capturas: " << mesa.tablero.capturas << ")" << endl;
        else
          cout << "ganan los Perros, Jugador " << mesa.jugador_perros << " (el Puma quedo acorralado, capturas: " << mesa.tablero.capturas << ")" << endl;
        pthread_mutex_unlock(&mutex_consola);
    }

    pthread_cond_destroy(&mesa.hay_solicitud);
    pthread_mutex_destroy(&mesa.mutex);

    //6. Libera la mesa. Primero se anota como libre y después se avisa con el semáforo: así, cuando el emparejador despierte, ya va a encontrar la mesa marcada como libre.
    int indice = mesa.indice;
    pthread_mutex_lock(&mutex_servidor);
    mesa_libre[indice] = true;
    pthread_mutex_unlock(&mutex_servidor);
    sem_post(&sem_mesas);
    return NULL;
}

//Valores por defecto, los retardos del enunciado, una semilla distinta en cada ejecución y la disposición inicial por defecto
static void config_por_defecto(Config &cfg){
    cfg.retardo_min = retardo_min_defecto;
    cfg.retardo_max = retardo_max_defecto;
    cfg.semilla = (unsigned int)time(NULL);
    cfg.hay_disposicion = false;
    cfg.casilla_puma = 0;
    for(int i = 0; i < num_perros; i++){
        cfg.casillas_perros[i] = 0;
    }
}


//Servidor, llegada de clientes, emparejamiento y límite de mesas

//Hilo de un jugador: llega al servidor y se anota. Su número de llegada decide su rol: los de llegada par (0, 2, 4, etc) juegan de Puma y los de llegada impar (1, 3, 5, etc) de Perros. La llegada 2p y la 2p+1 forman la partida p.
static void *hilo_cliente(void *arg){
    int id = *(int *)arg;
    //Muchos clientes llegan a la vez: el contador de llegadas se protege con el mutex
    pthread_mutex_lock(&mutex_servidor);
    int llegada = llegadas;
    llegadas++;
    quien_llego[llegada] = id;
    //Avisa al emparejador que llego un cliente mas. Se hace con el mutex tomado para que los avisos salgan en el mismo orden de las llegadas
    sem_post(&sem_llegadas);
    pthread_mutex_lock(&mutex_consola);
    cout << "Jugador " << id << " llega al servidor (llegada " << llegada << ": " << (llegada % 2 == 0 ? "Puma" : "Perros") << ")" << endl;
    pthread_mutex_unlock(&mutex_consola);
    pthread_mutex_unlock(&mutex_servidor);
    return NULL;
}

//El servidor: recibe a los n clientes, los empareja de a 2 y abre una mesa por pareja, sin pasar nunca de m mesas jugando a la vez. Lo ejecuta el hilo principal.
static void ejecutar_servidor(int n, int m, Config &cfg){
    //Nunca se usan mas mesas que las que caben en el arreglo (siempre son menos que M)
    int mesas_max = m;
    if(mesas_max > max_mesas){
        mesas_max = max_mesas;
        cout << "Aviso: se usaran como máximo " << max_mesas << " mesas" << endl;
    }

    //Inicializa las primitivas del servidor
    pthread_mutex_init(&mutex_servidor, NULL);
    pthread_mutex_init(&mutex_consola, NULL);
    sem_init(&sem_llegadas, 0, 0);
    sem_init(&sem_mesas, 0, mesas_max);
    llegadas = 0;
    for(int i = 0; i < mesas_max; i++){
        mesa_libre[i] = true;
        mesas[i].hilo_lanzado = false;
    }
 

//Lee config.txt. El formato es una lista de palabras clave, cada una seguida de sus valores , separados por espacios o saltos de linea, en cualquier orden como retardo_min 50, retardo_max 300, semilla 12345, puma 0, perros 13 17 18 19 20 21 22 23 24 25 26 27, Lo que no aparezca en el archivo queda con su valor por defecto.
//Devuelve false si no se puede abrir o si tiene un error.
static bool leer_config(const char *ruta, Config &cfg){
    ifstream archivo(ruta);
    if(!archivo.is_open()){
        cout << "Error: no se pudo abrir el archivo de configuracion " << ruta << endl;
        return false;
    }
    string clave;
    //Lee palabra por palabra; cada palabra clave dice que valores vienen después
    while(archivo >> clave){
        if(clave == "retardo_min"){
            archivo >> cfg.retardo_min;
        }
        else if(clave == "retardo_max"){
            archivo >> cfg.retardo_max;
        }
        else if(clave == "semilla"){
            archivo >> cfg.semilla;
        }
        else if(clave == "puma"){
            archivo >> cfg.casilla_puma;
            cfg.hay_disposicion = true;
        }
        else if(clave == "perros"){
            for(int i = 0; i < num_perros; i++){
                archivo >> cfg.casillas_perros[i];
            }
            cfg.hay_disposicion = true;
        }
        else{
            cout << "Error: palabra desconocida en el archivo de configuracion: " << clave << endl;
            return false;
        }
    }
    //Si la lectura se detuvo por un valor que no es un numero, y no por llegar al final del archivo , el archivo esta mal escrito
    if(archivo.fail() && !archivo.eof()){
        cout << "Error: valor invalido en el archivo de configuracion" << endl;
        return false;
    }
    //Los retardos tienen que tener sentido
    if(cfg.retardo_min < 0 || cfg.retardo_max < cfg.retardo_min){
        cout << "Error: retardo_min no puede ser negativo ni mayor que retardo_max" << endl;
        return false;
    }
    return true;
}

