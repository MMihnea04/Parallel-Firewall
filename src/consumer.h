/* SPDX-License-Identifier: BSD-3-Clause */

#ifndef SO_CONSUMER_H
#define SO_CONSUMER_H

#include <pthread.h>

#include "ring_buffer.h"
#include "packet.h"

typedef struct so_result_t {
so_action_t action;
unsigned long hash;
unsigned long timestamp;
} so_result_t;

typedef struct so_consumer_ctx_t {
so_ring_buffer_t *producer_rb;

/* results stored in arrival order */
so_result_t *results;
unsigned long total_packets;

/* atomic counter: not used when ring buffer tracks sequence */
volatile unsigned long next_seq;
} so_consumer_ctx_t;

/*
 * Start consumer threads.
 * total_packets is the number of packets in the input file.
 * out_filename is only used later in consumer_flush_logs().
 */
int create_consumers(pthread_t *tids,
     int num_consumers,
     so_ring_buffer_t *rb,
     const char *out_filename,
     unsigned long total_packets);

/* Called after joining consumers: writes all logs to file in order */
void consumer_flush_logs(const char *out_filename);

#endif /* SO_CONSUMER_H */

