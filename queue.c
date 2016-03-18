#define _POSIX_C_SOURCE 200809L

#include "queue.h"
#include "mkmimo.h"

Queue *new_queue() {
  Queue *q = (Queue *)malloc(sizeof(Queue));
  q->first = NULL;
  q->last = NULL;
  q->free = NULL;
  CHECK_ERRNO(pthread_mutex_init, &(q->lock), NULL);
  CHECK_ERRNO(pthread_cond_init, &(q->is_non_empty), NULL);
  return q;
}

void queue(Queue *q, void *elem) {
  // Take a node from the free list
  Node *new_node = q->free;
  if (new_node == NULL) {
    // Create a new node if none were free
    new_node = (Node *)malloc(sizeof(Node));
  } else {
    // Update the free list
    q->free = new_node->next;
  }
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
  Node *node = q->first;
  q->first = node->next;
  int *elem = node->elem;
  // Put the node to the free list
  node->next = q->free;
  q->free = node;
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
  pthread_mutex_unlock(&(q->lock));
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
