/*
 * mcsign - read signs from minecraft .mca and mcr files
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
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <glib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "nbt.h"
#include "debug.h"
#include "region.h"

#define SIGN_TAG "#map"

struct region_data {
	char *json_path;
	int x, y;
	char *filename;
	FILE *fp;
	int written;
};

struct sign_data {
	char **lines[3];
};

bool cnbt_map_sign(nbt_node *node, void *aux) {
	GQueue **queue = (GQueue **)aux;
	nbt_node *id_node;
	nbt_node *t1_node;
	nbt_node *t2_node, *t3_node, *t4_node;
	struct sign_data *data;
	
	/* Should be a compound */
	if (node->type != TAG_COMPOUND)
		return true;

	id_node = nbt_find_by_name(node, "id");

	/* Skip nodes w/o (correct) id subnodes */
	if (id_node == NULL || id_node->type != TAG_STRING)
		return true;

	/* Skip non-signs */
	if (strcmp(id_node->payload.tag_string, "Sign") != 0)
		return true;

	/* Skip non-#map-signs */
	t1_node = nbt_find_by_name(node, "Text1");
	if (t1_node->type != TAG_STRING ||
			strcmp(t1_node->payload.tag_string, SIGN_TAG) != 0)
		return true;
	
	/* Add node to queue */
	g_queue_push_head(*queue, node);

	return true;
}

void queue_output(gpointer data, gpointer user_data) {
	FILE *fp = (FILE *)user_data;
	nbt_node *node = (nbt_node *)data;
	int x, y, z;
	char *text2, *text3, *text4;

	x = nbt_find_by_name(node, "x")->payload.tag_int;
	y = nbt_find_by_name(node, "y")->payload.tag_int;
	z = nbt_find_by_name(node, "z")->payload.tag_int;
	text2 = nbt_find_by_name(node, "Text2")->payload.tag_string;
	text3 = nbt_find_by_name(node, "Text3")->payload.tag_string;
	text4 = nbt_find_by_name(node, "Text4")->payload.tag_string;

	fprintf(fp, "{ \"x\": \"%d\",  \"y\": \"%d\",  \"z\": \"%d\", "
			"\"msg\": \"%s %s %s (%d, %d, %d)\" }\n",
			x, y, z, text2, text3, text4, x, y, z);

	return;
}

void region_iterator(void *data, size_t len, void *user_data) {
	nbt_node *node_root, *node_levels, *node_te;
	GQueue *queue;
	FILE *fp;
	struct region_data *rdata = (struct region_data*)user_data;

	/* == Parse data == */
	node_root = nbt_parse_compressed(data, len);
	if (node_root == NULL) {
		ERR("Error when parsing for output %s at position %p: %d",
				rdata->filename, user_data, errno);
		return;
	}

	/* == Find signs and add them to the queue == */
	node_levels = nbt_find_by_name(node_root, "Level");
	if (node_levels == NULL || node_levels->type != TAG_COMPOUND) {
		ERR0("Could not find Level node");
		return;
	}
	node_te = nbt_find_by_name(node_root, "TileEntities");
	if (node_te == NULL || node_te->type != TAG_LIST) {
		ERR0("Could not find TileEntities node");
		return;
	}

	queue = g_queue_new();
	nbt_map(node_te, cnbt_map_sign, &queue);

	/* If list empty, remove any current files and go to next iteration */
	if (g_queue_is_empty(queue))
		goto out;

	/* Open file descriptor if not opened already */
	if (rdata->fp == NULL) {
		fp = fopen(rdata->filename, "w");
		if (fp < 0) {
			ERR("Unable to open output file %s: %d",
					rdata->filename, errno);
			exit(1);
		}

		rdata->fp = fp;
	}

	g_queue_foreach(queue, queue_output, fp);

out:
	/* == Free all the data! == */
	g_queue_free(queue);
	nbt_free(node_root);
}

void print_help(void) {
	ERR0("Usage: mcsign [WORLD PATH] [DESTINATION PATH]");
	ERR0("Where WORLD PATH is the path in the minecraft directory where");
	ERR0("the world is stored, usually 'world/'. This directory should");
	ERR0("contain, among other things, the 'region/' directory.");
	ERR0("DESTINATION PATH is where files containing sign information");
	ERR0("will be written, one for each .mca or .mcr that contains at");
	ERR0("least one matching sign. Signs need to contain only '#map'");
	ERR0("without single quotes on the first row to be output in these");
	ERR0("files.");
	ERR0("mcsign then read region coordinates on standard input, waiting");
	ERR0("for an end of file.");
	ERR0("");
	ERR0("Note: mcsign will erase any existing output file corresponding");
	ERR0("      to a region file if the region file is determined to not");
	ERR0("      contain a matching sign. This is done to enable");
	ERR0("      incremental re-runs.");
	ERR0("");
}

int main(int argc, char *argv[]) {
	int r;
	char *filename;
	int rx, ry;
	int len;
	FILE *f;
	struct region_desc *region;
	struct region_data rdata;
	char *json_path;
	char *world_path;

	if (argc != 3) {
		print_help();
		return 1;
	}
	while (scanf("%d %d", &rx, &ry) == 2) {

		world_path = argv[1];
		json_path = argv[2];

		DBG("main: %d %d", rx, ry);

		/* === Open and iterate inside region == */
		if (region_open(&region, world_path, rx, ry)) {
			ERR("Error while opening region %d %d", rx, ry);
			continue;
		}

		len = asprintf(&filename, "%s/signs.%d.%d.in", json_path,
				rx, ry);
		if (len < 0)
			return -ENOMEM;

		rdata.x = rx;
		rdata.y = ry;
		rdata.filename = filename;
		rdata.fp = NULL;

		foreach_part_in_region(region, region_iterator, &rdata);

		/* Remove file if nothing was found in the region file */
		if (rdata.fp == NULL)
			unlink(filename);
		else
			fclose(rdata.fp);

		free(filename);
		region_close(region);

	}
	return 0;
}
