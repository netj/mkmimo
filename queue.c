#include "queue.h"

typedef struct Node Node;
typedef struct Queue Queue;

struct Node;
struct Queue;

Queue *new_queue() {
  Queue *q = (Queue *) malloc(sizeof(Queue));
  q->first = NULL;
  q->last = NULL;
  return q;
}

void push(Queue *q, int *elem) {
  // Create a new queue node
  Node *new_node = (Node *) malloc(sizeof(Node));
  new_node->elem = elem;
  new_node->next = NULL;

  // Insert it into the queue
  if (is_empty(q)) {
    q->first = new_node;
    q->last = new_node;
  } else {           
    q->last->next = new_node;
    q->last = new_node;
  }
}

Node* peek(Queue *q) {
  return q->first;
}

int *pop(Queue *q) {   
  int *elem = q->first->elem;
  Node *next = q->first->next;
  free(q->first);

  q->first = next;
  return elem;
}

bool is_empty(Queue *q) {       
  if (q->first == NULL) return true;    
  return false;
}

int main() {
  return 0;
}
