# Game Of Life - Every Being is a Process (Linux only)
 This is was an assigment for my Operating Systems course.
Implement a Game of Life in the following way:
This the text of the assigment:
    
 2.On a 100x100 board (a matrix allocated in a shared memory segment, with cells protected by semaphores to prevent concurrent writing by multiple processes), there are plants, herbivorous animals, and carnivorous animals, with animals being male (M) or female (F). Each new participant comes into existence as a separate process that accesses the board and places a symbol (letter) in its current position: P (plant), I (male herbivore), i (female herbivore), C (male carnivore), c (female carnivore). Each participant has an initial energy level E: P has 100, I or i has 40, and C or c has 60. i and c have a gestation period G, initially set to -1. P does not move, but the rest can move.
    The game evolves in stages. In each stage, each participant performs a specific activity:
    
P (plant):

    Energy (E) decreases by 1, and if it reaches 0, the plant dies (disappears).
    E can decrease faster if there is an adjacent herbivore.
    Every 10 stages, it generates a child in an adjacent free position (if one exists).
    
I (male herbivore):

    E decreases by 1, and if it reaches 0, the animal dies (disappears).
    If adjacent to a P, E increases by 2 and the plant’s E decreases by 1.
    If E > 15 and adjacent to an i, E decreases by 5 and the i’s gestation (G) becomes 20.
    If E < 10, it moves 1x1 toward the closest P in a 10x10 neighboring area (if not already adjacent to a P and only if a unique P can be determined).
    If E > 20, it moves 1x1 toward the closest i in a 10x10 neighboring area (if not already adjacent to an i and only if a unique i can be determined).
    
i (female herbivore):
    E decreases by 1, and if it reaches 0, the animal dies (disappears).
    If adjacent to a P, E increases by 2 and the plant’s E decreases by 1.
    If E < 10, it moves 1x1 toward the closest P in a 10x10 neighboring area (if not already adjacent to a P and only if a unique P can be determined).
    If G is 0, it generates a child I or i in a free adjacent zone (if one exists), and G becomes -1.
    
C (male carnivore):

    E decreases by 1, and if it reaches 0, the animal dies (disappears).
    If adjacent to an I or i, E increases by the E of that I/i, and that I/i’s E becomes 0 (it dies).
    If E > 15 and adjacent to a c, E decreases by 5 and the c’s G becomes 20.
    If E < 20, it moves 1x1 toward the closest I/i in a 10x10 neighboring area (only if a unique I/i can be determined).
    If E > 30, it moves 1x1 toward the closest c in a 10x10 neighboring area (if not already adjacent to a c and only if a unique c can be determined).
    
c (female carnivore):

    E decreases by 1, and if it reaches 0, the animal dies (disappears).
    If adjacent to an I or i, E increases by the E of that I/i, and that I/i’s E becomes 0 (it dies).
    If E < 20, it moves 1x1 toward the closest I/i in a 10x10 neighboring area (only if a unique I/i can be determined).
    If G is 0, it generates a child C or c in a free adjacent zone (if one exists), and G becomes -1.
    
The Game of Life ends when the board is empty or only plants remain. The numerical values above can be adjusted to achieve more dramatic effects. The interface will be ncurses.

The following figures show how I tackled the problem. The main process (referred to as the Parent) first creates the shared memory for the matrix, which holds the "beings" of the application.
Then, the Parent creates a process that starts a new group, known as the Leader. The Leader manages the group and must remain alive until the end because the Parent needs to be able 
to kill all of its child processes and unpause them. To ensure this, we use a while(1) pause(); loop.

After that, the Parent populates the matrix with beings (each being is a process), and these beings are put on pause. The Parent then creates a new thread to handle requests from the beings,
which may ask for their children to be created and placed in the matrix.

Once this setup is complete, the Parent unpauses its children, and the game begins. The game also manages the SIGINT signal and can be stopped at any time by pressing 'Q'.
![Screenshot from 2024-11-19 12-41-53](https://github.com/user-attachments/assets/4606da0c-3e5c-4bfe-ba82-c6d875332a7a)
![Screenshot from 2024-11-19 12-50-40](https://github.com/user-attachments/assets/6ae97e18-a4d9-4141-b96d-52d14c41ecbc)

Previews:

![Screenshot from 2024-11-19 11-38-14](https://github.com/user-attachments/assets/b89abe5a-eb36-49de-b48f-112afce762f4)
![Screenshot from 2024-11-19 11-39-08](https://github.com/user-attachments/assets/a30e528b-ef80-4b41-a7ac-0acc3aa5d3b8)
![Screenshot from 2024-11-19 11-40-20](https://github.com/user-attachments/assets/96a1ed24-78c4-498b-94f6-aec4af53ec76)


