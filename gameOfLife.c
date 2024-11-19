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
#define SEM_CREATE_BEING "/createBeing"
#define SEM_PLACE_BEING "/placeBeing"
#define SizeOfMatrix 100
#define SizeOfQueue SizeOfMatrix * SizeOfMatrix

typedef enum {
    M,
    F
}Sex;

typedef struct {
    int E;
}Plant;

Plant createPlant() {
    Plant plant;
    plant.E = 100;
    return plant;
}

typedef struct {
    Sex sex;
    int E;
    int gestation;
}Herbivore;


Herbivore createHerbivore(Sex sex) {
    Herbivore herbivore;
    herbivore.E = 40;
    herbivore.sex= sex;
    herbivore.gestation = -1;
    return herbivore;
}

typedef struct {
    Sex sex;
    unsigned int E;
    int gestation;
}Carnivore;

Carnivore createCarnivore(Sex sex) {
    Carnivore carnivore;
    carnivore.E = 60;
    carnivore.gestation = -1;
    carnivore.sex = sex;
    return carnivore;
}

typedef enum {
    FREE,
    P,
    I,
    C
}TypeOfBeing;

typedef union {
    Plant plant;
    Herbivore herbivore;
    Carnivore carnivore;
}Being;

typedef struct {
    pthread_mutex_t lock;
    Being being;
    TypeOfBeing typeOfBeing;
}BeingContainer;
///

typedef struct {
    int x, y;
} Point;

typedef struct {
    Point point;
    TypeOfBeing typeOfBeing;
}NewBeing;

typedef struct {
    bool isEmpty;
    int position, nextAvailablePositon, size;
    pthread_mutex_t lock;
    NewBeing data[SizeOfMatrix];
} QueueThread;

typedef struct {
    BeingContainer m[SizeOfMatrix][SizeOfMatrix];
    QueueThread queueThread;
}Matrix;

typedef struct QueueNode {
    Point point;
    int dist;
    struct QueueNode *prev;  // Pointer to the previous QueueNode
} QueueNode;

typedef struct {
    QueueNode *data;
    int front, rear;
} Queue;

Matrix *matrixPtr;

// Directions for 4-connected grid (up, down, left, right)
int dirX[] = { -1, 1, 0, 0 };
int dirY[] = { 0, 0, -1, 1 };

///////////////////////////
bool isStillRunning = true;
pthread_mutex_t threadMutex;
pthread_mutex_t ReadWriteMutex;
sem_t * producerSemaphore;
sem_t  * consumerSemaphore; 
pid_t groupId = 0; 
int shm_id;
WINDOW *matrixWin; 

Queue* createQueue(int size);
void enqueue(Queue* q, int x, int y, int dist, QueueNode* prev);
QueueNode* dequeue(Queue* q);
// BFS function to find the nearest 'P' to the given 'I' in the matrix
Point searchForPlantsIn10x10(int startX, int startY, Matrix * matrixPtr);
Point searchForHerbivoreIn10x10(int startX, int startY, bool onlySearchForFemales, Matrix * matrixPtr);
Point searchForCarnivoreFIn10x10(int startX, int startY, Matrix * matrixPtr);
QueueThread createQueueThread();
void enqueueThread(QueueThread * queue, NewBeing newBeing);
NewBeing dequeueThread(QueueThread * queue);
void * placeNewBeing(void *argv);
void createNewBeing(int x, int y, TypeOfBeing typeOfBeing, Matrix * matrixPtr);
void plantBehavior(int x, int y, Matrix * matrixPtr, bool pauseIt);
void herbivoreBehavior(int x, int y, Matrix *matrixPtr,  bool pauseIt);
void carnivoreBehavior(int x, int y, Matrix *matrixPtr, bool pauseIt) ;
void justWakeUpTheKids(int sig);
void sigintHandler(int sig);

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
    shm_id = shmget(key, sizeof(Matrix), IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget");
        _exit(1);
    }

   
    matrixPtr = shmat(shm_id, NULL, 0);
    if (matrixPtr == (Matrix *) -1) {
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
    sem_unlink(SEM_CREATE_BEING);
    producerSemaphore = sem_open(SEM_CREATE_BEING, O_CREAT, 0666, SizeOfQueue);

    if (producerSemaphore == SEM_FAILED) {
        perror("Sem_open SEM_CREAZA_VIETATE deschidere initiala1");
        _exit(1);
    }

    sem_unlink(SEM_PLACE_BEING);
    consumerSemaphore = sem_open(SEM_PLACE_BEING, O_CREAT, 0666, 0);

    if (consumerSemaphore == SEM_FAILED) {
        perror("Sem_open SEM_PLACE_BEING deschidere initiala2");
        _exit(1);
    }
    
    matrixPtr->queueThread = createQueueThread();

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&matrixPtr->queueThread.lock, &attr);
    
    for (int i = 0; i < SizeOfMatrix; i++) {
        for (int j = 0; j < SizeOfMatrix; j ++) {
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&matrixPtr->m[i][j].lock, &attr);
            pid_t parinte;
            int z = rand();
            
            if (z % 20 == 3) {
                switch (rand() % 3) {
                case 0:
                    parinte = fork();
                    if(!parinte){
                    setpgid(0, groupId);
                    matrixPtr->m[i][j].being.plant = createPlant();
                    matrixPtr->m[i][j].typeOfBeing = P; 
                    plantBehavior(i,j,matrixPtr,  true);
                    _exit(0);
                    } 
                    break;
                case 1:
                    parinte = fork();
                    if(!parinte){
                    setpgid(0, groupId);
                    matrixPtr->m[i][j].being.herbivore = createHerbivore(rand() % 2);
                    matrixPtr->m[i][j].typeOfBeing = I; 
                    herbivoreBehavior(i,j,matrixPtr, true);
                    _exit(0);
                    } 
                    break;
                case 2:
                    parinte = fork();
                    if(!parinte){
                    setpgid(0, groupId);
                    matrixPtr->m[i][j].being.carnivore = createCarnivore(rand() % 2);
                    matrixPtr->m[i][j].typeOfBeing = C; 
                    carnivoreBehavior(i,j,matrixPtr,  true);
                    _exit(0);
                    }
                    break;
                default:
                    perror("Problema la alocarea matricei");
                    break;
                }
                
            } else {
                matrixPtr->m[i][j].typeOfBeing = FREE; // Inițializare opțională
            }
        }
        
    }

    pthread_t thread;  // Crem un nou thread ce va asigura initilizarea corecta a noile procese adica vietati

    // Create a new thread
    int result = pthread_create(&thread, NULL, placeNewBeing, NULL);
    if (result != 0) {
        perror("Thread creation failed");
        return 1;
    }

    if (signal(SIGINT, sigintHandler) == SIG_ERR) {
    perror("Error setting signal handler");
    return 1;
    }
    
    
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
    int start_y = (max_y - SizeOfMatrix - 2) / 2; // Center the box vertically
    int start_x = (max_x - SizeOfMatrix - 2) / 2; // Center the box horizontally
    matrixWin = newwin(SizeOfMatrix + 2 , SizeOfMatrix  + 2, 1, 1);
    box(matrixWin, 0, 0);
   
    int ch;
    

    // //trezim copiii
    kill(-groupId, SIGUSR1);
    // napms(5000);
    int z = 0;
    int numberHerbivores = 0, numberCarnivores = 0;
    while (1) {
        ch = getch(); 
        // Clear the previous matrix
        werase(matrixWin);
        box(matrixWin, 0, 0); // Redraw the box

        // Populate the matrix with random values
        numberHerbivores = numberCarnivores = 0;
        for (int i = 0; i < SizeOfMatrix; i++) {
            for (int j = 0; j < SizeOfMatrix; j++) {
                // printf("_exit1\n");
                pthread_mutex_lock(&(matrixPtr->m[i][j].lock));

                if(matrixPtr->m[i][j].typeOfBeing != FREE){
                    switch (matrixPtr->m[i][j].typeOfBeing) {
                    case P:
                        wattron(matrixWin, COLOR_PAIR(1)); 
                        mvwprintw(matrixWin, i + 1, j + 1, "%c", 'P');
                        wattroff(matrixWin, COLOR_PAIR(1));
                        break;
                    case I:
                        wattron(matrixWin, COLOR_PAIR(2)); 
                        if (matrixPtr->m[i][j].being.herbivore.sex == M)
                        mvwprintw(matrixWin, i + 1, j + 1, "%c", 'I');
                        // printf("M");
                        else mvwprintw(matrixWin, i + 1, j + 1, "%c", 'i');
                        // printf("m");
                        wattroff(matrixWin, COLOR_PAIR(2));
                        numberHerbivores ++;
                        break;
                    case C:
                        wattron(matrixWin, COLOR_PAIR(3)); 
                        if (matrixPtr->m[i][j].being.carnivore.sex == M)
                        mvwprintw(matrixWin, i + 1, j + 1, "%c", 'C');
                        else mvwprintw(matrixWin, i + 1, j + 1, "%c", 'c');
                        wattroff(matrixWin, COLOR_PAIR(3));
                        numberCarnivores ++;
                        break;
                        break;
                    default:
                        break;
                    }
                } else mvwprintw(matrixWin, i + 1, j + 1, " ");
                
                pthread_mutex_unlock(&(matrixPtr->m[i][j].lock));
                
            }
            
        }
        //We exit if there are only plants
        if (!numberCarnivores && !numberHerbivores) {
            break;
        } 
        //Refresh the window to show the updated matrix
        
        wrefresh(matrixWin);

        if (ch == 'q' || ch == 'Q') {
            break; // _exit the loop if 'q' is pressed
        } else napms(100);

    }

    delwin(matrixWin);
    endwin();
    // Clean Up
    pthread_mutex_lock(&threadMutex);
    isStillRunning = false;
    pthread_mutex_unlock(&threadMutex);

    pthread_join(thread, NULL);
    //kill all processes
    kill(-groupId, SIGTERM);

    pthread_mutex_destroy(&threadMutex);
    for (int i = 0; i < SizeOfMatrix; i++) {
        for (int j = 0; j < SizeOfMatrix; j++) {
            pthread_mutex_destroy(&matrixPtr->m[i][j].lock);
        }
    }
    pthread_mutex_destroy(&matrixPtr->queueThread.lock);
    sem_close(producerSemaphore);
    sem_close(consumerSemaphore);

    // Detach
    shmdt(matrixPtr);
    shmctl(shm_id, IPC_RMID, NULL); // Delete the shared memory

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
Point searchForPlantsIn10x10(int startX, int startY, Matrix * matrixPtr) {
    // Initialize the visited matrix
    bool visited[SizeOfMatrix][SizeOfMatrix] = { false };
    Queue* q = createQueue(SizeOfMatrix * SizeOfMatrix);  // Max size for 100x100 grid
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
        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
        TypeOfBeing typeOfBeing = matrixPtr->m[x][y].typeOfBeing;
        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

        if (typeOfBeing == P) {
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
            if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix && !visited[nx][ny]) {
                // Restrict the BFS to a 10x10 region around the start
                if (nx >= startX - 5 && nx <= startX + 5 && ny >= startY - 5 && ny <= startY + 5) {
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    TypeOfBeing typeNeighour = matrixPtr->m[nx][ny].typeOfBeing;
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if(typeNeighour != I && typeNeighour != C) {
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

Point searchForHerbivoreIn10x10(int startX, int startY, bool searchOnlyFemales, Matrix * matrixPtr) {
    // Initialize the visited matrix
    bool visited[SizeOfMatrix][SizeOfMatrix] = { false };
    Queue* q = createQueue(SizeOfMatrix * SizeOfMatrix);  // Max size for 100x100 grid
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
        bool  iFoundIt = false;
        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
        if (matrixPtr->m[x][y].typeOfBeing == I) {
            if(searchOnlyFemales) {
                if (matrixPtr->m[x][y].being.herbivore.sex  == F) {
                iFoundIt = true;
                }
            } else iFoundIt = true;     
        }
        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

        if (iFoundIt) {
            int i = 0;
            
            while (node->prev!=NULL)
            {   
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
            if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix && !visited[nx][ny]) {
                // Restrict the BFS to a 10x10 region around the start
                bool notMaleHerbivore = true;
                if (nx >= startX - 5 && nx <= startX + 5 && ny >= startY - 5 && ny <= startY + 5) {
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    TypeOfBeing typeNeighour = matrixPtr->m[nx][ny].typeOfBeing;
                    if (typeNeighour == I) {
                        if (matrixPtr->m[nx][ny].being.herbivore.sex == M) notMaleHerbivore = false;
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if(typeNeighour != P && typeNeighour != C && notMaleHerbivore) {
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

Point searchForCarnivoreFIn10x10(int startX, int startY, Matrix * matrixPtr) {
    // Initialize the visited matrix
    bool visited[SizeOfMatrix][SizeOfMatrix] = { false };
    Queue* q = createQueue(SizeOfMatrix * SizeOfMatrix);  // Max size for 100x100 grid
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
        bool  iFoundIt = false;
        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
        if (matrixPtr->m[x][y].typeOfBeing == C) {
            if (matrixPtr->m[x][y].being.carnivore.sex  == F) {
                iFoundIt = true;
            }
        }
        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

        if (iFoundIt) {
            int i = 0;
            
            while (node->prev!=NULL)
            {   
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
            if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix && !visited[nx][ny]) {
                // Restrict the BFS to a 10x10 region around the start
                bool notMaleCarnivore = true;
                if (nx >= startX - 5 && nx <= startX + 5 && ny >= startY - 5 && ny <= startY + 5) {
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    TypeOfBeing typeNeighour = matrixPtr->m[nx][ny].typeOfBeing;
                    if (typeNeighour == C) {
                        if (matrixPtr->m[nx][ny].being.carnivore.sex == M)
                            notMaleCarnivore = false;
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if(typeNeighour != P && typeNeighour != I && notMaleCarnivore) {
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
    queue.size = SizeOfQueue;
    queue.isEmpty = true;
    return queue;
}

void enqueueThread(QueueThread * queue, NewBeing newBeing) {
    if (!queue) {
        perror("Pass a valid thread queue");
        _exit(1);
    }
    pthread_mutex_lock(&queue->lock);
    queue->isEmpty = false;
    queue->data[queue->nextAvailablePositon] = newBeing;
    if (queue->nextAvailablePositon + 1 < queue->size) {
        queue->nextAvailablePositon += 1;
    } else queue->nextAvailablePositon = 0;
    if (queue->nextAvailablePositon == queue->position) {
        if (queue->position + 1 < queue->size) {
            queue->position += 1;
        } else  queue->position = 0;
    }

    pthread_mutex_unlock(&queue->lock);
     
}

NewBeing dequeueThread(QueueThread * queue) {

    if (!queue) {
        perror("Pass a valid thread queue");
        _exit(1);
    }

    pthread_mutex_lock(&queue->lock);
    if (queue->isEmpty) {
        return (NewBeing){(Point){-1, -1}, FREE};
    }

    NewBeing newBeing = queue->data[queue->position];
    if (queue->position + 1 < queue->size) {
        queue->position += 1;
    } else queue->position = 0;
    if (queue->nextAvailablePositon == queue->position) {
        queue->isEmpty = true;
    }

    pthread_mutex_unlock(&queue->lock);
    return newBeing;
}

void * placeNewBeing(void *argv) {

    producerSemaphore = sem_open(SEM_CREATE_BEING,0);
    if (producerSemaphore == SEM_FAILED){
        perror("couldn't open the producerSemaphore in the thread");
        _exit(1);
    }
    consumerSemaphore = sem_open(SEM_PLACE_BEING, 0);
     if (consumerSemaphore == SEM_FAILED){
        perror("mata");
        _exit(1);
    }
    while (1) {
        // Lock the mutex to safely check the flag
        pthread_mutex_lock(&threadMutex);
        if (!isStillRunning) {
            pthread_mutex_unlock(&threadMutex);
            break;
        }
        pthread_mutex_unlock(&threadMutex);
        pid_t pid;
        sem_wait(consumerSemaphore);
            NewBeing newBeing;
            newBeing.point.x = -1; newBeing.point.y = -1;
            if (matrixPtr != NULL) {
                newBeing = dequeueThread(&matrixPtr->queueThread);
            } else {perror(" Thread doesn't have access to the matrix!!");}
            
            if (newBeing.point.x != -1 && newBeing.point.y != -1) {
                switch (newBeing.typeOfBeing) {
                    case P:
                        pid = fork();
                        if (!pid) {
                            plantBehavior(newBeing.point.x, newBeing.point.y, matrixPtr, false);
                            _exit(0);
                        }
                        break;
                    case I:
                        pid = fork();
                        if (!pid) {
                            herbivoreBehavior(newBeing.point.x, newBeing.point.y, matrixPtr, false);
                            _exit(0);
                        }
                        break;
                    case C:
                        pid = fork();
                        if (!pid) {
                            carnivoreBehavior(newBeing.point.x, newBeing.point.y, matrixPtr, false);
                            _exit(0);
                        }
                        break;
                    default:
                        break;
                }
            }
            
        sem_post(producerSemaphore);
    }
    return NULL;
}


void plantBehavior(int x, int y, Matrix * matrixPtr, bool pauseIt) {
    pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
    if (matrixPtr->m[x][y].typeOfBeing != P) { perror("Synchronization error occured in Plant"); _exit(1);}
    Plant self =  matrixPtr->m[x][y].being.plant;
    pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
    if (pauseIt) pause();
    int fecundation = 0;
    while (1) {
        int E = self.E;
        self.E -= 1;
        fecundation ++;
        if (self.E <= 0) {
            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            matrixPtr->m[x][y].typeOfBeing = FREE;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            break;
        }
        if (fecundation >= 30) {
            
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    // Restrict the BFS to a 10x10 region around the start
                    bool exit = false;
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (matrixPtr->m[nx][ny].typeOfBeing == FREE) {
                        fecundation = 0;
                        matrixPtr->m[nx][ny].typeOfBeing = P;
                        matrixPtr->m[nx][ny].being.plant = createPlant();
                        NewBeing newBeing = {nx, ny, P};
                        sem_wait(producerSemaphore);
                            enqueueThread(&matrixPtr->queueThread,newBeing);
                        sem_post(consumerSemaphore);
                        exit = true;
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if (exit) break;
                }
            } 
    
        } 

        
        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
        if(matrixPtr->m[x][y].typeOfBeing != P) {perror("Synchronization error occured in Plant"); _exit(1);}
        int diferenceE = E - matrixPtr->m[x][y].being.plant.E;
        self.E -= diferenceE;
        matrixPtr->m[x][y].being.plant.E = self.E;
        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
        
        sleep(1);
    }
    
}

void herbivoreBehavior(int x, int y, Matrix *matrixPtr,  bool pauseIt) {
    pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
    if(matrixPtr->m[x][y].typeOfBeing != I) perror("Synchronization error occured in Ierbivor");
    Herbivore self =  matrixPtr->m[x][y].being.herbivore;
    pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
    if (pauseIt) pause();
    int o = 0;
    switch (self.sex) {
    case M:
        while (1) {
            o ++;
            int E = self.E;
            self.E -= 1;
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                matrixPtr->m[x][y].typeOfBeing = FREE;
                pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
                break;
            }
            //Adiacenta
            
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];
                bool potiIntra = true;
                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (matrixPtr->m[nx][ny].typeOfBeing == P) {
                        self.E  += 2;
                        matrixPtr->m[nx][ny].being.plant.E -= 1;
                    }
                    if (self.E > 15 && matrixPtr->m[nx][ny].typeOfBeing == I) {
                        if (matrixPtr->m[nx][ny].being.herbivore.sex == F && matrixPtr->m[nx][ny].being.herbivore.gestation == -1) {
                           self.E -= 5;
                           if (matrixPtr->m[nx][ny].being.herbivore.gestation == -1)
                           matrixPtr->m[nx][ny].being.herbivore.gestation = 20; 
                        }
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                }
            }

            bool isActuallyDeath = false;

            if (self.E < 10) {
                
                Point moveTo = searchForPlantsIn10x10(x, y, matrixPtr);

                if (moveTo.x != -1 && moveTo.y != -1) {

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));
                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                       pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                        
                        if (matrixPtr->m[x][y].being.herbivore.E > 0) {
                            matrixPtr->m[x][y].typeOfBeing = FREE;
                            matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;
                        } else isActuallyDeath = true;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        if (!isActuallyDeath) {
                            matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = I;
                       
                            x = moveTo.x; y = moveTo.y;
                        }

                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                } 
            }

            if (self.E > 20) {
               
                Point moveTo = searchForHerbivoreIn10x10(x, y, true, matrixPtr);
                //if (f1 != NULL) {fprintf(f1, "%d %d \n", moveTo.x, moveTo.y); fflush(f1);}
                if (moveTo.x != -1 && moveTo.y != -1) {
                    bool isActuallyDeath = false;

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                        
                        if (matrixPtr->m[x][y].being.herbivore.E > 0) {
                            matrixPtr->m[x][y].typeOfBeing = FREE;
                            matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;
                        } else isActuallyDeath = true;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        if (!isActuallyDeath) {
                            matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = I;
                       
                            x = moveTo.x; y = moveTo.y;
                        }
                        
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                }
            }

            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            if(matrixPtr->m[x][y].typeOfBeing != I) { perror("Synchronization error occured in2 herbivore M"); _exit(1);}
            int diferenceE = E - matrixPtr->m[x][y].being.herbivore.E;
            self.E -= diferenceE;
            matrixPtr->m[x][y].being.herbivore.E = self.E;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            sleep(1);
        }
        break;
    case F:
        while (1) {
            o ++;
            int E = self.E;
            self.E -= 1;
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                matrixPtr->m[x][y].typeOfBeing = FREE;
                pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
                break;
            }
            bool gaveBirth = false;
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    // Restrict the BFS to a 10x10 region around the start
                    bool exit = false;
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (self.gestation == 0 && matrixPtr->m[nx][ny].typeOfBeing == FREE) {
                        self.gestation -= 1;
                        matrixPtr->m[nx][ny].typeOfBeing = I;
                        time_t seconds;
                        time(&seconds);
                        matrixPtr->m[nx][ny].being.herbivore = createHerbivore(seconds % 2);
                        NewBeing newBeing = {nx, ny, I};
                        sem_wait(producerSemaphore);
                            enqueueThread(&matrixPtr->queueThread,newBeing);
                        sem_post(consumerSemaphore);
                        gaveBirth = true;
                        exit = true;
                    }
                    if (matrixPtr->m[nx][ny].typeOfBeing == P) {
                        self.E  += 2;
                        matrixPtr->m[nx][ny].being.plant.E -= 1;
                        exit = true;
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if (exit) break;
                }
            }

            if (self.E < 10) {
                
                Point moveTo = searchForPlantsIn10x10(x, y, matrixPtr);
            
                if (moveTo.x != -1 && moveTo.y != -1) {
                    bool isActuallyDeath = false;
                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));
                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                        if (matrixPtr->m[x][y].being.herbivore.E > 0) {
                            matrixPtr->m[x][y].typeOfBeing = FREE;
                            matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;
                        } else isActuallyDeath = true;
                        
                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        if (!isActuallyDeath) {
                            matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = I;
                       
                            x = moveTo.x; y = moveTo.y;
                        }
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                } 
            }
            

            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            if(matrixPtr->m[x][y].typeOfBeing != I) { perror("Synchronization error occured in herbivore F"); _exit(1);}
            int diferenceE = E - matrixPtr->m[x][y].being.herbivore.E;
            if(gaveBirth) matrixPtr->m[x][y].being.herbivore.gestation = -1;
            else {
                if (self.gestation == -1 && matrixPtr->m[x][y].being.herbivore.gestation != -1) {
                self.gestation = matrixPtr->m[x][y].being.herbivore.gestation;
                } else {
                    matrixPtr->m[x][y].being.herbivore.gestation = self.gestation;
                }
            }
            self.E -= diferenceE;
            matrixPtr->m[x][y].being.herbivore.E = self.E;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            sleep(1);
        }
        break;
    default:
        break;
    }
    
}

void carnivoreBehavior(int x, int y, Matrix *matrixPtr, bool pauseIt) {
    pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
    if(matrixPtr->m[x][y].typeOfBeing != C) perror("Synchronization error occured in - Carnivore");
    Carnivore self =  matrixPtr->m[x][y].being.carnivore;
    pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
    if (pauseIt) pause();
    switch (self.sex) {
    case M:
        while (1) {
            int E = self.E;
            self.E -= 1;
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                matrixPtr->m[x][y].typeOfBeing = FREE;
                pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
                break;
            }
            bool maiCautaFemela = true;
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    // Restrict the BFS to a 10x10 region around the start
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (self.E > 15 && matrixPtr->m[nx][ny].typeOfBeing == C) {
                        if (matrixPtr->m[nx][ny].being.carnivore.sex == F ) { 
                           if (matrixPtr->m[nx][ny].being.carnivore.gestation == -1) {
                            self.E -= 5;
                            matrixPtr->m[nx][ny].being.carnivore.gestation = 20;
                           }
                        }
                    }
                    if (matrixPtr->m[nx][ny].typeOfBeing == I && matrixPtr->m[nx][ny].being.herbivore.E > 0)  {
                        self.E  += matrixPtr->m[nx][ny].being.herbivore.E;
                        matrixPtr->m[nx][ny].being.herbivore.E = 0;
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                }
            }

            if (self.E < 20) {
                
                Point moveTo = searchForHerbivoreIn10x10(x, y, false, matrixPtr);

                if (moveTo.x != -1 && moveTo.y != -1) {

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[x][y].typeOfBeing = FREE;
                        matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = C;
                       
                        x = moveTo.x; y = moveTo.y;
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                } 
            }

            if (self.E > 30) {

                Point moveTo = searchForCarnivoreFIn10x10(x, y, matrixPtr);

                if (moveTo.x != -1 && moveTo.y != -1) {

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[x][y].typeOfBeing = FREE;
                        matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = C;
                       
                        x = moveTo.x; y = moveTo.y;
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                }
            }

            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            if(matrixPtr->m[x][y].typeOfBeing != C) {
            perror("Synchronization error occured in carnivore M"); 
            _exit(1);}
            int diferenceE = E - matrixPtr->m[x][y].being.carnivore.E;
            self.E -= diferenceE;
            matrixPtr->m[x][y].being.carnivore.E = self.E;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            sleep(1);
        }
        break;
    case F:
        while (1) {
            int E = self.E;
            self.E -= 1;
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                matrixPtr->m[x][y].typeOfBeing = FREE;
                pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
                break;
            }
            bool gaveBirth = false;
            if (self.gestation != -1 && self.gestation != 0) self.gestation --;
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    // Restrict the BFS to a 10x10 region around the start
                    bool exit = false;
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (self.gestation == 0 && matrixPtr->m[nx][ny].typeOfBeing == FREE) {
                        self.gestation -= 1;
                        matrixPtr->m[nx][ny].typeOfBeing = C;
                        time_t seconds;
                        time(&seconds);
                        matrixPtr->m[nx][ny].being.carnivore = createCarnivore((seconds + x + y)% 2);
                        NewBeing newBeing = {nx, ny, C};
                        sem_wait(producerSemaphore);
                            enqueueThread(&matrixPtr->queueThread,newBeing);
                        sem_post(consumerSemaphore);
                        gaveBirth = true;
                        exit = true;
                    }
                    if (matrixPtr->m[nx][ny].typeOfBeing == I && matrixPtr->m[nx][ny].being.herbivore.E > 0)  {
                        self.E  += matrixPtr->m[nx][ny].being.herbivore.E;
                        matrixPtr->m[nx][ny].being.herbivore.E = 0;
                        exit = true;
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if (exit) break;
                }
            }

            if (self.E < 20) {
                
                Point moveTo = searchForHerbivoreIn10x10(x, y, false, matrixPtr);

                if (moveTo.x != -1 && moveTo.y != -1) {

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));
                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[x][y].typeOfBeing = FREE;
                        matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = C;
                       
                        x = moveTo.x; y = moveTo.y;
                    }
                    
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
#define SEM_CREATE_BEING "/createBeing"
#define SEM_PLACE_BEING "/placeBeing"
#define SizeOfMatrix 100
#define SizeOfQueue SizeOfMatrix * SizeOfMatrix

typedef enum {
    M,
    F
}Sex;

typedef struct {
    int E;
}Plant;

Plant createPlant() {
    Plant plant;
    plant.E = 100;
    return plant;
}

typedef struct {
    Sex sex;
    int E;
    int gestation;
}Herbivore;


Herbivore createHerbivore(Sex sex) {
    Herbivore herbivore;
    herbivore.E = 40;
    herbivore.sex= sex;
    herbivore.gestation = -1;
    return herbivore;
}

typedef struct {
    Sex sex;
    unsigned int E;
    int gestation;
}Carnivore;

Carnivore createCarnivore(Sex sex) {
    Carnivore carnivore;
    carnivore.E = 60;
    carnivore.gestation = -1;
    carnivore.sex = sex;
    return carnivore;
}

typedef enum {
    FREE,
    P,
    I,
    C
}TypeOfBeing;

typedef union {
    Plant plant;
    Herbivore herbivore;
    Carnivore carnivore;
}Being;

typedef struct {
    pthread_mutex_t lock;
    Being being;
    TypeOfBeing typeOfBeing;
}BeingContainer;
///

typedef struct {
    int x, y;
} Point;

typedef struct {
    Point point;
    TypeOfBeing typeOfBeing;
}NewBeing;

typedef struct {
    bool isEmpty;
    int position, nextAvailablePositon, size;
    pthread_mutex_t lock;
    NewBeing data[SizeOfMatrix];
} QueueThread;

typedef struct {
    BeingContainer m[SizeOfMatrix][SizeOfMatrix];
    QueueThread queueThread;
}Matrix;

typedef struct QueueNode {
    Point point;
    int dist;
    struct QueueNode *prev;  // Pointer to the previous QueueNode
} QueueNode;

typedef struct {
    QueueNode *data;
    int front, rear;
} Queue;

Matrix *matrixPtr;

// Directions for 4-connected grid (up, down, left, right)
int dirX[] = { -1, 1, 0, 0 };
int dirY[] = { 0, 0, -1, 1 };

///////////////////////////
bool isStillRunning = true;
pthread_mutex_t threadMutex;
pthread_mutex_t ReadWriteMutex;
sem_t * producerSemaphore;
sem_t  * consumerSemaphore; 
pid_t groupId = 0; 
int shm_id;
WINDOW *matrixWin; 

Queue* createQueue(int size);
void enqueue(Queue* q, int x, int y, int dist, QueueNode* prev);
QueueNode* dequeue(Queue* q);
// BFS function to find the nearest 'P' to the given 'I' in the matrix
Point searchForPlantsIn10x10(int startX, int startY, Matrix * matrixPtr);
Point searchForHerbivoreIn10x10(int startX, int startY, bool onlySearchForFemales, Matrix * matrixPtr);
Point searchForCarnivoreFIn10x10(int startX, int startY, Matrix * matrixPtr);
QueueThread createQueueThread();
void enqueueThread(QueueThread * queue, NewBeing newBeing);
NewBeing dequeueThread(QueueThread * queue);
void * placeNewBeing(void *argv);
void createNewBeing(int x, int y, TypeOfBeing typeOfBeing, Matrix * matrixPtr);
void plantBehavior(int x, int y, Matrix * matrixPtr, bool pauseIt);
void herbivoreBehavior(int x, int y, Matrix *matrixPtr,  bool pauseIt);
void carnivoreBehavior(int x, int y, Matrix *matrixPtr, bool pauseIt) ;
void justWakeUpTheKids(int sig);
void sigintHandler(int sig);

int main() {

    srand(time(NULL));

    //we use this to wake up the kids 
    if (signal(SIGUSR1, justWakeUpTheKids) == SIG_ERR) {
        perror("Error setting signal handler");
        return 1;
    }


    // Generarea unei chei unice pentru zona de memorie partajată
    key_t key = ftok("DDD_sem1_2024_2025", 65); // Asigură-te că myfile există

    // Crearea zonei de memorie partajată
    shm_id = shmget(key, sizeof(Matrix), IPC_CREAT | 0666);
    if (shm_id < 0) {
        perror("shmget");
        _exit(1);
    }

    // Ataching the shared memory zone
    matrixPtr = shmat(shm_id, NULL, 0);
    if (matrixPtr == (Matrix *) -1) {
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
    sem_unlink(SEM_CREATE_BEING);
    producerSemaphore = sem_open(SEM_CREATE_BEING, O_CREAT, 0666, SizeOfQueue);

    if (producerSemaphore == SEM_FAILED) {
        perror("Sem_open SEM_CREAZA_VIETATE deschidere initiala1");
        _exit(1);
    }

    sem_unlink(SEM_PLACE_BEING);
    consumerSemaphore = sem_open(SEM_PLACE_BEING, O_CREAT, 0666, 0);

    if (consumerSemaphore == SEM_FAILED) {
        perror("Sem_open SEM_PLACE_BEING deschidere initiala2");
        _exit(1);
    }
    
    matrixPtr->queueThread = createQueueThread();

    pthread_mutexattr_t attr;
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
    pthread_mutex_init(&matrixPtr->queueThread.lock, &attr);
    
    for (int i = 0; i < SizeOfMatrix; i++) {
        for (int j = 0; j < SizeOfMatrix; j ++) {
            pthread_mutexattr_init(&attr);
            pthread_mutexattr_setpshared(&attr, PTHREAD_PROCESS_SHARED);
            pthread_mutex_init(&matrixPtr->m[i][j].lock, &attr);
            pid_t parinte;
            int z = rand();
            
            if (z % 20 == 3) {
                switch (rand() % 3) {
                case 0:
                    parinte = fork();
                    if(!parinte){
                    setpgid(0, groupId);
                    matrixPtr->m[i][j].being.plant = createPlant();
                    matrixPtr->m[i][j].typeOfBeing = P; 
                    plantBehavior(i,j,matrixPtr,  true);
                    _exit(0);
                    } 
                    break;
                case 1:
                    parinte = fork();
                    if(!parinte){
                    setpgid(0, groupId);
                    matrixPtr->m[i][j].being.herbivore = createHerbivore(rand() % 2);
                    matrixPtr->m[i][j].typeOfBeing = I; 
                    herbivoreBehavior(i,j,matrixPtr, true);
                    _exit(0);
                    } 
                    break;
                case 2:
                    parinte = fork();
                    if(!parinte){
                    setpgid(0, groupId);
                    matrixPtr->m[i][j].being.carnivore = createCarnivore(rand() % 2);
                    matrixPtr->m[i][j].typeOfBeing = C; 
                    carnivoreBehavior(i,j,matrixPtr,  true);
                    _exit(0);
                    }
                    break;
                default:
                    perror("Problema la alocarea matricei");
                    break;
                }
                
            } else {
                matrixPtr->m[i][j].typeOfBeing = FREE;
            }
        }
        
    }

    pthread_t thread;  // We Create a new thread to handle new request of forking new kids
                       
    // Create a new thread
    int result = pthread_create(&thread, NULL, placeNewBeing, NULL);
    if (result != 0) {
        perror("Thread creation failed");
        return 1;
    }

    if (signal(SIGINT, sigintHandler) == SIG_ERR) {
    perror("Error setting signal handler");
    return 1;
    }
    
    
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
    int start_y = (max_y - SizeOfMatrix - 2) / 2; // Center the box vertically
    int start_x = (max_x - SizeOfMatrix - 2) / 2; // Center the box horizontally
    matrixWin = newwin(SizeOfMatrix + 2 , SizeOfMatrix  + 2, 1, 1);
    box(matrixWin, 0, 0);
   
    int ch;
    

    // //trezim copiii
    kill(-groupId, SIGUSR1);
    // napms(5000);
    int z = 0;
    int numberHerbivores = 0, numberCarnivores = 0;
    while (1) {
        ch = getch(); 
        // Clear the previous matrix
        werase(matrixWin);
        box(matrixWin, 0, 0); // Redraw the box

        // Populate the matrix with random values
        numberHerbivores = numberCarnivores = 0;
        for (int i = 0; i < SizeOfMatrix; i++) {
            for (int j = 0; j < SizeOfMatrix; j++) {
                // printf("_exit1\n");
                pthread_mutex_lock(&(matrixPtr->m[i][j].lock));

                if(matrixPtr->m[i][j].typeOfBeing != FREE){
                    switch (matrixPtr->m[i][j].typeOfBeing) {
                    case P:
                        wattron(matrixWin, COLOR_PAIR(1)); 
                        mvwprintw(matrixWin, i + 1, j + 1, "%c", 'P');
                        wattroff(matrixWin, COLOR_PAIR(1));
                        break;
                    case I:
                        wattron(matrixWin, COLOR_PAIR(2)); 
                        if (matrixPtr->m[i][j].being.herbivore.sex == M)
                        mvwprintw(matrixWin, i + 1, j + 1, "%c", 'I');
                        else mvwprintw(matrixWin, i + 1, j + 1, "%c", 'i');
                        wattroff(matrixWin, COLOR_PAIR(2));
                        numberHerbivores ++;
                        break;
                    case C:
                        wattron(matrixWin, COLOR_PAIR(3)); 
                        if (matrixPtr->m[i][j].being.carnivore.sex == M)
                        mvwprintw(matrixWin, i + 1, j + 1, "%c", 'C');
                        //printf("C");
                        else mvwprintw(matrixWin, i + 1, j + 1, "%c", 'c');
                        //printf("c");
                        wattroff(matrixWin, COLOR_PAIR(3));
                        numberCarnivores ++;
                        break;
                        break;
                    default:
                        break;
                    }
                } else mvwprintw(matrixWin, i + 1, j + 1, " ");
                
                pthread_mutex_unlock(&(matrixPtr->m[i][j].lock));
                
            }
            
        }
        //Iesim daca sunt numai plante
        if (!numberCarnivores && !numberHerbivores) {
            break;
        } 
        //Refresh the window to show the updated matrix
        
        wrefresh(matrixWin);

        if (ch == 'q' || ch == 'Q') {
            break; // _exit the loop if 'q' is pressed
        } else napms(100);

    }

    delwin(matrixWin);
    endwin();
    // Curățarea
    pthread_mutex_lock(&threadMutex);
    isStillRunning = false;
    pthread_mutex_unlock(&threadMutex);

    pthread_join(thread, NULL);
    //terminam toate procesele
    kill(-groupId, SIGTERM);

    pthread_mutex_destroy(&threadMutex);
    for (int i = 0; i < SizeOfMatrix; i++) {
        for (int j = 0; j < SizeOfMatrix; j++) {
            pthread_mutex_destroy(&matrixPtr->m[i][j].lock);
        }
    }
    pthread_mutex_destroy(&matrixPtr->queueThread.lock);
    sem_close(producerSemaphore);
    sem_close(consumerSemaphore);

    // Dezatașare și ștergere
    shmdt(matrixPtr);
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
Point searchForPlantsIn10x10(int startX, int startY, Matrix * matrixPtr) {
    // Initialize the visited matrix
    bool visited[SizeOfMatrix][SizeOfMatrix] = { false };
    Queue* q = createQueue(SizeOfMatrix * SizeOfMatrix);  // Max size for 100x100 grid
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
        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
        TypeOfBeing typeOfBeing = matrixPtr->m[x][y].typeOfBeing;
        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

        if (typeOfBeing == P) {
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
            if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix && !visited[nx][ny]) {
                // Restrict the BFS to a 10x10 region around the start
                if (nx >= startX - 5 && nx <= startX + 5 && ny >= startY - 5 && ny <= startY + 5) {
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    TypeOfBeing typeNeighour = matrixPtr->m[nx][ny].typeOfBeing;
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if(typeNeighour != I && typeNeighour != C) {
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

Point searchForHerbivoreIn10x10(int startX, int startY, bool searchOnlyFemales, Matrix * matrixPtr) {
    // Initialize the visited matrix
    bool visited[SizeOfMatrix][SizeOfMatrix] = { false };
    Queue* q = createQueue(SizeOfMatrix * SizeOfMatrix);  // Max size for 100x100 grid
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
        bool  iFoundIt = false;
        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
        if (matrixPtr->m[x][y].typeOfBeing == I) {
            if(searchOnlyFemales) {
                if (matrixPtr->m[x][y].being.herbivore.sex  == F) {
                iFoundIt = true;
                }
            } else iFoundIt = true;     
        }
        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

        if (iFoundIt) {
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
            if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix && !visited[nx][ny]) {
                // Restrict the BFS to a 10x10 region around the start
                bool notMaleHerbivore = true;
                if (nx >= startX - 5 && nx <= startX + 5 && ny >= startY - 5 && ny <= startY + 5) {
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    TypeOfBeing typeNeighour = matrixPtr->m[nx][ny].typeOfBeing;
                    if (typeNeighour == I) {
                        if (matrixPtr->m[nx][ny].being.herbivore.sex == M) notMaleHerbivore = false;
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if(typeNeighour != P && typeNeighour != C && notMaleHerbivore) {
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

Point searchForCarnivoreFIn10x10(int startX, int startY, Matrix * matrixPtr) {
    // Initialize the visited matrix
    bool visited[SizeOfMatrix][SizeOfMatrix] = { false };
    Queue* q = createQueue(SizeOfMatrix * SizeOfMatrix);  // Max size for 100x100 grid
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
        bool  iFoundIt = false;
        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
        if (matrixPtr->m[x][y].typeOfBeing == C) {
            if (matrixPtr->m[x][y].being.carnivore.sex  == F) {
                iFoundIt = true;
            }
        }
        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

        if (iFoundIt) {
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
            if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix && !visited[nx][ny]) {
                // Restrict the BFS to a 10x10 region around the start
                bool notMaleCarnivore = true;
                if (nx >= startX - 5 && nx <= startX + 5 && ny >= startY - 5 && ny <= startY + 5) {
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    TypeOfBeing typeNeighour = matrixPtr->m[nx][ny].typeOfBeing;
                    if (typeNeighour == C) {
                        if (matrixPtr->m[nx][ny].being.carnivore.sex == M)
                            notMaleCarnivore = false;
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if(typeNeighour != P && typeNeighour != I && notMaleCarnivore) {
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
    queue.size = SizeOfQueue;
    queue.isEmpty = true;
    return queue;
}

void enqueueThread(QueueThread * queue, NewBeing newBeing) {
    if (!queue) {
        perror("Pass a valid thread queue");
        _exit(1);
    }
    pthread_mutex_lock(&queue->lock);
    queue->isEmpty = false;
    queue->data[queue->nextAvailablePositon] = newBeing;
    if (queue->nextAvailablePositon + 1 < queue->size) {
        queue->nextAvailablePositon += 1;
    } else queue->nextAvailablePositon = 0;
    if (queue->nextAvailablePositon == queue->position) {
        if (queue->position + 1 < queue->size) {
            queue->position += 1;
        } else  queue->position = 0;
    }

    pthread_mutex_unlock(&queue->lock);
     
}

NewBeing dequeueThread(QueueThread * queue) {

    if (!queue) {
        perror("Pass a valid thread queue");
        _exit(1);
    }

    pthread_mutex_lock(&queue->lock);
    if (queue->isEmpty) {
        return (NewBeing){(Point){-1, -1}, FREE};
    }

    NewBeing newBeing = queue->data[queue->position];
    if (queue->position + 1 < queue->size) {
        queue->position += 1;
    } else queue->position = 0;
    if (queue->nextAvailablePositon == queue->position) {
        queue->isEmpty = true;
    }

    pthread_mutex_unlock(&queue->lock);
    return newBeing;
}

void * plaseazaVietateNoua(void *argv) {

    producerSemaphore = sem_open(SEM_CREATE_BEING,0);
    if (producerSemaphore == SEM_FAILED){
        perror("couldn't open the producerSemaphore in the thread");
        _exit(1);
    }
    consumerSemaphore = sem_open(SEM_PLACE_BEING, 0);
     if (consumerSemaphore == SEM_FAILED){
        perror("mata");
        _exit(1);
    }
    while (1) {
        // Lock the mutex to safely check the flag
        pthread_mutex_lock(&threadMutex);
        if (!isStillRunning) {
            pthread_mutex_unlock(&threadMutex);
            printf("Thread is terminating.\n");
            break;
        }
        pthread_mutex_unlock(&threadMutex);
        pid_t pid;
        sem_wait(consumerSemaphore);
            NewBeing newBeing;
            newBeing.point.x = -1; newBeing.point.y = -1;
            if (matrixPtr != NULL) {
                newBeing = dequeueThread(&matrixPtr->queueThread);
            } else {perror(" Thread doesn't have access to the matrix!!");}
            
            if (newBeing.point.x != -1 && newBeing.point.y != -1) {
                switch (newBeing.typeOfBeing) {
                    case P:
                        pid = fork();
                        if (!pid) {
                            plantBehavior(newBeing.point.x, newBeing.point.y, matrixPtr, false);
                            _exit(0);
                        }
                        break;
                    case I:
                        pid = fork();
                        if (!pid) {
                            herbivoreBehavior(newBeing.point.x, newBeing.point.y, matrixPtr, false);
                            _exit(0);
                        }
                        break;
                    case C:
                        pid = fork();
                        if (!pid) {
                            carnivoreBehavior(newBeing.point.x, newBeing.point.y, matrixPtr, false);
                            _exit(0);
                        }
                        break;
                    default:
                        break;
                }
            }
            
        sem_post(producerSemaphore);
    }
    return NULL;
}


void plantBehavior(int x, int y, Matrix * matrixPtr, bool pauseIt) {
    pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
    if (matrixPtr->m[x][y].typeOfBeing != P) { perror("Synchronization error occured in Plant"); _exit(1);}
    Plant self =  matrixPtr->m[x][y].being.plant;
    pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
    if (pauseIt) pause();
    int fecundation = 0;
    while (1) {
        int E = self.E;
        self.E -= 1;
        fecundation ++;
        if (self.E <= 0) {
            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            matrixPtr->m[x][y].typeOfBeing = FREE;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            break;
        }
        if (fecundation >= 30) {
            
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    // Restrict the BFS to a 10x10 region around the start
                    bool exit = false;
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (matrixPtr->m[nx][ny].typeOfBeing == FREE) {
                        fecundation = 0;
                        matrixPtr->m[nx][ny].typeOfBeing = P;
                        matrixPtr->m[nx][ny].being.plant = createPlant();
                        NewBeing newBeing = {nx, ny, P};
                        sem_wait(producerSemaphore);
                            enqueueThread(&matrixPtr->queueThread,newBeing);
                        sem_post(consumerSemaphore);
                        exit = true;
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if (exit) break;
                }
            } 
    
        } 

        
        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
        if(matrixPtr->m[x][y].typeOfBeing != P) {perror("Synchronization error occured in Plant"); _exit(1);}
        int diferenceE = E - matrixPtr->m[x][y].being.plant.E;
        self.E -= diferenceE;
        matrixPtr->m[x][y].being.plant.E = self.E;
        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
        
        sleep(1);
    }
    
}

void herbivoreBehavior(int x, int y, Matrix *matrixPtr,  bool pauseIt) {
    pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
    if(matrixPtr->m[x][y].typeOfBeing != I) perror("Synchronization error occured in Ierbivor");
    Herbivore self =  matrixPtr->m[x][y].being.herbivore;
    pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
    if (pauseIt) pause();
    int o = 0;
    switch (self.sex) {
    case M:
        while (1) {
            o ++;
            int E = self.E;
            self.E -= 1;
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                matrixPtr->m[x][y].typeOfBeing = FREE;
                pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
                break;
            }
            //Adiacenta
            
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];
                bool potiIntra = true;
                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (matrixPtr->m[nx][ny].typeOfBeing == P) {
                        self.E  += 2;
                        matrixPtr->m[nx][ny].being.plant.E -= 1;
                    }
                    if (self.E > 15 && matrixPtr->m[nx][ny].typeOfBeing == I) {
                        if (matrixPtr->m[nx][ny].being.herbivore.sex == F && matrixPtr->m[nx][ny].being.herbivore.gestation == -1) {
                           self.E -= 5;
                           if (matrixPtr->m[nx][ny].being.herbivore.gestation == -1)
                           matrixPtr->m[nx][ny].being.herbivore.gestation = 20; 
                        }
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                }
            }

            bool isActuallyDeath = false;

            if (self.E < 10) {
                
                Point moveTo = searchForPlantsIn10x10(x, y, matrixPtr);

                if (moveTo.x != -1 && moveTo.y != -1) {

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));
                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                       pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                        
                        if (matrixPtr->m[x][y].being.herbivore.E > 0) {
                            matrixPtr->m[x][y].typeOfBeing = FREE;
                            matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;
                        } else isActuallyDeath = true;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        if (!isActuallyDeath) {
                            matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = I;
                       
                            x = moveTo.x; y = moveTo.y;
                        }

                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                } 
            }

            if (self.E > 20) {
               
                Point moveTo = searchForHerbivoreIn10x10(x, y, true, matrixPtr);
                //if (f1 != NULL) {fprintf(f1, "%d %d \n", moveTo.x, moveTo.y); fflush(f1);}
                if (moveTo.x != -1 && moveTo.y != -1) {
                    bool isActuallyDeath = false;

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                        
                        if (matrixPtr->m[x][y].being.herbivore.E > 0) {
                            matrixPtr->m[x][y].typeOfBeing = FREE;
                            matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;
                        } else isActuallyDeath = true;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        if (!isActuallyDeath) {
                            matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = I;
                       
                            x = moveTo.x; y = moveTo.y;
                        }
                        
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                }
            }

            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            if(matrixPtr->m[x][y].typeOfBeing != I) { perror("Synchronization error occured in2 herbivore M"); _exit(1);}
            int diferenceE = E - matrixPtr->m[x][y].being.herbivore.E;
            self.E -= diferenceE;
            matrixPtr->m[x][y].being.herbivore.E = self.E;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            sleep(1);
        }
        break;
    case F:
        while (1) {
            o ++;
            int E = self.E;
            self.E -= 1;
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                matrixPtr->m[x][y].typeOfBeing = FREE;
                pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
                break;
            }
            bool gaveBirth = false;
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    // Restrict the BFS to a 10x10 region around the start
                    bool exit = false;
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (self.gestation == 0 && matrixPtr->m[nx][ny].typeOfBeing == FREE) {
                        self.gestation -= 1;
                        matrixPtr->m[nx][ny].typeOfBeing = I;
                        time_t seconds;
                        time(&seconds);
                        matrixPtr->m[nx][ny].being.herbivore = createHerbivore(seconds % 2);
                        NewBeing newBeing = {nx, ny, I};
                        sem_wait(producerSemaphore);
                            enqueueThread(&matrixPtr->queueThread,newBeing);
                        sem_post(consumerSemaphore);
                        gaveBirth = true;
                        exit = true;
                    }
                    if (matrixPtr->m[nx][ny].typeOfBeing == P) {
                        self.E  += 2;
                        matrixPtr->m[nx][ny].being.plant.E -= 1;
                        exit = true;
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if (exit) break;
                }
            }

            if (self.E < 10) {
                
                Point moveTo = searchForPlantsIn10x10(x, y, matrixPtr);
            
                if (moveTo.x != -1 && moveTo.y != -1) {
                    bool isActuallyDeath = false;
                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));
                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                        if (matrixPtr->m[x][y].being.herbivore.E > 0) {
                            matrixPtr->m[x][y].typeOfBeing = FREE;
                            matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;
                        } else isActuallyDeath = true;
                        
                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        if (!isActuallyDeath) {
                            matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = I;
                       
                            x = moveTo.x; y = moveTo.y;
                        }
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                } 
            }
            

            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            if(matrixPtr->m[x][y].typeOfBeing != I) { perror("Synchronization error occured in herbivore F"); _exit(1);}
            int diferenceE = E - matrixPtr->m[x][y].being.herbivore.E;
            if(gaveBirth) matrixPtr->m[x][y].being.herbivore.gestation = -1;
            else {
                if (self.gestation == -1 && matrixPtr->m[x][y].being.herbivore.gestation != -1) {
                self.gestation = matrixPtr->m[x][y].being.herbivore.gestation;
                } else {
                    matrixPtr->m[x][y].being.herbivore.gestation = self.gestation;
                }
            }
            self.E -= diferenceE;
            matrixPtr->m[x][y].being.herbivore.E = self.E;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            sleep(1);
        }
        break;
    default:
        break;
    }
    
}

void carnivoreBehavior(int x, int y, Matrix *matrixPtr, bool pauseIt) {
    pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
    if(matrixPtr->m[x][y].typeOfBeing != C) perror("Synchronization error occured in - Carnivore");
    Carnivore self =  matrixPtr->m[x][y].being.carnivore;
    pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
    if (pauseIt) pause();
    switch (self.sex) {
    case M:
        while (1) {
            int E = self.E;
            self.E -= 1;
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                matrixPtr->m[x][y].typeOfBeing = FREE;
                pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
                break;
            }
            bool maiCautaFemela = true;
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    // Restrict the BFS to a 10x10 region around the start
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (self.E > 15 && matrixPtr->m[nx][ny].typeOfBeing == C) {
                        if (matrixPtr->m[nx][ny].being.carnivore.sex == F ) { 
                           if (matrixPtr->m[nx][ny].being.carnivore.gestation == -1) {
                            self.E -= 5;
                            matrixPtr->m[nx][ny].being.carnivore.gestation = 20;
                           }
                        }
                    }
                    if (matrixPtr->m[nx][ny].typeOfBeing == I && matrixPtr->m[nx][ny].being.herbivore.E > 0)  {
                        self.E  += matrixPtr->m[nx][ny].being.herbivore.E;
                        matrixPtr->m[nx][ny].being.herbivore.E = 0;
                    }
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                }
            }

            if (self.E < 20) {
                
                Point moveTo = searchForHerbivoreIn10x10(x, y, false, matrixPtr);

                if (moveTo.x != -1 && moveTo.y != -1) {

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[x][y].typeOfBeing = FREE;
                        matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = C;
                       
                        x = moveTo.x; y = moveTo.y;
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                } 
            }

            if (self.E > 30) {

                Point moveTo = searchForCarnivoreFIn10x10(x, y, matrixPtr);

                if (moveTo.x != -1 && moveTo.y != -1) {

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[x][y].typeOfBeing = FREE;
                        matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = C;
                       
                        x = moveTo.x; y = moveTo.y;
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                }
            }

            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            if(matrixPtr->m[x][y].typeOfBeing != C) {
            perror("Synchronization error occured in carnivore M"); 
            _exit(1);}
            int diferenceE = E - matrixPtr->m[x][y].being.carnivore.E;
            self.E -= diferenceE;
            matrixPtr->m[x][y].being.carnivore.E = self.E;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            sleep(1);
        }
        break;
    case F:
        while (1) {
            int E = self.E;
            self.E -= 1;
            if (self.E <= 0) {
                pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
                matrixPtr->m[x][y].typeOfBeing = FREE;
                pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
                break;
            }
            bool gaveBirth = false;
            if (self.gestation != -1 && self.gestation != 0) self.gestation --;
            for (int i = 0; i < 4; i++) {
                int nx = x + dirX[i];
                int ny = y + dirY[i];

                if (nx >= 0 && nx < SizeOfMatrix && ny >= 0 && ny < SizeOfMatrix ) {
                    // Restrict the BFS to a 10x10 region around the start
                    bool exit = false;
                    pthread_mutex_lock(&(matrixPtr->m[nx][ny].lock));
                    if (self.gestation == 0 && matrixPtr->m[nx][ny].typeOfBeing == FREE) {
                        self.gestation -= 1;
                        matrixPtr->m[nx][ny].typeOfBeing = C;
                        time_t seconds;
                        time(&seconds);
                        matrixPtr->m[nx][ny].being.carnivore = createCarnivore((seconds + x + y)% 2);
                        NewBeing newBeing = {nx, ny, C};
                        sem_wait(producerSemaphore);
                            enqueueThread(&matrixPtr->queueThread,newBeing);
                        sem_post(consumerSemaphore);
                        gaveBirth = true;
                        exit = true;
                    }
                    if (matrixPtr->m[nx][ny].typeOfBeing == I && matrixPtr->m[nx][ny].being.herbivore.E > 0)  {
                        self.E  += matrixPtr->m[nx][ny].being.herbivore.E;
                        matrixPtr->m[nx][ny].being.herbivore.E = 0;
                        exit = true;
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[nx][ny].lock));
                    if (exit) break;
                }
            }

            if (self.E < 20) {
                
                Point moveTo = searchForHerbivoreIn10x10(x, y, false, matrixPtr);

                if (moveTo.x != -1 && moveTo.y != -1) {

                    pthread_mutex_lock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));
                    if (matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing == FREE ) {

                        pthread_mutex_lock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[x][y].typeOfBeing = FREE;
                        matrixPtr->m[moveTo.x][moveTo.y].being = matrixPtr->m[x][y].being;

                        pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));

                        matrixPtr->m[moveTo.x][moveTo.y].typeOfBeing  = C;
                       
                        x = moveTo.x; y = moveTo.y;
                    }
                    
                    pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                } 
            }
            

            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            if(matrixPtr->m[x][y].typeOfBeing != C) { perror("Synchronization error occured in carnivore F"); _exit(1);}
            int diferenceE = E - matrixPtr->m[x][y].being.carnivore.E;
            if(gaveBirth) matrixPtr->m[x][y].being.carnivore.gestation = -1;
            else {
                if (self.gestation == -1 && matrixPtr->m[x][y].being.carnivore.gestation != -1) {
                self.gestation = matrixPtr->m[x][y].being.carnivore.gestation;
                } else {
                    matrixPtr->m[x][y].being.carnivore.gestation = self.gestation;
                }
            }
            self.E -= diferenceE;
            matrixPtr->m[x][y].being.carnivore.E = self.E;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            sleep(1);
        }
        break;
    default:
        break;
    }
}

void justWakeUpTheKids(int sig) {}

void sigint_handler(int sig) {
    if(groupId)
    kill(-groupId, SIGTERM);
    if (matrixPtr != NULL) {
        for (int i = 0; i < SizeOfMatrix; i++) {
            for (int j = 0; j < SizeOfMatrix; j++) {
                pthread_mutex_destroy(&matrixPtr->m[i][j].lock);
            }
        }
        shmdt(matrixPtr);
        shmctl(shm_id, IPC_RMID, NULL);
    }
    delwin(matrixWin);
    endwin();
    _exit(0);  
}
       pthread_mutex_unlock(&(matrixPtr->m[moveTo.x][moveTo.y].lock));

                } 
            }
            

            pthread_mutex_lock(&(matrixPtr->m[x][y].lock));
            if(matrixPtr->m[x][y].typeOfBeing != C) { perror("Synchronization error occured in carnivore F"); _exit(1);}
            int diferenceE = E - matrixPtr->m[x][y].being.carnivore.E;
            if(gaveBirth) matrixPtr->m[x][y].being.carnivore.gestation = -1;
            else {
                if (self.gestation == -1 && matrixPtr->m[x][y].being.carnivore.gestation != -1) {
                self.gestation = matrixPtr->m[x][y].being.carnivore.gestation;
                } else {
                    matrixPtr->m[x][y].being.carnivore.gestation = self.gestation;
                }
            }
            self.E -= diferenceE;
            matrixPtr->m[x][y].being.carnivore.E = self.E;
            pthread_mutex_unlock(&(matrixPtr->m[x][y].lock));
            sleep(1);
        }
        break;
    default:
        break;
    }
}

void justWakeUpTheKids(int sig) {}

void sigint_handler(int sig) {
    if(groupId)
    kill(-groupId, SIGTERM);
    if (matrixPtr != NULL) {
        for (int i = 0; i < SizeOfMatrix; i++) {
            for (int j = 0; j < SizeOfMatrix; j++) {
                pthread_mutex_destroy(&matrixPtr->m[i][j].lock);
            }
        }
        shmdt(matrixPtr);
        shmctl(shm_id, IPC_RMID, NULL);
    }
    delwin(matrixWin);
    endwin();
    _exit(0);  
}
