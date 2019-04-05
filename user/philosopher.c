#include "philosopher.h"

int spork[16] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1};
int mutex     = 1;

// Updates array to say sporks are in use
void pick_up_sporks(int id) {
  sem_wait(&spork[id]);
  sem_wait(&spork[(id + 1) % 16]);
}

// Updates array to say sporks are no longer in use
void put_down_sporks(int id) {
  sem_post(&spork[id]);
  sem_post(&spork[(id + 1) % 16]);
}

void main_philosopher() {

  for (int i = 0; i < 16; i++) {
    if (fork() == 0) { // Section that controls all the fork children
      int philosopher_id = i;
      char id_char[2];
      itoa(id_char, philosopher_id);

      while( 1 ) {

        // Philosopher thinks
        write( STDOUT_FILENO, "Philosopher ", 12 );write( STDOUT_FILENO, id_char, 2 );write( STDOUT_FILENO, " is thinking \n", 14 );
        sleep();
        sem_wait(&mutex);

        // Critical section start
        if ( spork[philosopher_id] && spork[(philosopher_id + 1) % 16] ) {
          // Philosopher picks up sporks
          pick_up_sporks(philosopher_id);
          sem_post(&mutex); // Critical section end

          // Philosopher eats
          write( STDOUT_FILENO, "Philosopher ", 12 );write( STDOUT_FILENO, id_char, 2 );write( STDOUT_FILENO, " is eating \n", 12 );
          sleep();

          write( STDOUT_FILENO, "Philosopher ", 12 );write( STDOUT_FILENO, id_char, 2 );write( STDOUT_FILENO, " puts his cutlery down \n", 24 );
          // Philosopher waits for lock to put down sporks
          sem_wait(&mutex);

          // Critical section start
          put_down_sporks(philosopher_id);
        }

        sem_post(&mutex); // Critical section end

      }
    }
  }

  exit( EXIT_SUCCESS );
}
