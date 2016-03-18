#ifndef QUEUE_H
#define QUEUE_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <pthread.h>

typedef struct Node Node;
typedef struct Queue Queue;

struct Node {
  int *elem;
  Node *next;
};

struct Queue {
  Node *first, *last, *free;
  pthread_mutex_t lock;
  pthread_cond_t is_non_empty;
};

Queue *new_queue();
void queue(Queue *q, void *elem);
Node *peek(Queue *q);
void *dequeue(Queue *q);
bool is_empty(Queue *q);

// multithread-friendly versions with mutex and condition variables
void queue_and_signal(Queue *q, void *elem);
void *dequeue_or_wait(Queue *q);

#endif /* QUEUE_H */
