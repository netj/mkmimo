#include "queue.h"

Queue *new_queue() {
  Queue *q = (Queue *)malloc(sizeof(Queue));
  q->first = NULL;
  q->last = NULL;
  return q;
}

void queue(Queue *q, void *elem) {
  // Create a new queue node
  Node *new_node = (Node *)malloc(sizeof(Node));
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

Node *peek(Queue *q) { return q->first; }

void *dequeue(Queue *q) {
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

void queue_and_signal(Queue *q, void *elem) {
  pthread_mutex_lock(&(q->lock));
  queue(q, elem);
  pthread_cond_signal(&(q->is_non_empty));
}

void *dequeue_or_wait(Queue *q) {
  pthread_mutex_lock(&(q->lock));
  while (is_empty(q)) {
    pthread_cond_wait(&(q->is_non_empty), &(q->lock));
  }
  void *elem = dequeue(q);
  pthread_mutex_unlock(&(q->lock));
  return elem;
}
