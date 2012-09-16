/*
 * region - read minecraft .mca or mcr files
 *
 * Copyright Jonas Eriksson 2012
 *
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 */

#define _GNU_SOURCE

#include <stdio.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <errno.h>
#include <arpa/inet.h> /* For ntoh* */

#include "region.h"
#include "debug.h"

static int read_all(void *buf, int fd, size_t len) {
	size_t left = len;
	size_t part;
	while (left > 0) {
		part = read(fd, buf, len);
		if (part < 0) {
			ERR0("Short read");
			return -EIO;
		}
		left -= part;
	}

	return len;
}

int region_open(struct region_desc **rd, const char *world_path,
		int rx, int ry) {
	char *file;
	int len, fd;
	enum region_format format = anvil;
	struct region_desc *desc;
	struct stat stat_buf;

	len = asprintf(&file, "%s/region/r.%d.%d.mca", world_path, rx, ry);
	if (len < 0)
		return -1;

	fd = open(file, O_RDONLY);
	DBG("open '%s': %d", file, fd);
	if (fd < 0) {
		/* Fall back on .mcr by replacing the last a in the string and
		 * trying again */
		format = classic;
		file[len - 1] = 'r';
		fd = open(file, O_RDONLY);
		DBG("open '%s': %d", file, fd);
	}
	free(file);

	if (fd < 0)
		return fd;

	desc = malloc(sizeof(struct region_desc) + 4096 + 4096);
	if (desc == NULL) {
		close(fd);
		return -ENOMEM;
	}

	desc->fd = fd;
	desc->format = format;
	/* sector_data should point right after the struct region_desc, and
	 * the timestamps should point at that position + 4096, or + 4*1024 */
	desc->sector_data = (uint32_t*)&desc[1];
	desc->timestamps = &(desc->sector_data[1024]);

	if (read_all(desc->sector_data, fd, 4096) < 0) {
		free(desc);
		return -EIO;
	}
	if (read_all(desc->timestamps, fd, 4096) < 0) {
		free(desc);
		return -EIO;
	}

	/* Create the memory mapping */
	if (fstat(fd, &stat_buf)) {
		free(desc);
		ERR("Stat failed: %d", errno);
		return -EIO;
	}

	desc->mapped_file = mmap(NULL, stat_buf.st_size, PROT_READ,
			MAP_PRIVATE, fd, 0);
	if (desc->mapped_file == MAP_FAILED) {
		ERR("mmap failed: %d", errno);
		return -EIO;
	}
	desc->mapping_size = stat_buf.st_size;

	*rd = desc;

	return 0;
}

int region_close(struct region_desc *rd) {
	munmap(rd->mapped_file, rd->mapping_size);
	close(rd->fd);
	free(rd);
}

int foreach_part_in_region(struct region_desc *rd,
		void (*func)(void *, size_t, void *), void *user_data) {
	char *data;
	uint32_t timestamp;
	uint32_t tmp;
	size_t metadata_pos, file_pos, data_size;

	for (metadata_pos = 0; metadata_pos < 1024; metadata_pos++) {
		tmp = ntohl(rd->sector_data[metadata_pos]);
		data_size = (tmp & 0xff) * 4096;
		file_pos = (tmp >> 8) * 4096;
		if (file_pos == 0)
			continue;
		timestamp = ntohl(rd->timestamps[metadata_pos]);
		DBG("Chunk %d: %d:%d ts:%d", metadata_pos, file_pos,
				data_size, timestamp);

		/* Skipping 4 byte size and 1 byte compression format */
		/* XXX: Check compression format? */
		func(&(rd->mapped_file[file_pos + 5]), data_size - 1,
				user_data);
	}

}
