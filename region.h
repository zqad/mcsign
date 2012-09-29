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

#ifndef _REGION_H
#define _REGION_H

#include <stdint.h>

enum region_format {
	classic,
	anvil,
};

struct region_desc {
	int fd;
	enum region_format format;
	uint32_t *sector_data;
	uint32_t *timestamps;
	char *mapped_file;
	off_t mapping_size;
};

int region_open(struct region_desc **region_desc, const char *filename);

int region_close(struct region_desc *region_desc);

int foreach_part_in_region(struct region_desc *region_desc,
                void (*func)(void *, size_t, void *), void *user_data);

#endif /* _REGION_H */
