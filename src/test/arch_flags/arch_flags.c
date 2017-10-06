/*
 * Copyright 2015-2017, Intel Corporation
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
 * arch_flags.c -- unit test for architecture flags
 */
#include <string.h>
#include <elf.h>
#include <link.h>

#include "unittest.h"
#include "pool_hdr.h"
#include "pmemcommon.h"

#define FATAL_USAGE()\
UT_FATAL("usage: arch_flags <file>:<alignment_desc>:<reserved>")
#define ARCH_FLAGS_LOG_PREFIX "arch_flags"
#define ARCH_FLAGS_LOG_LEVEL_VAR "ARCH_FLAGS_LOG_LEVEL"
#define ARCH_FLAGS_LOG_FILE_VAR "ARCH_FLAGS_LOG_FILE"
#define ARCH_FLAGS_LOG_MAJOR 0
#define ARCH_FLAGS_LOG_MINOR 0

/*
 * split_opts_path -- split options string and path to file
 *
 * The valid format is <char>:<opts>:<path>
 */
static int
split_path_opts(char *arg, char **path, char **opts)
{
	*path = arg;
	char *ptr = strchr(arg, ':');
	if (!ptr)
		return -1;
	*ptr = '\0';
	*opts = ptr + 1;
	return 0;
}

/*
 * read_arch_flags -- read arch flags from file
 */
static int
read_arch_flags(char *arg, struct arch_flags *arch_flags)
{
	char *path;
	char *opts;
	ElfW(Ehdr) elf;
	int fd;
	int ret;

	if ((ret = split_path_opts(arg, &path, &opts)))
		return ret;

	memset(&elf, 0, sizeof(elf));

	uint64_t alignment_desc;
	uint64_t reserved;

	if ((ret = sscanf(opts, "0x%jx:0x%jx", &alignment_desc,
			&reserved)) != 2)
		return -1;

	if ((fd = open(path, O_RDONLY)) < 0) {
		UT_ERR("!open %s", path);
		return 1;
	}
	ret = read(fd, &elf, sizeof(elf));
	close(fd);
	if (ret == -1) {
		UT_ERR("!read %s", path);
		return 1;
	}

	if (elf.e_ident[EI_MAG0] != ELFMAG0 ||
	    elf.e_ident[EI_MAG1] != ELFMAG1 ||
	    elf.e_ident[EI_MAG2] != ELFMAG2 ||
	    elf.e_ident[EI_MAG3] != ELFMAG3) {
		UT_OUT("invalid ELF magic");
		return 1;
	}

	arch_flags->e_machine = elf.e_machine;
	arch_flags->ei_class = elf.e_ident[EI_CLASS];
	arch_flags->ei_data = elf.e_ident[EI_DATA];
	arch_flags->alignment_desc = alignment_desc();

	if (alignment_desc)
		arch_flags->alignment_desc = alignment_desc;

	if (reserved)
		memcpy(arch_flags->reserved,
				&reserved, sizeof(arch_flags->reserved));

	return 0;
}

int
main(int argc, char *argv[])
{
	START(argc, argv, "arch_flags");

	common_init(ARCH_FLAGS_LOG_PREFIX,
		ARCH_FLAGS_LOG_LEVEL_VAR,
		ARCH_FLAGS_LOG_FILE_VAR,
		ARCH_FLAGS_LOG_MAJOR,
		ARCH_FLAGS_LOG_MINOR);

	if (argc < 3)
		FATAL_USAGE();

	for (int i = 1; i < argc; i++) {
		char *opts = argv[i];
		int ret;
		struct arch_flags arch_flags;

		if ((ret = read_arch_flags(opts, &arch_flags)) < 0)
			FATAL_USAGE();
		else if (ret == 0) {
			UT_OUT("check: %d",
				util_check_arch_flags(&arch_flags));
		}
	}

	common_fini();

	DONE(NULL);
}
