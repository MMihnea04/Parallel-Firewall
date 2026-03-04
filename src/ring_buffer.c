// SPDX-License-Identifier: BSD-3-Clause

#include <stdlib.h>
#include <pthread.h>

#include "ring_buffer.h"

int ring_buffer_init(so_ring_buffer_t *ring, size_t cap)
{
	/* TODO: implement ring_buffer_init */

	int rc;

	if (!ring)
		return -1;

	ring->data = malloc(cap);
	if (!ring->data)
		return -1;

	// initialize buffer capacity and read/write positions
	ring->cap = cap;
	ring->len = 0;
	ring->read_pos = 0;
	ring->write_pos = 0;
	ring->stopped = 0;
	ring->packet_seq = 0;

	// initialize mutex and wait conditions
	rc = pthread_mutex_init(&ring->mutex, NULL);
	if (rc != 0) {
		free(ring->data);
		ring->data = NULL;
		return -1;
	}

	rc = pthread_cond_init(&ring->can_produce, NULL);
	if (rc != 0) {
		pthread_mutex_destroy(&ring->mutex);
		free(ring->data);
		ring->data = NULL;
		return -1;
	}

	rc = pthread_cond_init(&ring->can_consume, NULL);
	if (rc != 0) {
		pthread_cond_destroy(&ring->can_produce);
		pthread_mutex_destroy(&ring->mutex);
		free(ring->data);
		ring->data = NULL;
		return -1;
	}

	return 0;
}

ssize_t ring_buffer_enqueue(so_ring_buffer_t *ring, const void *data, size_t size)
{
	const char *src;
	size_t space_to_end;

	if (!ring || !data || size == 0)
		return -1;

	src = (char *)data;

	pthread_mutex_lock(&ring->mutex);

	while (ring->len + size > ring->cap && !ring->stopped)
		pthread_cond_wait(&ring->can_produce, &ring->mutex);

	if (ring->stopped) {
		pthread_mutex_unlock(&ring->mutex);
		return -1;
	}

	space_to_end = ring->cap - ring->write_pos;

	if (size <= space_to_end) {
		memcpy(ring->data + ring->write_pos, src, size);
	} else {
		memcpy(ring->data + ring->write_pos, src, space_to_end);
		memcpy(ring->data, src + space_to_end, size - space_to_end);
	}

	ring->write_pos = (ring->write_pos + size) % ring->cap;
	ring->len += size;

	pthread_cond_signal(&ring->can_consume);
	pthread_mutex_unlock(&ring->mutex);

	return (ssize_t)size;
}

ssize_t ring_buffer_dequeue(so_ring_buffer_t *ring, void *data, size_t size, unsigned long *seq)
{
	/* TODO: Implement ring_buffer_dequeue */

	char *dst;
	size_t space_to_end;

	if (!ring || !data || size == 0)
		return -1;

	dst = (char *)data;

	pthread_mutex_lock(&ring->mutex);

	// wait for data in buffer
	while (ring->len < size && !ring->stopped)
		pthread_cond_wait(&ring->can_consume, &ring->mutex);

	// if stopped, exit
	if (ring->len < size) {
		pthread_mutex_unlock(&ring->mutex);
		return 0;
	}

	// obtain sequence if needed
	if (seq)
		*seq = __sync_fetch_and_add(&ring->packet_seq, 1);

	// copy data from buffer to data
	space_to_end = ring->cap - ring->read_pos;
	if (size <= space_to_end) {
		memcpy(dst, ring->data + ring->read_pos, size);
	} else {
		memcpy(dst, ring->data + ring->read_pos, space_to_end);
		memcpy(dst + space_to_end, ring->data, size - space_to_end);
	}

	// update read position and length
	ring->read_pos = (ring->read_pos + size) % ring->cap;
	ring->len -= size;

	pthread_cond_signal(&ring->can_produce);
	pthread_mutex_unlock(&ring->mutex);

	return (ssize_t)size;
}

void ring_buffer_destroy(so_ring_buffer_t *ring)
{
	/* TODO: Implement ring_buffer_destroy */

	if (!ring || !ring->data)
		return;

	// set stopped and wake up all waiting threads
	pthread_mutex_lock(&ring->mutex);
	ring->stopped = 1;
	pthread_cond_broadcast(&ring->can_consume);
	pthread_cond_broadcast(&ring->can_produce);
	pthread_mutex_unlock(&ring->mutex);

	// destroy conditions and mutex
	pthread_cond_destroy(&ring->can_consume);
	pthread_cond_destroy(&ring->can_produce);
	pthread_mutex_destroy(&ring->mutex);

	// free memory and reset fields
	free(ring->data);
	ring->data = NULL;
	ring->cap = 0;
	ring->len = 0;
	ring->read_pos = 0;
	ring->write_pos = 0;
}

void ring_buffer_stop(so_ring_buffer_t *ring)
{
	/* TODO: Implement ring_buffer_stop */

	if (!ring)
		return;

	// set stopped and wake up consumers
	pthread_mutex_lock(&ring->mutex);
	ring->stopped = 1;
	pthread_cond_broadcast(&ring->can_consume);
	pthread_mutex_unlock(&ring->mutex);
}

