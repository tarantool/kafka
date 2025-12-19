#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#include <common.h>
#include <queue.h>

////////////////////////////////////////////////////////////////////////////////////////////////////
/**
 * General thread safe queue based on linked list
 */

/**
 * Pop without locking mutex.
 * Caller must lock and unlock queue mutex by itself.
 * Use with caution!
 * @param queue
 * @return
 */
void *
queue_lockfree_pop(queue_t *queue) {
    if (queue == NULL)
        return NULL;

    void *output = NULL;

    if (queue->head != NULL) {
        output = queue->head->value;
        queue_node_t *tmp = queue->head;
        queue->head = queue->head->next;
        free(tmp);
        if (queue->head == NULL) {
            queue->tail = NULL;
        }

        queue->count -= 1;
    }

    return output;
}

void *
queue_pop(queue_t *queue) {
    if (queue == NULL)
        return NULL;

    pthread_mutex_lock(&queue->lock);
    void *output = queue_lockfree_pop(queue);
    pthread_mutex_unlock(&queue->lock);

    return output;
}

/**
 * Push without locking mutex.
 * Caller must lock and unlock queue mutex by itself.
 * Use with caution!
 * @param queue
 * @param value
 * @return
 */
void
queue_lockfree_push(queue_t *queue, void *value) {
    if (queue == NULL)
        return;

    queue_node_t *new_node;
    new_node = xmalloc(sizeof(queue_node_t));
    new_node->value = value;
    new_node->next = NULL;

    if (queue->tail != NULL)
        queue->tail->next = new_node;

    queue->tail = new_node;
    if (queue->head == NULL)
        queue->head = new_node;

    queue->count += 1;
}

int
queue_push(queue_t *queue, void *value) {
    if (value == NULL || queue == NULL) {
        return -1;
    }

    pthread_mutex_lock(&queue->lock);
    queue_lockfree_push(queue, value);
    pthread_mutex_unlock(&queue->lock);

    return 0;
}

queue_t *
new_queue() {
    queue_t *queue = xmalloc(sizeof(queue_t));
    if (pthread_mutex_init(&queue->lock, NULL) != 0) {
        free(queue);
        return NULL;
    }

    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;

    return queue;
}

void
destroy_queue(queue_t *queue) {
    if (queue == NULL)
        return;

    /* Drain remaining nodes (values are owned by caller) */
    pthread_mutex_lock(&queue->lock);
    queue_node_t *n = queue->head;
    while (n != NULL) {
        queue_node_t *next = n->next;
        free(n);
        n = next;
    }
    pthread_mutex_unlock(&queue->lock);

    pthread_mutex_destroy(&queue->lock);
    free(queue);
}
