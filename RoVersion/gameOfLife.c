#define _GNU_SOURCE
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#include <semaphore.h>
///dragulicid@yahoo.com
#define SEM_CREEAZA_VIETATE "/creeazaVietate"
#define SEM_PLASEAZA_VIETATE "/plaseazaVietate"
#define MarimeMatrice 100
#define MarimeQueue MarimeMatrice * MarimeMatrice

typedef enum {
    M,
    F
}Sex;

typedef struct {
    int E;
}Planta;

Planta creazaPlanta() {
    Planta planta;
    planta.E = 100;
    return planta;
}

typedef struct {
    Sex sex;
    int E;
    int gestatie;
}Ierbivor;


Ierbivor creazaIerbivor(Sex sex) {
    Ierbivor ierbivor;
    ierbivor.E = 40;
    ierbivor.sex= sex;
    ierbivor.gestatie = -1;
    return ierbivor;
}

typedef struct {
    Sex sex;
    unsigned int E;
    int gestatie;
}Carnivor;

Carnivor creazaCarnivor(Sex sex) {
    Carnivor carnivor;
    carnivor.E = 60;
    carnivor.gestatie = -1;
    carnivor.sex = sex;
    return carnivor;
}

typedef enum {
    LIBER,
    P,
    I,
    C
}TipVietate;

typedef union {
    Planta planta;
    Ierbivor ierbivor;
    Carnivor carnivor;
}Vietati;

typedef struct {
    pthread_mutex_t lock;
    Vietati vietate;
    TipVietate tipVietate;
}ContainerVietate;
///

typedef struct {
    int x, y;
} Point;

typedef struct {
    Point point;
    TipVietate tipVietate;
}VietateNoua;

typedef struct {
    bool isEmpty;
    int position, nextAvailablePositon, size;
    pthread_mutex_t lock;
    VietateNoua data[MarimeQueue];
} QueueThread;

typedef struct {
    ContainerVietate m[MarimeMatrice][MarimeMatrice];
    QueueThread queueThread;
}Matrice;

typedef struct QueueNode {
    Point point;
    int dist;
    struct QueueNode *prev;  // Pointer to the previous QueueNode
} QueueNode;

typedef struct {
    QueueNode *data;
    int front, rear;
} Queue;

Matrice *matrice_ptr;

// Directions for 4-connected grid (up, down, left, right)
int dirX[] = { -1, 1, 0, 0 };
int dirY[] = { 0, 0, -1, 1 };

///////////////////////////
bool incaPotRula = true;
pthread_mutex_t threadMutex;
pthread_mutex_t ReadWriteMutex;
sem_t * semaforProducator;
sem_t  * semaforConsumator; 
pid_t groupId = 0; 
int shm_id;
WINDOW *matrix_win; 

Queue* createQueue(int size);
void enqueue(Queue* q, int x, int y, int dist, QueueNode* prev);
QueueNode* dequeue(Queue* q);
// BFS function to find the nearest 'P' to the given 'I' in the matrix
Point cautaPlantaIn10x10(int startX, int startY, Matrice * matricePtr);
Point cautaIerbivorIn10x10(int startX, int startY, bool cautaDoarFemele, Matrice * matricePtr);
Point cautaCarnivorFIn10x10(int startX, int startY, Matrice * matricePtr);
QueueThread createQueueThread();
void enqueueThread(QueueThread * queue, VietateNoua vietateNoua);
VietateNoua dequeueThread(QueueThread * queue);
void * plaseazaVietateNoua(void *argv);
void creazaVietateaNoua(int x, int y, TipVietate tipVietate, Matrice * matricePtr);
void actioneazaPlanta(int x, int y, Matrice * matricePtr, bool pauseIt, bool follow);
void actioneazaIerbivor(int x, int y, Matrice *matrice_ptr,  bool pauseIt, bool follow) ;
void actioneazaCarnivor(int x, int y, Matrice *matrice_ptr, bool pauseIt, bool follow) ;
void justWakeUpTheKids(int sig);
void sigint_handler(int sig);

int main() {

    srand(time(NULL));

    //doar ca sa trezim copii
    if (signal(SIGUSR1, justWakeUpTheKids) == SIG_ERR) {
        perror("Error setting signal handler");
        return 1;
    }


    // Generarea unei chei unice pentru zona de memorie partajată
    key_t key = ftok("DDD_sem1_2024_2025", 65); // Asigură-te că myfile există

    // Crearea zonei de memorie partajată
    shm_id = shmget(key, sizeof(Matrice), IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget");
        _exit(1);
    }

    // Atașarea zonei de memorie partajată
    matrice_ptr = shmat(shm_id, NULL, 0);
    if (matrice_ptr == (Matrice *) -1) {
        perror("shmat");
        _exit(1);
    }
    // Inițializarea mutexurilor din matrice
    //Ne asiguram ca toti copii apartin aceluiasi grup de procese
    groupId = fork();
    
    if (!groupId) {
        //Cum am ales 0, 0 pgid va fi pid-ul acestuia
       setpgid(0,0);
       while(1) {
         pause();
       }
       _exit(0);
    } 
    sem_unlink(SEM_CREEAZA_VIETATE);
    semaforProducator = sem_open(SEM_CREEAZA_VIETATE, O_CREAT, 0666, MarimeQueue);

    if (semaforProducator == SEM_FAILED) {
        perror("Sem_open SEM_CREAZA_VIETATE deschidere initiala1");
        _exit(1);
    }

    sem_unlink(SEM_PLASEAZA_VIETATE);
    semaforConsumator = sem_open(SEM_PLASEAZA_VIETATE, O_CREAT, 0666, 0);

    if (semaforConsumator == SEM_FAILED) {
        perror("Sem_open SEM_PLASEAZA_VIETATE deschidere initiala2");
        _exit(1);
    }
    
    matrice_ptr->queueThread = createQueueThread();

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&matrice_ptr->queueThread.lock, &attr);
    
    int w = 0;int w1 = 0;int w2 = 0;
    for (int i = 0; i < MarimeMatrice; i++) {
        for (int j = 0; j < MarimeMatrice; j ++) {
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&matrice_ptr->m[i][j].lock, &attr);
            pid_t parinte;
            int z = rand();
            
            if (z % 20 == 3) {
                switch (rand() % 3) {
                case 0:
                    parinte = fork();
                    if(!parinte){
                    setpgid(0, groupId);
                    matrice_ptr->m[i][j].vietate.planta = creazaPlanta();
                    matrice_ptr->m[i][j].tipVietate = P; 
                    if(w1 == 0)
                    actioneazaPlanta(i,j,matrice_ptr,  true, true);
                    else actioneazaPlanta(i,j,matrice_ptr,  true, false);
                    _exit(0);
                    } 
                    w1 ++;
                    break;
                case 1:
                    parinte = fork();
                    if(!parinte){
                    setpgid(0, groupId);
                    matrice_ptr->m[i][j].vietate.ierbivor = creazaIerbivor(rand() % 2);
                    matrice_ptr->m[i][j].vietate.ierbivor = creazaIerbivor(rand() % 2);
                    matrice_ptr->m[i][j].tipVietate = I; 
                    if (w == 0)
                    actioneazaIerbivor(i,j,matrice_ptr, true, true);
                    else actioneazaIerbivor(i,j,matrice_ptr, true, false);
                    
                    _exit(0);
                    } 
                    w += 1;
                    break;
                case 2:
                    parinte = fork();
                    if(!parinte){
                    setpgid(0, groupId);
                    matrice_ptr->m[i][j].vietate.carnivor = creazaCarnivor(rand() % 2);
                    matrice_ptr->m[i][j].tipVietate = C; 
                    if (w2 == 0)
                    actioneazaCarnivor(i,j,matrice_ptr,  true, true);
                    else actioneazaCarnivor(i,j,matrice_ptr, true, false);
                    _exit(0);
                    }
                    w2 += 1;
                    break;
                default:
                    perror("Problema la alocarea matricei");
                    break;
                }
                
            } else {
                matrice_ptr->m[i][j].tipVietate = LIBER; // Inițializare opțională
            }
        }
        
    }

    pthread_t thread;  // Crem un nou thread ce va asigura initilizarea corecta a noile procese adica vietati
    int arg = 42;      // Argument to pass to the thread function
   
    // Create a new thread
    int result = pthread_create(&thread, NULL, plaseazaVietateNoua, (void*)&arg);
    if (result != 0) {
        perror("Thread creation failed");
        return 1;
    }

    if (signal(SIGINT, sigint_handler) == SIG_ERR) {
    perror("Error setting signal handler");
    return 1;
    }

    // kill(-groupId, SIGUSR1);
    // while (1) {
    //     /* code */
    // }
    
    
    initscr();
    cbreak(); // Disable line buffering
    noecho(); // Don't echo pressed keys
    keypad(stdscr, TRUE); // Enable function keys and arrow keys
    nodelay(stdscr, TRUE);

    start_color();

    init_pair(1, COLOR_GREEN, COLOR_BLACK); 
    init_pair(2, COLOR_BLUE, COLOR_BLACK);
    init_pair(3, COLOR_RED, COLOR_BLACK);

    int max_y, max_x;
    getmaxyx(stdscr, max_y, max_x);
    int start_y = (max_y - MarimeMatrice - 2) / 2; // Center the box vertically
    int start_x = (max_x - MarimeMatrice - 2) / 2; // Center the box horizontally
    matrix_win = newwin(MarimeMatrice + 2 , MarimeMatrice  + 2, 1, 1);
    box(matrix_win, 0, 0);
   
    int ch;
    

    // //trezim copiii
    kill(-groupId, SIGUSR1);
    // napms(5000);
    int z = 0;
    int nrPlante = 0, nrIerbivori = 0, nrCarnivori = 0;
    while (1) {
        ch = getch(); 
        // Clear the previous matrix
        werase(matrix_win);
        box(matrix_win, 0, 0); // Redraw the box

        // Populate the matrix with random values
        nrPlante = nrIerbivori = nrCarnivori = 0;
        for (int i = 0; i < MarimeMatrice; i++) {
            for (int j = 0; j < MarimeMatrice; j++) {
                // printf("_exit1\n");
                pthread_mutex_lock(&(matrice_ptr->m[i][j].lock));

                if(matrice_ptr->m[i][j].tipVietate != LIBER){
                    switch (matrice_ptr->m[i][j].tipVietate) {
                    case P:
                        wattron(matrix_win, COLOR_PAIR(1)); 
                        mvwprintw(matrix_win, i + 1, j + 1, "%c", 'P');
                        wattroff(matrix_win, COLOR_PAIR(1));
                        break;
                    case I:
                        wattron(matrix_win, COLOR_PAIR(2)); 
                        if (matrice_ptr->m[i][j].vietate.ierbivor.sex == M)
                        mvwprintw(matrix_win, i + 1, j + 1, "%c", 'I');
                        // printf("M");
                        else mvwprintw(matrix_win, i + 1, j + 1, "%c", 'i');
                        // printf("m");
                        wattroff(matrix_win, COLOR_PAIR(2));
                        nrIerbivori ++;
                        break;
                    case C:
                        wattron(matrix_win, COLOR_PAIR(3)); 
                        if (matrice_ptr->m[i][j].vietate.carnivor.sex == M)
                        mvwprintw(matrix_win, i + 1, j + 1, "%c", 'C');
                        //printf("C");
                        else mvwprintw(matrix_win, i + 1, j + 1, "%c", 'c');
                        //printf("c");
                        wattroff(matrix_win, COLOR_PAIR(3));
                        nrCarnivori ++;
                        break;
                        break;
                    default:
                        break;
                    }
                } else mvwprintw(matrix_win, i + 1, j + 1, " ");
                
                pthread_mutex_unlock(&(matrice_ptr->m[i][j].lock));
                
            }
            
        }
        //Iesim daca sunt numai plante
        if (!nrCarnivori && !nrIerbivori) {
            break;
        } 
        //Refresh the window to show the updated matrix
        
        wrefresh(matrix_win);

        if (ch == 'q' || ch == 'Q') {
            break; // _exit the loop if 'q' is pressed
        } else napms(100);

    }

    delwin(matrix_win);
    endwin();
    // Curățarea
    pthread_mutex_lock(&threadMutex);
    incaPotRula = false;
    pthread_mutex_unlock(&threadMutex);

    pthread_join(thread, NULL);
    //terminam toate procesele
    kill(-groupId, SIGTERM);

    pthread_mutex_destroy(&threadMutex);
    for (int i = 0; i < MarimeMatrice; i++) {
        for (int j = 0; j < MarimeMatrice; j++) {
            pthread_mutex_destroy(&matrice_ptr->m[i][j].lock);
        }
    }
    pthread_mutex_destroy(&matrice_ptr->queueThread.lock);
    sem_close(semaforProducator);
    sem_close(semaforConsumator);

    // Dezatașare și ștergere
    shmdt(matrice_ptr);
    shmctl(shm_id, IPC_RMID, NULL); // Șterge zona de memorie partajată

    return 0;

}

Queue* createQueue(int size) {
    Queue* q = (Queue*)malloc(sizeof(Queue));
    q->data = (QueueNode*)malloc(sizeof(QueueNode) * size);
    q->front = q->rear = -1;
    return q;
}

void enqueue(Queue* q, int x, int y, int dist, QueueNode* prev) {
    q->rear++;
    q->data[q->rear].point.x = x;
    q->data[q->rear].point.y = y;

    q->data[q->rear].dist = dist;
    q->data[q->rear].prev = prev;
    if (q->front == -1) {
        q->front = 0;
    }
    
}

QueueNode* dequeue(Queue* q) {
    if (q->front == -1) {
        // If the queue is empty, return NULL or handle it in some other way.
        return NULL;  // Return NULL to indicate the queue is empty
    }

    QueueNode* node = &q->data[q->front];  // Get the pointer to the front node
    
    if (q ->front > q->rear) {
        return NULL;
    }

    q->front += 1;

    return node;  // Return the pointer to the dequeued node
}

bool isEmpty(Queue* q) {
    return q->front == -1;
}

// BFS function to find the nearest 'P' to the given 'I' in the matrix
Point cautaPlantaIn10x10(int startX, int startY, Matrice * matricePtr) {
    // Initialize the visited matrix
    bool visited[MarimeMatrice][MarimeMatrice] = { false };
    Queue* q = createQueue(MarimeMatrice * MarimeMatrice);  // Max size for 100x100 grid
    enqueue(q, startX, startY, 0, NULL);
    visited[startX][startY] = true;
    
    // BFS loop
    while (!isEmpty(q)) {
       
        QueueNode * node = (dequeue(q));
        if(node == NULL) break;
        
        int x = node->point.x;
        int y = node->point.y;
        int dist = node->dist;
     
        // If we found a 'P', return its position
        pthread_mutex_lock(&(matricePtr->m[x][y].lock));
        TipVietate tipVietate = matricePtr->m[x][y].tipVietate;
        pthread_mutex_unlock(&(matricePtr->m[x][y].lock));

        if (tipVietate == P) {
            int i = 0;
            
            while (node->prev!=NULL) {   
                if(node->prev->point.x == startX && node->prev->point.y == startY) {
                    break;
                }
                node =  node->prev;
            }
          
            free(q);
            
            return (Point){ node->point.x, node->point.y };
        }

        // Explore all 4 directions
        for (int i = 0; i < 4; i++) {
            int nx = x + dirX[i];
            int ny = y + dirY[i];

            // Check if the new position is within bounds and not visited
            if (nx >= 0 && nx < MarimeMatrice && ny >= 0 && ny < MarimeMatrice && !visited[nx][ny]) {
                // Restrict the BFS to a 10x10 region around the start
                if (nx >= startX - 5 && nx <= startX + 5 && ny >= startY - 5 && ny <= startY + 5) {
                    pthread_mutex_lock(&(matricePtr->m[nx][ny].lock));
                    TipVietate tipVecin = matricePtr->m[nx][ny].tipVietate;
                    pthread_mutex_unlock(&(matricePtr->m[nx][ny].lock));
                    if(tipVecin != I && tipVecin != C) {
                    visited[nx][ny] = true;
                    if(node)
                    enqueue(q, nx, ny, dist + 1, node);
                    }
                }
            }
        }
    }
   
    free(q);
   
    // If no 'P' is found, return a special value (e.g., -1, -1)
    return (Point){ -1, -1 };
}

Point cautaIerbivorIn10x10(int startX, int startY, bool cautaDoarFemele, Matrice * matricePtr) {
    // Initialize the visited matrix
    bool visited[MarimeMatrice][MarimeMatrice] = { false };
    Queue* q = createQueue(MarimeMatrice * MarimeMatrice);  // Max size for 100x100 grid
    enqueue(q, startX, startY, 0, NULL);
    visited[startX][startY] = true;
    
    // BFS loop
    while (!isEmpty(q)) {
       
        QueueNode * node = (dequeue(q));
        if(node == NULL) break;
        //if(node->prev != NULL)
        //printf("bunicuta p:%p %d %d\n", node->prev, node->prev->point.x, node->prev->point.y);
        int x = node->point.x;
        int y = node->point.y;
        int dist = node->dist;
        // printf("|%d %d|", node.point.x, node.point.y, node);
        // If we found a 'P', return its position
        bool  amGasit = false;
        pthread_mutex_lock(&(matricePtr->m[x][y].lock));
        if (matricePtr->m[x][y].tipVietate == I) {
            if(cautaDoarFemele) {
                if (matricePtr->m[x][y].vietate.ierbivor.sex  == F) {
                amGasit = true;
                }
            } else amGasit = true;     
        }
        pthread_mutex_unlock(&(matricePtr->m[x][y].lock));

        if (amGasit) {
            int i = 0;
            
            while (node->prev!=NULL)
            {   
                if(node->prev->point.x == startX && node->prev->point.y == startY) {
                    //printf("!(%d %d)!", node->point.x, node->point.y);
                    break;
                }
                node =  node->prev;
            }
            free(q);
            return (Point){ node->point.x, node->point.y };
        }

        // Explore all 4 directions
        for (int i = 0; i < 4; i++) {
            int nx = x + dirX[i];
            int ny = y + dirY[i];

            // Check if the new position is within bounds and not visited
            if (nx >= 0 && nx < MarimeMatrice && ny >= 0 && ny < MarimeMatrice && !visited[nx][ny]) {
                // Restrict the BFS to a 10x10 region around the start
                bool nuEsteBarbatIerbivor = true;
                if (nx >= startX - 5 && nx <= startX + 5 && ny >= startY - 5 && ny <= startY + 5) {
                    pthread_mutex_lock(&(matricePtr->m[nx][ny].lock));
                    TipVietate tipVecin = matricePtr->m[nx][ny].tipVietate;
                    if (tipVecin == I) {
                        if (matricePtr->m[nx][ny].vietate.ierbivor.sex == M) nuEsteBarbatIerbivor = false;
                    }
                    pthread_mutex_unlock(&(matricePtr->m[nx][ny].lock));
                    if(tipVecin != P && tipVecin != C && nuEsteBarbatIerbivor) {
                    visited[nx][ny] = true;
                    if(node)
                    enqueue(q, nx, ny, dist + 1, node);
                    }
                }
            }
        }
    }
 
    free(q);

    // If no 'P' is found, return a special value (e.g., -1, -1)
    return (Point){ -1, -1 };
}

Point cautaCarnivorFIn10x10(int startX, int startY, Matrice * matricePtr) {
    // Initialize the visited matrix
    bool visited[MarimeMatrice][MarimeMatrice] = { false };
    Queue* q = createQueue(MarimeMatrice * MarimeMatrice);  // Max size for 100x100 grid
    enqueue(q, startX, startY, 0, NULL);
    visited[startX][startY] = true;
    
    // BFS loop
    while (!isEmpty(q)) {
       
        QueueNode * node = (dequeue(q));
        if(node == NULL) break;
        //printf("bunicuta p:%p %d %d\n", node->prev, node->prev->point.x, node->prev->point.y);
        int x = node->point.x;
        int y = node->point.y;
        int dist = node->dist;
        // If we found a 'P', return its position
        bool  amGasit = false;
        pthread_mutex_lock(&(matricePtr->m[x][y].lock));
        if (matricePtr->m[x][y].tipVietate == C) {
            if (matricePtr->m[x][y].vietate.carnivor.sex  == F) {
                amGasit = true;
            }
        }
        pthread_mutex_unlock(&(matricePtr->m[x][y].lock));

        if (amGasit) {
            int i = 0;
            
            while (node->prev!=NULL)
            {   
                if(node->prev->point.x == startX && node->prev->point.y == startY) {
                    //printf("!(%d %d)!", node->point.x, node->point.y);
                    break;
                }
                node =  node->prev;
            }
            free(q);
            return (Point){ node->point.x, node->point.y };
        }

        // Explore all 4 directions
        for (int i = 0; i < 4; i++) {
            int nx = x + dirX[i];
            int ny = y + dirY[i];

            // Check if the new position is within bounds and not visited
            if (nx >= 0 && nx < MarimeMatrice && ny >= 0 && ny < MarimeMatrice && !visited[nx][ny]) {
                // Restrict the BFS to a 10x10 region around the start
                bool nuEsteBarbatCarnivor = true;
                if (nx >= startX - 5 && nx <= startX + 5 && ny >= startY - 5 && ny <= startY + 5) {
                    pthread_mutex_lock(&(matricePtr->m[nx][ny].lock));
                    TipVietate tipVecin = matricePtr->m[nx][ny].tipVietate;
                    if (tipVecin == C) {
                        if (matricePtr->m[nx][ny].vietate.carnivor.sex == M)
                            nuEsteBarbatCarnivor = false;
                    }
                    pthread_mutex_unlock(&(matricePtr->m[nx][ny].lock));
                    if(tipVecin != P && tipVecin != I && nuEsteBarbatCarnivor) {
                    visited[nx][ny] = true;
                    if(node)
                    enqueue(q, nx, ny, dist + 1, node);
                    }
                }
            }
        }
    }
 
    free(q);

    return (Point){ -1, -1 };
}

////////////////////////////////
QueueThread createQueueThread() {
    QueueThread queue;
    queue.position = 0;
    queue.nextAvailablePositon = 0;
    queue.size = MarimeQueue;
    queue.isEmpty = true;
    return queue;
}

void enqueueThread(QueueThread * queue, VietateNoua vietateNoua) {
    if (!queue) {
        perror("Pass a valid thread queue");
        _exit(1);
    }
    pthread_mutex_lock(&queue->lock);
    queue->isEmpty = false;
    queue->data[queue->nextAvailablePositon] = vietateNoua;
    if (queue->nextAvailablePositon + 1 < queue->size) {
        queue->nextAvailablePositon += 1;
    } else queue->nextAvailablePositon = 0;
    if (queue->nextAvailablePositon == queue->position) {
        if (queue->position + 1 < queue->size) {
            queue->position += 1;
        } else  queue->position = 0;
    }

    //printf("En:(%d %d) %d %d %d size%d\n",vietateNoua.point.x, vietateNoua.point.y, queue->nextAvailablePositon, queue->position, queue->isEmpty, queue->size);
    
    pthread_mutex_unlock(&queue->lock);
     
}

VietateNoua dequeueThread(QueueThread * queue) {
    if (!queue) {
        perror("Pass a valid thread queue");
        _exit(1);
    }
    pthread_mutex_lock(&queue->lock);
    if (queue->isEmpty) {
        return (VietateNoua){(Point){-1, -1}, LIBER};
    }
    //printf("%d %d\n", queue->position, queue->data[queue->position].point.x);
    VietateNoua vietateNoua = queue->data[queue->position];
    if (queue->position + 1 < queue->size) {
        queue->position += 1;
    } else queue->position = 0;
    if (queue->nextAvailablePositon == queue->position) {
        queue->isEmpty = true;
    }
    //printf("Des:(%d %d) %d %d %d size%d\n",vietateNoua.point.x, vietateNoua.point.y, queue->nextAvailablePositon, queue->position, queue->isEmpty, queue->size);
    pthread_mutex_unlock(&queue->lock);
    return vietateNoua;
}

void * plaseazaVietateNoua(void *argv) {
    FILE * f = fopen("output.txt","w+");
    semaforProducator = sem_open(SEM_CREEAZA_VIETATE,0);
    if (semaforProducator == SEM_FAILED){
        perror("mata");
        _exit(1);
    }
    semaforConsumator = sem_open(SEM_PLASEAZA_VIETATE, 0);
     if (semaforConsumator == SEM_FAILED){
        perror("mata");
        _exit(1);
    }
    while (1) {
        // Lock the mutex to safely check the flag
        pthread_mutex_lock(&threadMutex);
        if (!incaPotRula) {
            pthread_mutex_unlock(&threadMutex);
            printf("Thread is terminating.\n");
            break;
        }
        pthread_mutex_unlock(&threadMutex);
        pid_t pid;
        if (matrice_ptr == NULL) {for(int i = 0; i < 100; i++) perror("gello"); _exit(1);}
        sem_wait(semaforConsumator);
            int i1, i2;sem_getvalue(semaforConsumator,&i1);sem_getvalue(semaforProducator,&i2);
            fprintf(f,"SCHIWP %d %d\n", i1,i2);
            VietateNoua vietateNoua;
            vietateNoua.point.x = -1; vietateNoua.point.y = -1;
            if (matrice_ptr != NULL) {
                vietateNoua = dequeueThread(&matrice_ptr->queueThread);
                //printf("oho:%d %d %d \n", vietateNoua.point.x, vietateNoua.point.y, vietateNoua.tipVietate);
            } //else printf("oho\n");
            
            if (vietateNoua.point.x != -1 && vietateNoua.point.y != -1) {
                switch (vietateNoua.tipVietate) {
                    case P:
                        pid = fork();
                        if (!pid) {
                            actioneazaPlanta(vietateNoua.point.x, vietateNoua.point.y, matrice_ptr, false, false);
                            _exit(0);
                        } else {fprintf(f,"hey %d\n", pid);fflush(f);}
                        break;
                    case I:
                        pid = fork();
                        if (!pid) {
                            actioneazaIerbivor(vietateNoua.point.x, vietateNoua.point.y, matrice_ptr, false, false);
                            _exit(0);
                        }else {fprintf(f,"hey %d\n", pid);fflush(f);}
                        break;
                    case C:
                        pid = fork();
                        if (!pid) {
                            actioneazaCarnivor(vietateNoua.point.x, vietateNoua.point.y, matrice_ptr, false, false);
                            _exit(0);
                        }else {fprintf(f,"hey %d\n", pid);fflush(f);}
                        break;
                    default:
                        break;
                }
            }
            
        sem_post(semaforProducator);
    }
    printf("f\n");
    fclose(f);
    printf("f\n");
    return NULL;
}


void actioneazaPlanta(int x, int y, Matrice * matricePtr, bool pauseIt, bool follow) {
    pthread_mutex_lock(&(matricePtr->m[x][y].lock));
    if (matricePtr->m[x][y].tipVietate != P) { perror("Eroare de coordonare"); _exit(1);}
    Planta self =  matricePtr->m[x][y].vietate.planta;
    pthread_mutex_unlock(&(matricePtr->m[x][y].lock));
    if (pauseIt) pause();
    int fecundare = 0;
    FILE * file = NULL;
    if (follow) {
        file  = fopen("outputPlanta.txt", "w+");
        if (file == NULL) {
            perror("Error opening file");
            _exit(1);
        }
        
        fprintf(file, "Hello From Nigeria %d %d %d!!\n",getpid(), x, y);
        fflush(file);
    }
    int o = 0;
    while (1) {
        int E = self.E;
        self.E -= 1;
        fecundare ++;
        if (self.E <= 0) {
            pthread_mutex_lock(&(matricePtr->m[x][y].lock));
            matricePtr->m[x][y].tipVietate = LIBER;
            pthread_mutex_unlock(&(matricePtr->m[x][y].lock));
            if (file != NULL){fprintf(file, "am murit %d %d\n", x, y); fflush(file);fclose(file);}
            break;
        }
        if (fecundare >= 30) {
            
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < MarimeMatrice && ny >= 0 && ny < MarimeMatrice ) {
                    // Restrict the BFS to a 10x10 region around the start
                    bool exit = false;
                    pthread_mutex_lock(&(matricePtr->m[nx][ny].lock));
                    if (matricePtr->m[nx][ny].tipVietate == LIBER) {
                        fecundare = 0;
                        matricePtr->m[nx][ny].tipVietate = P;
                        matricePtr->m[nx][ny].vietate.planta = creazaPlanta();
                        VietateNoua vietateNoua = {nx, ny, P};
                        if (file != NULL){fprintf(file, "am fecundat %d %d\n", vietateNoua.point.x, y);}; 
                        sem_wait(semaforProducator);
                            if (file != NULL){fprintf(file, "am intrat %d %d\n", x, y); fflush(file);}
                            enqueueThread(&matricePtr->queueThread,vietateNoua);
                            //printf("am iesit\n");
                        sem_post(semaforConsumator);
                        //else printf("e cu succes\n");
                        if (file != NULL){fprintf(file, "am iesit din fecundatie %d %d\n", x, y); fflush(file);}
                        exit = true;
                    }
                    pthread_mutex_unlock(&(matricePtr->m[nx][ny].lock));
                    if (exit) break;
                }
            } 
            //if (file != NULL){fprintf(file, "am iesit %d %d\n", x, y); fflush(file);fclose(file);}
            
        } 

        
        pthread_mutex_lock(&(matricePtr->m[x][y].lock));
        if(matricePtr->m[x][y].tipVietate != P) {perror("Eroare de coordonare2"); _exit(1);}
        int diferentaE = E - matricePtr->m[x][y].vietate.planta.E;
        if (file != NULL) { fprintf(file, "energie %d\n", self.E); fflush(file);}
        self.E -= diferentaE;
        matricePtr->m[x][y].vietate.planta.E = self.E;
        pthread_mutex_unlock(&(matricePtr->m[x][y].lock));
        
        sleep(1);
    }
    
}

void actioneazaIerbivor(int x, int y, Matrice *matrice_ptr,  bool pauseIt, bool follow) {
    pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
    if(matrice_ptr->m[x][y].tipVietate != I) perror("Eroare de coordonare Ierbivor");
    Ierbivor self =  matrice_ptr->m[x][y].vietate.ierbivor;
    pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
    if (pauseIt) pause();
    FILE * file = NULL;
    if (follow) {
        file  = fopen("outputIerbivor.txt", "w+");
        if (file == NULL) {
            perror("Error opening file");
            _exit(1);
        }
        
        fprintf(file, "Hello From Nigeria %d!!\n",getpid());
        fflush(file);
    }
    int o = 0;
    switch (self.sex) {
    case M:
        // char  numeFisier[20] = "teste/m";
        // if (x / 10) numeFisier[7] = ((char) (x/10) )+ '0'; else  numeFisier[7] = '0'; 
        // if (x % 10) numeFisier[8] = ((char) (x%10) )+ '0'; else  numeFisier[8] = '0'; 
        // numeFisier[9] = '_';
        // if (y / 10) numeFisier[10] = ((char) (y/10) )+ '0'; else  numeFisier[7] = '0'; 
        // if (y % 10) numeFisier[11] = ((char) (y%10) )+ '0'; else  numeFisier[8] = '0'; 
        // numeFisier[12] = '.'; numeFisier[13] = 't'; numeFisier[14] = 'x'; numeFisier[15] = 't';
        // numeFisier[16] = '\0';
        // FILE * f1 = fopen( numeFisier, "w+");
        while (1) {
            o ++;
            int E = self.E;
            self.E -= 1;
            if(file != NULL) {
                fprintf(file, "%d noile coord  %d %d Energie %d\n",getpid(), x, y,self.E);
                fflush(file);
             } 
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
                if (file != NULL){fprintf(file, "am murit %d %d\n", x, y); fflush(file);fclose(file);}
                matrice_ptr->m[x][y].tipVietate = LIBER;
                pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
                break;
            }
            //Adiacenta
            
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];
                bool potiIntra = true;
                if (nx >= 0 && nx < MarimeMatrice && ny >= 0 && ny < MarimeMatrice ) {
                    
                    pthread_mutex_lock(&(matrice_ptr->m[nx][ny].lock));
                    if (matrice_ptr->m[nx][ny].tipVietate == P) {
                        self.E  += 2;
                        matrice_ptr->m[nx][ny].vietate.planta.E -= 1;
                    }
                    if (self.E > 15 && matrice_ptr->m[nx][ny].tipVietate == I) {
                        if (matrice_ptr->m[nx][ny].vietate.ierbivor.sex == F && matrice_ptr->m[nx][ny].vietate.ierbivor.gestatie == -1) {
                           self.E -= 5;
                           if (matrice_ptr->m[nx][ny].vietate.ierbivor.gestatie == -1)
                           matrice_ptr->m[nx][ny].vietate.ierbivor.gestatie = 20; 
                        }
                    }
                    pthread_mutex_unlock(&(matrice_ptr->m[nx][ny].lock));
                }
            }

            bool eDefaptMort = false;

            if (self.E < 10) {
                
                Point urmatoareaPozitieSpreP = cautaPlantaIn10x10(x, y, matrice_ptr);
                if(file != NULL){fprintf(file, "%d tura %dma indrept spre %d %d\n",getpid(), o, urmatoareaPozitieSpreP.x, urmatoareaPozitieSpreP.y);fflush(file);}

                if (urmatoareaPozitieSpreP.x != -1 && urmatoareaPozitieSpreP.y != -1) {

                    pthread_mutex_lock(&(matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].lock));
                    if (matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].tipVietate == LIBER ) {

                       pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
                        
                        if (matrice_ptr->m[x][y].vietate.ierbivor.E > 0) {
                            matrice_ptr->m[x][y].tipVietate = LIBER;
                            matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].vietate = matrice_ptr->m[x][y].vietate;
                        } else eDefaptMort = true;

                        pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));

                        if (!eDefaptMort) {
                            matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].tipVietate  = I;
                       
                            x = urmatoareaPozitieSpreP.x; y = urmatoareaPozitieSpreP.y;
                        }
                        
                        if(file != NULL){fprintf(file,"%d tura %d tip:%d\n",getpid(), o, matrice_ptr->m[x][y].tipVietate);fflush(file);}
                    }
                    
                    pthread_mutex_unlock(&(matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].lock));

                } 
            }

            if (self.E > 20) {
               
                Point urmatoareaPozitie = cautaIerbivorIn10x10(x, y, true, matrice_ptr);
                //if (f1 != NULL) {fprintf(f1, "%d %d \n", urmatoareaPozitie.x, urmatoareaPozitie.y); fflush(f1);}
                if (urmatoareaPozitie.x != -1 && urmatoareaPozitie.y != -1) {
                    bool eDefaptMort = false;

                    pthread_mutex_lock(&(matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].lock));

                    if (matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].tipVietate == LIBER) {

                        pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
                        
                        if (matrice_ptr->m[x][y].vietate.ierbivor.E > 0) {
                            matrice_ptr->m[x][y].tipVietate = LIBER;
                            matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].vietate = matrice_ptr->m[x][y].vietate;
                        } else eDefaptMort = true;

                        pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));

                        if (!eDefaptMort) {
                            matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].tipVietate  = I;
                       
                            x = urmatoareaPozitie.x; y = urmatoareaPozitie.y;
                        }
                        
                        if(file != NULL){fprintf(file,"%d tura %d tip:%d\n",getpid(), o, matrice_ptr->m[x][y].tipVietate);fflush(file);}
                    }
                    
                    pthread_mutex_unlock(&(matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].lock));

                }
            }

            pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
            if(matrice_ptr->m[x][y].tipVietate != I) {printf("%d\n", matrice_ptr->m[x][y].tipVietate); perror("Eroare de coordonare2 Ierbivor M"); _exit(1);}
            int diferentaE = E - matrice_ptr->m[x][y].vietate.ierbivor.E;
            self.E -= diferentaE;
            matrice_ptr->m[x][y].vietate.ierbivor.E = self.E;
            pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
            sleep(1);
        }
        break;
    case F:
        while (1) {
            o ++;
            int E = self.E;
            self.E -= 1;
            if(file != NULL) {
                fprintf(file, "%d noile coord  %d %d Energie %d\n",getpid(), x, y,self.E);
                fflush(file);
            } 
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
                if (file != NULL){fprintf(file, "am murit %d %d\n", x, y); fflush(file);fclose(file);}
                matrice_ptr->m[x][y].tipVietate = LIBER;
                pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
                break;
            }
            //Adiacenta
            bool aNascut = false;
            if (self.gestatie != -1 && self.gestatie != 0) self.gestatie --;
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < MarimeMatrice && ny >= 0 && ny < MarimeMatrice ) {
                    // Restrict the BFS to a 10x10 region around the start
                    bool exit = false;
                    pthread_mutex_lock(&(matrice_ptr->m[nx][ny].lock));
                    if (self.gestatie == 0 && matrice_ptr->m[nx][ny].tipVietate == LIBER) {
                        self.gestatie -= 1;
                        matrice_ptr->m[nx][ny].tipVietate = I;
                        time_t seconds;
                        time(&seconds);
                        matrice_ptr->m[nx][ny].vietate.ierbivor = creazaIerbivor(seconds % 2);
                        VietateNoua vietateNoua = {nx, ny, I};
                        if (file != NULL){fprintf(file, "am fecundat %d %d\n", vietateNoua.point.x, y);}; 
                        sem_wait(semaforProducator);
                            if (file != NULL){fprintf(file, "am intrat %d %d\n", x, y); fflush(file);}
                            enqueueThread(&matrice_ptr->queueThread,vietateNoua);
                        sem_post(semaforConsumator);
                        aNascut = true;
                        exit = true;
                    }
                    if (matrice_ptr->m[nx][ny].tipVietate == P) {
                        self.E  += 2;
                        matrice_ptr->m[nx][ny].vietate.planta.E -= 1;
                        exit = true;
                    }
                    
                    pthread_mutex_unlock(&(matrice_ptr->m[nx][ny].lock));
                    if (exit) break;
                }
            }

            if (self.E < 10) {
                
                Point urmatoareaPozitieSpreP = cautaPlantaIn10x10(x, y, matrice_ptr);
                if(file != NULL){fprintf(file, "%d tura %dma indrept spre %d %d\n",getpid(), o, urmatoareaPozitieSpreP.x, urmatoareaPozitieSpreP.y);fflush(file);}
            
                if (urmatoareaPozitieSpreP.x != -1 && urmatoareaPozitieSpreP.y != -1) {
                    bool eDefaptMort = false;
                    pthread_mutex_lock(&(matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].lock));
                    if (matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].tipVietate == LIBER ) {

                        pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
                        if (matrice_ptr->m[x][y].vietate.ierbivor.E > 0) {
                            matrice_ptr->m[x][y].tipVietate = LIBER;
                            matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].vietate = matrice_ptr->m[x][y].vietate;
                        } else eDefaptMort = true;
                        
                        pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));

                        if (!eDefaptMort) {
                            matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].tipVietate  = I;
                       
                            x = urmatoareaPozitieSpreP.x; y = urmatoareaPozitieSpreP.y;
                        }
                        if(file != NULL){fprintf(file,"%d tura %d tip:%d\n",getpid(), o, matrice_ptr->m[x][y].tipVietate);fflush(file);}
                    }
                    
                    pthread_mutex_unlock(&(matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].lock));

                } 
            }
            

            pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
            if(matrice_ptr->m[x][y].tipVietate != I) {printf("%d", matrice_ptr->m[x][y].tipVietate); perror("Eroare de coordonare2 Ierbivor F"); _exit(1);}
            int diferentaE = E - matrice_ptr->m[x][y].vietate.ierbivor.E;
            if(aNascut) matrice_ptr->m[x][y].vietate.ierbivor.gestatie = -1;
            else {
                if (self.gestatie == -1 && matrice_ptr->m[x][y].vietate.ierbivor.gestatie != -1) {
                self.gestatie = matrice_ptr->m[x][y].vietate.ierbivor.gestatie;
                } else {
                    matrice_ptr->m[x][y].vietate.ierbivor.gestatie = self.gestatie;
                }
            }
            self.E -= diferentaE;
            matrice_ptr->m[x][y].vietate.ierbivor.E = self.E;
            pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
            sleep(1);
        }
        break;
    default:
        break;
    }
    
}

void actioneazaCarnivor(int x, int y, Matrice *matrice_ptr, bool pauseIt, bool follow) {
    pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
    if(matrice_ptr->m[x][y].tipVietate != C) perror("Eroare de coordonare Carnivor");
    Carnivor self =  matrice_ptr->m[x][y].vietate.carnivor;
    pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
    if (pauseIt) pause();
    FILE * file = NULL;
    // char  numeFisier[20] = "teste/f";
    // if (x / 10) numeFisier[7] = ((char) (x/10) )+ '0'; else  numeFisier[7] = '0'; 
    // if (x % 10) numeFisier[8] = ((char) (x%10) )+ '0'; else  numeFisier[8] = '0'; 
    // numeFisier[9] = '_';
    // if (y / 10) numeFisier[10] = ((char) (y/10) )+ '0'; else  numeFisier[7] = '0'; 
    // if (y % 10) numeFisier[11] = ((char) (y%10) )+ '0'; else  numeFisier[8] = '0'; 
    // numeFisier[12] = '.'; numeFisier[13] = 't'; numeFisier[14] = 'x'; numeFisier[15] = 't';
    // numeFisier[16] = '\0';
    // FILE * f1 = fopen( numeFisier, "w+");
    if (follow) {
        file  = fopen("outputCarnivor.txt", "w+");
        if (file == NULL) {
            perror("Error opening file");
            _exit(1);
        }
        
        fprintf(file, "Hello From Nigeria %d!!\n",getpid());
        fflush(file);
    }
    int o = 0;
    switch (self.sex) {
    case M:
        while (1) {
            o ++;
            int E = self.E;
            self.E -= 1;
            if(file != NULL) {
                fprintf(file, "%d noile coord  %d %d Energie %d\n",getpid(), x, y,self.E);
                fflush(file);
             } 
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
                if (file != NULL){fprintf(file, "am murit %d %d\n", x, y); fflush(file);fclose(file);}
                matrice_ptr->m[x][y].tipVietate = LIBER;
                pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
                break;
            }
            bool maiCautaFemela = true;
            //Adiacenta
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < MarimeMatrice && ny >= 0 && ny < MarimeMatrice ) {
                    // Restrict the BFS to a 10x10 region around the start
                    pthread_mutex_lock(&(matrice_ptr->m[nx][ny].lock));
                    if (self.E > 15 && matrice_ptr->m[nx][ny].tipVietate == C) {
                        if (matrice_ptr->m[nx][ny].vietate.carnivor.sex == F ) { 
                           if (matrice_ptr->m[nx][ny].vietate.carnivor.gestatie == -1) {
                            self.E -= 5;
                            matrice_ptr->m[nx][ny].vietate.carnivor.gestatie = 20;
                           }
                        }
                    }
                    if (matrice_ptr->m[nx][ny].tipVietate == I && matrice_ptr->m[nx][ny].vietate.ierbivor.E > 0)  {
                        self.E  += matrice_ptr->m[nx][ny].vietate.ierbivor.E;
                        matrice_ptr->m[nx][ny].vietate.ierbivor.E = 0;
                    }
                    pthread_mutex_unlock(&(matrice_ptr->m[nx][ny].lock));
                }
            }

            if (self.E < 20) {
                
                Point urmatoareaPozitieSpreP = cautaIerbivorIn10x10(x, y, false, matrice_ptr);
                if(file != NULL){fprintf(file, "%d tura %dma indrept spre %d %d\n",getpid(), o, urmatoareaPozitieSpreP.x, urmatoareaPozitieSpreP.y);fflush(file);}

                if (urmatoareaPozitieSpreP.x != -1 && urmatoareaPozitieSpreP.y != -1) {

                    pthread_mutex_lock(&(matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].lock));

                    if (matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].tipVietate == LIBER ) {

                        pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));

                        matrice_ptr->m[x][y].tipVietate = LIBER;
                        matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].vietate = matrice_ptr->m[x][y].vietate;

                        pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));

                        matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].tipVietate  = C;
                       
                        x = urmatoareaPozitieSpreP.x; y = urmatoareaPozitieSpreP.y;
                        if(file != NULL){fprintf(file,"%d tura %d tip:%d\n",getpid(), o, matrice_ptr->m[x][y].tipVietate);fflush(file);}
                    }
                    
                    pthread_mutex_unlock(&(matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].lock));

                } 
            }

            if (self.E > 30) {
                //printf("mata\n");
                Point urmatoareaPozitie = cautaCarnivorFIn10x10(x, y, matrice_ptr);
                //if (f1 != NULL) {fprintf(f1, "%d %d \n", urmatoareaPozitie.x, urmatoareaPozitie.y); fflush(f1);}
                if (urmatoareaPozitie.x != -1 && urmatoareaPozitie.y != -1) {

                    pthread_mutex_lock(&(matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].lock));

                    if (matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].tipVietate == LIBER ) {

                        pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));

                        matrice_ptr->m[x][y].tipVietate = LIBER;
                        matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].vietate = matrice_ptr->m[x][y].vietate;

                        pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));

                        matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].tipVietate  = C;
                       
                        x = urmatoareaPozitie.x; y = urmatoareaPozitie.y;
                        if(file != NULL){fprintf(file,"%d tura %d tip:%d\n",getpid(), o, matrice_ptr->m[x][y].tipVietate);fflush(file);}
                    }
                    
                    pthread_mutex_unlock(&(matrice_ptr->m[urmatoareaPozitie.x][urmatoareaPozitie.y].lock));

                }
            }

            pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
            if(matrice_ptr->m[x][y].tipVietate != C) {if (file != NULL) fprintf(file,"%d", matrice_ptr->m[x][y].tipVietate); 
            perror("Eroare de coordonare2 Carnivor M"); _exit(1);}
            int diferentaE = E - matrice_ptr->m[x][y].vietate.carnivor.E;
            self.E -= diferentaE;
            matrice_ptr->m[x][y].vietate.carnivor.E = self.E;
            pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
            sleep(1);
        }
        break;
    case F:
        while (1) {
            o ++;
            int E = self.E;
            self.E -= 1;
            if(file != NULL) {
                fprintf(file, "%d noile coord  %d %d Energie %d\n",getpid(), x, y,self.E);
                if(self.gestatie <= 0 && self.gestatie != -1) fprintf(file, "Gestatia a carnivorei %d\n", self.gestatie);
                fflush(file);
            } 
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
                if (file != NULL){fprintf(file, "am murit %d %d\n", x, y); fflush(file);fclose(file);}
                matrice_ptr->m[x][y].tipVietate = LIBER;
                pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
                break;
            }
            //Adiacenta
            bool aNascut = false;
            if (self.gestatie != -1 && self.gestatie != 0) self.gestatie --;
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < MarimeMatrice && ny >= 0 && ny < MarimeMatrice ) {
                    // Restrict the BFS to a 10x10 region around the start
                    bool exit = false;
                    pthread_mutex_lock(&(matrice_ptr->m[nx][ny].lock));
                    if (self.gestatie == 0 && matrice_ptr->m[nx][ny].tipVietate == LIBER) {
                        self.gestatie -= 1;
                        matrice_ptr->m[nx][ny].tipVietate = C;
                        time_t seconds;
                        time(&seconds);
                        matrice_ptr->m[nx][ny].vietate.carnivor = creazaCarnivor((seconds + x + y)% 2);
                        VietateNoua vietateNoua = {nx, ny, C};
                        if (file != NULL){fprintf(file, "am fecundat %d %d\n", vietateNoua.point.x, y);}; 
                        sem_wait(semaforProducator);
                            if (file != NULL){fprintf(file, "am intrat %d %d\n", x, y); fflush(file);}
                            enqueueThread(&matrice_ptr->queueThread,vietateNoua);
                            //printf("am iesit\n");
                        sem_post(semaforConsumator);
                        aNascut = true;
                        exit = true;
                    }
                    if (matrice_ptr->m[nx][ny].tipVietate == I && matrice_ptr->m[nx][ny].vietate.ierbivor.E > 0)  {
                        self.E  += matrice_ptr->m[nx][ny].vietate.ierbivor.E;
                        matrice_ptr->m[nx][ny].vietate.ierbivor.E = 0;
                        exit = true;
                    }
                    
                    pthread_mutex_unlock(&(matrice_ptr->m[nx][ny].lock));
                    if (exit) break;
                }
            }

            if (self.E < 20) {
                
                Point urmatoareaPozitieSpreP = cautaIerbivorIn10x10(x, y, false, matrice_ptr);
                if(file != NULL){fprintf(file, "%d tura %dma indrept spre %d %d\n",getpid(), o, urmatoareaPozitieSpreP.x, urmatoareaPozitieSpreP.y);fflush(file);}

                if (urmatoareaPozitieSpreP.x != -1 && urmatoareaPozitieSpreP.y != -1) {

                    pthread_mutex_lock(&(matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].lock));
                    if (matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].tipVietate == LIBER ) {

                        pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));

                        matrice_ptr->m[x][y].tipVietate = LIBER;
                        matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].vietate = matrice_ptr->m[x][y].vietate;

                        pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));

                        matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].tipVietate  = C;
                       
                        x = urmatoareaPozitieSpreP.x; y = urmatoareaPozitieSpreP.y;
                        if(file != NULL){fprintf(file,"%d tura %d tip:%d\n",getpid(), o, matrice_ptr->m[x][y].tipVietate);fflush(file);}
                    }
                    
                    pthread_mutex_unlock(&(matrice_ptr->m[urmatoareaPozitieSpreP.x][urmatoareaPozitieSpreP.y].lock));

                } 
            }
            

            pthread_mutex_lock(&(matrice_ptr->m[x][y].lock));
            if(matrice_ptr->m[x][y].tipVietate != C) { perror("Eroare de coordonare2 Carnivor F");}
            int diferentaE = E - matrice_ptr->m[x][y].vietate.carnivor.E;
            if(aNascut) matrice_ptr->m[x][y].vietate.carnivor.gestatie = -1;
            else {
                if (self.gestatie == -1 && matrice_ptr->m[x][y].vietate.carnivor.gestatie != -1) {
                self.gestatie = matrice_ptr->m[x][y].vietate.carnivor.gestatie;
                } else {
                    matrice_ptr->m[x][y].vietate.carnivor.gestatie = self.gestatie;
                }
            }
            self.E -= diferentaE;
            matrice_ptr->m[x][y].vietate.carnivor.E = self.E;
            pthread_mutex_unlock(&(matrice_ptr->m[x][y].lock));
            sleep(1);
        }
        break;
    default:
        break;
    }
    //fclose(f1);
}

void justWakeUpTheKids(int sig) {}

void sigint_handler(int sig) {
    if(groupId)
    kill(-groupId, SIGTERM);
    if (matrice_ptr != NULL) {
        for (int i = 0; i < MarimeMatrice; i++) {
            for (int j = 0; j < MarimeMatrice; j++) {
                pthread_mutex_destroy(&matrice_ptr->m[i][j].lock);
            }
        }
        // Dezatașare și ștergere
        shmdt(matrice_ptr);
        shmctl(shm_id, IPC_RMID, NULL);
    }
    delwin(matrix_win);
    endwin();
    _exit(0);  
}
