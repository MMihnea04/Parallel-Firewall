// SPDX-License-Identifier: BSD-3-Clause

#include <pthread.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>

#include "consumer.h"
#include "ring_buffer.h"
#include "packet.h"
#include "utils.h"

// global context for consumers
static so_consumer_ctx_t global_ctx;

static void *consumer_thread(void *arg)
{
	/* TODO: implement consumer thread */

	// get context for consumer
	so_consumer_ctx_t *consumer_ctx = (so_consumer_ctx_t *)arg;
	char buffer_in[PKT_SZ];

	for (;;) {
		// get packet from ring buffer
		unsigned long seq_nmbr;
		ssize_t buff_read_sz = ring_buffer_dequeue(consumer_ctx->producer_rb, buffer_in, PKT_SZ, &seq_nmbr);

		// if stopped, exit from thread
		if (buff_read_sz == 0)
			break;

		DIE(buff_read_sz < 0, "ring_buffer_dequeue");

		// process packet
		so_packet_t *packet = (so_packet_t *)buffer_in;
		so_action_t fw_act = process_packet(packet);
		unsigned long pkt_hash = packet_hash(packet);
		unsigned long pkt_timestamp = packet->hdr.timestamp;

		// save result
		if (seq_nmbr < consumer_ctx->total_packets) {
			consumer_ctx->results[seq_nmbr].action = fw_act;
			consumer_ctx->results[seq_nmbr].hash = pkt_hash;
			consumer_ctx->results[seq_nmbr].timestamp = pkt_timestamp;
		}
	}

	return NULL;
}

int create_consumers(pthread_t *tids,
	     int num_consumers,
	     so_ring_buffer_t *rb,
	     const char *out_filename,
	     unsigned long total_packets)
{
	int ret;

	(void)out_filename;

	// validate input
	DIE(tids == NULL, "tids NULL");
	DIE(rb == NULL, "rb NULL");

	// initialize global context
	global_ctx.producer_rb = rb;
	global_ctx.total_packets = total_packets;
	global_ctx.next_seq = 0;

	// allocate memory for results
	global_ctx.results = calloc(total_packets, sizeof(*global_ctx.results));
	DIE(global_ctx.results == NULL, "calloc results");

	for (int i = 0; i < num_consumers; i++) {
		ret = pthread_create(&tids[i], NULL, consumer_thread, &global_ctx);
		DIE(ret != 0, "pthread_create");
	}

	return num_consumers;
}

void consumer_flush_logs(const char *out_filename)
{
	int fd;
	char buffer_out[256];

	// open output file
	fd = open(out_filename, O_RDWR | O_CREAT | O_TRUNC, 0666);
	DIE(fd < 0, "open");

	// get total number of packets
	unsigned long pkt_count = global_ctx.total_packets;

	for (unsigned long i = 0; i < pkt_count; i++) {
		// get result
		so_result_t *rez = &global_ctx.results[i];

		int str_len = snprintf(buffer_out, sizeof(buffer_out), "%s %016lx %lu\n",
			   RES_TO_STR(rez->action),
			   rez->hash,
			   rez->timestamp);

		// write to output file and verify if everything was written

		ssize_t write_ret = write(fd, buffer_out, str_len);

		if (write_ret < 0)
			perror("write");
	}

	close(fd);

	// free memory for results and reset fields
	free(global_ctx.results);
	global_ctx.results = NULL;
	global_ctx.total_packets = 0;
	global_ctx.next_seq = 0;
}
