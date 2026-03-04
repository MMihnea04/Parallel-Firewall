// SPDX-License-Identifier: BSD-3-Clause

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <pthread.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#include "ring_buffer.h"
#include "consumer.h"
#include "producer.h"
#include "log/log.h"
#include "packet.h"
#include "utils.h"

#define SO_RING_SZ		(PKT_SZ * 1000)

pthread_mutex_t MUTEX_LOG;

void log_lock(bool lock, void *udata)
{
	pthread_mutex_t *LOCK = (pthread_mutex_t *) udata;

	if (lock)
		pthread_mutex_lock(LOCK);
	else
		pthread_mutex_unlock(LOCK);
}

void __attribute__((constructor)) init()
{
	pthread_mutex_init(&MUTEX_LOG, NULL);
	log_set_lock(log_lock, &MUTEX_LOG);
}

void __attribute__((destructor)) dest()
{
	pthread_mutex_destroy(&MUTEX_LOG);
}

static void run_serial_firewall(const char *in_file, const char *out_file)
{
	char buffer[PKT_SZ], out_buf[PKT_SZ];
	ssize_t sz;
	int in_fd, out_fd, len;

	// open input file
	in_fd = open(in_file, O_RDONLY);
	DIE(in_fd < 0, "open in_fd");

	// open output file
	out_fd = open(out_file, O_RDWR | O_CREAT | O_TRUNC, 0666);
	DIE(out_fd < 0, "open out_fd");

	// read packets and process them
	while ((sz = read(in_fd, buffer, PKT_SZ)) != 0) {
		DIE(sz != PKT_SZ, "packet truncated");

		so_packet_t *pkt = (so_packet_t *)buffer;

		// process packet
		so_action_t action = process_packet(pkt);
		unsigned long hash = packet_hash(pkt);
		unsigned long timestamp = pkt->hdr.timestamp;

		// write to output file
		len = snprintf(out_buf, sizeof(out_buf), "%s %016lx %lu\n", RES_TO_STR(action), hash, timestamp);

		// write to output file and verify if everything was written

		ssize_t ret = write(out_fd, out_buf, len);

		if (ret < 0)
			perror("write");
	}

	close(in_fd);
	close(out_fd);
}

int main(int argc, char **argv)
{
	so_ring_buffer_t ring_buffer;
	int num_consumers, threads, rc;
	pthread_t *thread_ids = NULL;
	struct stat st;
	unsigned long total_packets;

	if (argc < 4) {
		fprintf(stderr, "Usage %s <input-file> <output-file> <num-consumers:1-32>\n", argv[0]);
		exit(EXIT_FAILURE);
	}

	num_consumers = strtol(argv[3], NULL, 10);

	if (num_consumers <= 0 || num_consumers > 32) {
		fprintf(stderr, "num-consumers [%d] must be in the interval [1-32]\n", num_consumers);
		exit(EXIT_FAILURE);
	}

	if (num_consumers == 1) {
		run_serial_firewall(argv[1], argv[2]);
		return 0;
	}

	rc = stat(argv[1], &st);
	DIE(rc < 0, "stat");
	total_packets = (unsigned long)(st.st_size / PKT_SZ);

	rc = ring_buffer_init(&ring_buffer, SO_RING_SZ);
	DIE(rc < 0, "ring_buffer_init");

	thread_ids = calloc(num_consumers, sizeof(pthread_t));
	DIE(thread_ids == NULL, "calloc pthread_t");

	/* create consumer threads */
	threads = create_consumers(thread_ids, num_consumers,
									   &ring_buffer, argv[2], total_packets);

	/* start publishing data */
	publish_data(&ring_buffer, argv[1]);

	/* TODO: wait for child threads to finish execution*/
	for (int i = 0; i < threads; i++)
		pthread_join(thread_ids[i], NULL);

	consumer_flush_logs(argv[2]);	ring_buffer_destroy(&ring_buffer);
	free(thread_ids);

	return 0;
}
