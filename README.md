###Torneo de Komikan Concurrente - Tarea 2 Sistemas Operativos

Integrantes: Fernando Letelier - Jorge Latorre

1.- Descripción

Simulador de un torneo multijugador de Komikan en tiempo real. Un servidor recibe N jugadores, los empareja de a 2 ,uno juega de Puma y el otro de Perros, y les asigna una mesa de juego, sin pasar nunca de M mesas jugando al mismo tiempo. En cada mesa hay un hilo juez y 13 hilos de piezas (1 puma y 12 perros) que se mueven de forma simultánea, sin turnos, con retardos aleatorios.

Reglas que se cumplen:

- Las piezas nunca modifican el tablero: piden el movimiento al juez y solo el juez lo aplica, por lo que dos piezas que quieren la misma casilla nunca la obtienen a la vez.
- Nunca hay más de M mesas activas. Si las M están ocupadas, las parejas restantes esperan de forma pasiva.
- El Puma gana si captura 6 o más perros. Los Perros ganan si dejan al Puma sin movimientos válidos (ni pasos ni saltos).
- Si un jugador falla en medio de una partida, el juez lo detecta, aborta la partida y declara ganador a su oponente por abandono.
- Con Ctrl+C el servidor deja de aceptar jugadores, las partidas activas terminan de forma ordenada y se liberan todos los recursos.

Tablero: 25 casillas de la cuadrícula de 5 x 5, 2 casillas intermedias del triángulo y 1 trono (28 casillas en total), numeradas de arriba hacia abajo:

                  0              <- trono
              1       2          <- intermedias del triángulo
          3   4   5   6   7      <- fila 0 de la cuadrícula
          8   9  10  11  12
         13  14  15  16  17
         18  19  20  21  22
         23  24  25  26  27      <- fila 4 de la cuadrícula

Las conexiones y los saltos se calculan a partir de las 13 líneas rectas del tablero (filas, columnas, las dos diagonales del triángulo y la línea entre las dos intermedias). Dos casillas seguidas en una línea son vecinas y tres seguidas permiten un salto.

2.- Compilación y uso

Compilar:

    g++ -Wall -Wextra -std=c++17 -o torneo torneo.cpp -lpthread

Ejecutar:

    ./torneo N M [config.txt]

N es la cantidad de jugadores y M es el máximo de mesas jugando a la vez. Ambos deben ser enteros positivos. N no puede ser mayor que 100000 y si M es mayor que 1000 se usan 1000 mesas. config.txt es opcional. Si faltan argumentos, N o M no son válidos, el archivo no se puede abrir o tiene un error, el programa muestra un mensaje y termina con código 1.

Formato de config.txt, lista de palabras clave, cada una seguida de su(s) valor(es), separados por espacios o saltos de línea, en cualquier orden:

    retardo_min 50
    retardo_max 300
    semilla 1234
    puma 0
    perros 13 17 18 19 20 21 22 23 24 25 26 27
    abandono 10

- retardo_min / retardo_max: retardo mínimo y máximo (ms) entre los intentos de movimiento de una pieza. Por defecto 50 y 300.
- semilla: semilla de los números aleatorios. Por defecto es la hora actual.
- puma: casilla inicial del puma. Por defecto 0 (el trono).
- perros: casillas iniciales de los 12 perros. Por defecto 13, 17, 18 a 27.
- abandono: probabilidad (0 a 100) de que un jugador abandone una partida. Por defecto 10.

Lo que no aparezca en el archivo queda con su valor por defecto. Si se entrega "puma" o "perros", la disposición completa del archivo reemplaza a la de por defecto y debe ser válida (casillas existentes y sin repetir).

Ejemplo:

    ./torneo 40 4 config.txt

Con este comando llegan 40 jugadores, se forman 20 partidas y nunca hay más de 4 mesas jugando a la vez. Las demás parejas esperan a que se libere una mesa.

Como probar el abandono: ejecutar con "abandono 100" en config.txt. En cada partida un jugador falla entre su ciclo de decisión 5 y 20, y el juez declara ganador a su oponente.

Como probar la interrupción: ejecutar "./torneo 40 2" y presionar Ctrl+C durante la ejecución. Las partidas activas terminan con el mensaje "se interrumpió por SIGINT" y al final se imprime "Todas las partidas terminaron".

3.- Funciones Implementadas

Tablero y reglas:
- agregar_vecino: anota que una casilla es vecina de otra.
- agregar_salto: anota que desde una casilla se puede saltar sobre otra hasta una tercera.
- construir_tablero: llena las tablas de vecinos y saltos recorriendo cada línea del tablero.
- vaciar_tablero: deja el tablero sin piezas y sin capturas.
- colocar_pieza: pone una pieza en una casilla.
- colocar_pieza_valida: igual que la anterior, pero solo si la casilla existe y está libre.
- colocar_inicial: arma la disposición inicial (la del archivo o la de por defecto) y devuelve false si es inválida.
- son_vecinas: indica si dos casillas son vecinas.
- paso_valido: revisa que un paso simple cumpla las reglas de adyacencia y que el destino esté libre.
- perro_saltado: si el puma puede saltar hasta un destino sobre un perro, devuelve la casilla de ese perro.
- mover_pieza: mueve una pieza a una casilla (la validación se hace antes).
- capturar_perro: el puma salta y captura al perro.
- puma_acorralado: indica si el puma no tiene ningún paso ni salto posible.
- revisar_ganador: revisa las condiciones de victoria.
- simbolo / imprimir_tablero / imprimir_pieza: imprimen el tablero (P puma, D perro, . vacía) y el nombre de la pieza.

Mesa (piezas y juez):
- elegir_destino: elige al azar uno de los movimientos válidos de una pieza (paso o, si es el puma, salto con captura).
- turno_de_pieza: lo que hace una pieza en un ciclo con el mutex de la mesa tomado: revisa si debe terminar, cuenta el ciclo, falla si le toca y deja una solicitud en la cola.
- soltar_mutex: cleanup handler que suelta el mutex si un hilo muere con él tomado.
- hilo_pieza: hilo de una pieza. Repite: esperar un retardo aleatorio y ejecutar su turno con el mutex tomado.
- atender_solicitud: el juez revisa una solicitud y, si cumple las reglas, la aplica y muestra el tablero; si no, la rechaza.
- hilo_juez: hilo del juez. Inicializa la mesa, lanza las 13 piezas, atiende solicitudes, detecta al ganador o al abandono, espera a las piezas, anuncia el resultado, destruye las primitivas de la mesa y la libera.

Servidor:
- hilo_cliente: hilo de un jugador. Se anota en el orden de llegada (llegada par = Puma, impar = Perros) y avisa con un semáforo.
- ejecutar_servidor: crea los clientes, empareja de a 2 (llegadas 2p y 2p+1), espera una mesa libre, lanza el juez de cada partida y al final espera a todos los hilos y destruye las primitivas.
- manejador_seremi / manejar_sigint: manejador de SIGINT (Ctrl+C). Solo marca una bandera y despierta al hilo principal con sem_post.

Configuración y main:
- config_por_defecto: asigna los valores por defecto.
- leer_config: lee config.txt, valida los valores y devuelve false si hay un error.
- main: valida argumentos, lee la configuración, construye el tablero, comprueba la disposición inicial e inicia el servidor.

Cada mesa guarda su estado (tablero, cola de solicitudes, si terminó, quién ganó, si fue por abandono) y lo protege con su propio mutex.

4.- Decisiones de Diseño

Exclusión mutua en el tablero (un juez por mesa):
Cada mesa tiene un mutex que protege el tablero, la cola de solicitudes y los datos de la partida. Las piezas no mueven nada: eligen un movimiento y dejan una solicitud en una cola circular. El juez es el único que modifica el tablero, las atiende de a una en orden de llegada y las vuelve a validar. Así, si dos piezas piden la misma casilla, solo la primera la obtiene y la otra recibe el rechazo "ya no era posible". No hay race conditions porque todo acceso al tablero ocurre con el mutex de la mesa tomado.

Espera pasiva, sin busy-waiting:
El juez duerme en pthread_cond_wait hasta que llegue una solicitud, un abandono o un SIGINT. Las piezas esperan con usleep. El servidor usa dos semáforos: sem_llegadas cuenta los clientes que llegaron y no se emparejan, y sem_mesas parte en M y cuenta las mesas libres, por lo que nunca puede haber más de M mesas jugando. Si están todas ocupadas, el emparejador se duerme en sem_wait. El pthread_cond_wait se repite en un while por si el hilo despierta sin que haya nada que atender.

Orden de los mutex (prevención de deadlock):
El mutex de la mesa o el del servidor se toman siempre antes que el de la consola, y con la consola tomada nunca se pide otro mutex. Además, el juez suelta el mutex de la mesa antes de hacer join de las piezas, porque ellas necesitan tomarlo para darse cuenta de que la partida terminó. La mesa se marca como libre antes de hacer sem_post, así el emparejador siempre la encuentra libre al despertar.

Livelock y starvation:
No hay livelock porque las piezas no reintentan en un ciclo cerrado: si su movimiento es rechazado, esperan un retardo aleatorio nuevo y eligen de nuevo según el tablero actual. No hay starvation porque la cola es FIFO (se atiende por orden de llegada) y las parejas se emparejan y reciben mesa en orden. Si la cola de una mesa está llena (32 solicitudes), la pieza no deja su solicitud y lo intenta en su próximo ciclo.

Fallo de un jugador (abandono):
Con probabilidad "abandono" se sortea en cada partida un jugador que falla, y en cuál de sus ciclos de decisión (entre el 5 y el 20). Cuando le toca, el hilo de la pieza avisa al juez y muere con pthread_exit teniendo el mutex tomado. El cleanup handler (pthread_cleanup_push) suelta el mutex, así la mesa no queda bloqueada. El juez detecta el abandono, termina la partida, declara ganador al oponente y destruye las primitivas de la mesa.

Interrupción del servidor (Ctrl+C):
El manejador de SIGINT (instalado con sigaction, sin SA_RESTART) solo marca una variable volatile sig_atomic_t y hace sem_post a los dos semáforos, para despertar al hilo principal si estaba dormido. El servidor deja de crear clientes y de emparejar. Cada pieza ve la bandera, avisa al juez con pthread_cond_signal y termina. El juez marca la partida como terminada sin ganador, hace join de sus piezas, destruye sus primitivas y libera la mesa. Al final el servidor hace join de todos los hilos y destruye los semáforos y mutex. Las parejas que estaban esperando mesa no juegan.

Reproducibilidad:
La semilla de cada partida es semilla + 100 x (número de partida) y la de cada pieza es la semilla de la partida + el número de pieza. Con la misma semilla se repiten los sorteos (abandonos y elecciones de movimiento), pero el orden exacto de los hilos depende del planificador del sistema operativo, por lo que las partidas completas pueden variar entre ejecuciones.

Evaluación (complejidad, rendimiento y concurrencia):
Un solo juez por mesa que atiende solicitudes hace la solución simple de razonar y evita bloqueos entre piezas, a cambio de que los movimientos de una misma mesa se validan de a uno. Esto no es un problema real porque las piezas se mueven cada 50 a 300 ms. Entre mesas sí hay concurrencia total, ya que cada una tiene su propio mutex y su propia variable de condición, y solo comparten la consola y el servidor, que se toman por muy poco tiempo.

5.- Limitaciones
- Máximo 100000 jugadores, 1000 mesas y 32 solicitudes pendientes por mesa.
- Los movimientos de las piezas son aleatorios, por lo que no hay estrategia y una partida podría durar bastante. No hay límite de duración por partida.
- El abandono es simulado (lo sortea el juez al iniciar la partida); un jugador no falla por sí solo.
- Si N es impar, el último jugador se queda sin rival y no juega.
- Si se interrumpe con Ctrl+C, las partidas activas terminan sin ganador y las parejas que esperaban mesa no juegan.
- Se asume que config.txt tiene el formato correcto; los errores que se detectan son palabras desconocidas, valores no numéricos, retardos inválidos, abandono fuera de 0 a 100 y disposiciones inválidas.
