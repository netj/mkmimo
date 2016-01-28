#include <stdio.h>  
#include <stdlib.h>
#include <stdbool.h>

typedef struct Node Node;
typedef struct Queue Queue;

struct Node {
  int *elem;
  Node *next;
};

struct Queue {
  Node *first, *last;
};

Queue *new_queue();
void queue(Queue *q, int *elem);
Node *peek(Queue *q);
int *dequeue(Queue *q);
bool is_empty(Queue *q);

