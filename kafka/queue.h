#ifndef TNT_KAFKA_QUEUE_H
#define TNT_KAFKA_QUEUE_H

#include <pthread.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * General thread safe queue based on licked list
 */

typedef struct queue_node_t {
    void *value;
    struct queue_node_t *next;
} queue_node_t;

typedef struct {
    pthread_mutex_t  lock;
    queue_node_t    *head;
    queue_node_t    *tail;
    int              count;
} queue_t;

/**
 * Pop without locking mutex.
 * Caller must lock and unlock queue mutex by itself.
 * Use with caution!
 * @param queue
 * @return
 */
void *
queue_lockfree_pop(queue_t *queue);

void *
queue_pop(queue_t *queue);

/**
 * Push without locking mutex.
 * Caller must lock and unlock queue mutex by itself.
 * Use with caution!
 * @param queue
 * @param value
 * @return
 */
int
queue_lockfree_push(queue_t *queue, void *value);

int
queue_push(queue_t *queue, void *value);

queue_t *
new_queue();

void destroy_queue(queue_t *queue);

#endif // TNT_KAFKA_QUEUE_H
