/*
 * Copyright 2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * vmem_fork.c -- unit test for libvmem fork() support
 *
 * usage: vmem_fork [c|e] <nfork> <nthread>
 */

#include <libvmem.h>
#include <sys/wait.h>
#include "unittest.h"

#define NBUFS 16

struct threadarg {
	VMEM *vmp;
	size_t sizes[NBUFS];
};
typedef struct threadarg threadarg_t;

/*
 * get_rand_size -- returns random size of allocation
 */
static size_t
get_rand_size(void)
{
	return sizeof(int) + 64 * (rand() % 100);
}

/*
 * do_test -- thread callback
 *
 * This function is called in separate thread and the main thread
 * forks a child processes. Please be aware that any locks held in this
 * function may potentially cause a deadlock.
 *
 * For example using rand() in this function may cause a deadlock because
 * it grabs an internal lock.
 */
static void *
do_test(void *arg)
{
	threadarg_t *a = (threadarg_t *)arg;

	int **bufs = vmem_malloc(a->vmp, NBUFS * sizeof(void *));
	UT_ASSERTne(bufs, NULL);

	for (int j = 0; j < NBUFS; j++) {
		bufs[j] = vmem_malloc(a->vmp, a->sizes[j]);
		UT_ASSERTne(bufs[j], NULL);
	}

	for (int j = 0; j < NBUFS; j++) {
		UT_ASSERT(vmem_malloc_usable_size(a->vmp, bufs[j]) >=
			a->sizes[j]);
		vmem_free(a->vmp, bufs[j]);
	}

	vmem_free(a->vmp, bufs);

	return NULL;
}

int
main(int argc, char *argv[])
{
	if (argc == 4 && argv[3][0] == 't') {
		exit(0);
	}

	START(argc, argv, "vmem_fork");

	if (argc < 4)
		UT_FATAL("usage: %s [c|e] <nfork> <nthread>", argv[0]);

	int nfork = atoi(argv[2]);
	int nthread = atoi(argv[3]);
	UT_ASSERT(nfork >= 0);
	UT_ASSERT(nthread >= 0);

	os_thread_t thread[nthread];
	int first_child = 0;

	/*
	 * Allocate memory for function vmem_create_in_region().
	 * Each thread gets its own chunk, so make sure we allocate enough.
	 */
	void *mem_pool = NULL;

	mem_pool = MMAP_ANON_ALIGNED(VMEM_MIN_POOL * nfork, 4 << 20);

	VMEM *vmp = vmem_create_in_region(mem_pool, VMEM_MIN_POOL * nfork);
	if (vmp == NULL)
		UT_FATAL("!vmem_create_in_region");

	int **bufs = vmem_malloc(vmp, nfork * NBUFS * sizeof(void *));
	UT_ASSERTne(bufs, NULL);

	size_t *sizes = vmem_malloc(vmp, nfork * NBUFS * sizeof(size_t));
	UT_ASSERTne(sizes, NULL);

	int *pids1 = vmem_malloc(vmp, nfork * sizeof(pid_t));
	UT_ASSERTne(pids1, NULL);

	int *pids2 = vmem_malloc(vmp, nfork * sizeof(pid_t));
	UT_ASSERTne(pids2, NULL);

	for (int i = 0; i < nfork; i++) {
		for (int j = 0; j < NBUFS; j++) {
			int idx = i * NBUFS + j;

			sizes[idx] = get_rand_size();
			bufs[idx] = vmem_malloc(vmp, sizes[idx]);
			UT_ASSERTne(bufs[idx], NULL);
			UT_ASSERT(vmem_malloc_usable_size(vmp,
				bufs[idx]) >= sizes[idx]);
		}

		threadarg_t **targs = vmem_malloc(vmp,
			sizeof(threadarg_t *) * nthread);
		UT_ASSERTne(targs, NULL);
		for (int t = 0; t < nthread; ++t) {
			targs[t] = vmem_malloc(vmp, sizeof(threadarg_t));
			UT_ASSERTne(targs[t], NULL);
			targs[t]->vmp = vmp;
			for (int j = 0; j < NBUFS; j++)
				targs[t]->sizes[j] = get_rand_size();
		}

		for (int t = 0; t < nthread; ++t) {
			PTHREAD_CREATE(&thread[t], NULL, do_test, targs[t]);
		}

		pids1[i] = fork();
		if (pids1[i] == -1)
			UT_OUT("fork failed");
		UT_ASSERTne(pids1[i], -1);

		if (pids1[i] == 0 && argv[1][0] == 'e' && i == nfork - 1) {
			int fd = os_open("/dev/null", O_RDWR, S_IWUSR);
			int res = dup2(fd, 1);
			UT_ASSERTne(res, -1);
			os_close(fd);
			execl("/bin/echo", "/bin/echo", "Hello world!", NULL);
		}

		pids2[i] = getpid();

		for (int j = 0; j < NBUFS; j++) {
			*bufs[i * NBUFS + j] = ((unsigned)pids2[i] << 16) + j;
		}

		if (pids1[i]) {
			/* parent */
			for (int t = 0; t < nthread; ++t) {
				PTHREAD_JOIN(thread[t], NULL);
				vmem_free(vmp, targs[t]);
			}

			vmem_free(vmp, targs);
		} else {
			/* child */
			first_child = i + 1;
		}

		for (int ii = 0; ii < i; ii++) {
			for (int j = 0; j < NBUFS; j++) {
				UT_ASSERTeq(*bufs[ii * NBUFS + j],
					((unsigned)pids2[ii] << 16) + j);
			}
		}
	}

	for (int i = first_child; i < nfork; i++) {
		int status;
		waitpid(pids1[i], &status, 0);
		UT_ASSERT(WIFEXITED(status));
		UT_ASSERTeq(WEXITSTATUS(status), 0);
	}

	vmem_free(vmp, pids1);
	vmem_free(vmp, pids2);

	for (int i = 0; i < nfork; i++) {
		for (int j = 0; j < NBUFS; j++) {
			int idx = i * NBUFS + j;

			UT_ASSERT(vmem_malloc_usable_size(vmp,
				bufs[idx]) >= sizes[idx]);
			vmem_free(vmp, bufs[idx]);
		}
	}

	vmem_free(vmp, sizes);
	vmem_free(vmp, bufs);

	if (first_child == 0) {
		vmem_delete(vmp);

		DONE(NULL);
	}
}
