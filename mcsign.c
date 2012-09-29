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
#include <getopt.h>

#include "nbt.h"
#include "debug.h"
#include "region.h"

#define SIGN_TAG "#map"
#define DEFAULT_OUTPUT_FORMAT "{ \"x\": \"%x\", \"y\": \"%y\", " \
	"\"z\": \"%z\", \"msg\": \"%u %v %w (%x, %y, %z)\" },\n"
char *opt_output_format = DEFAULT_OUTPUT_FORMAT;
#define OUTPUT_BUF_SIZE 1024 /* Write output in at most 1k-blocks */

char *opt_world_path = NULL;
char *opt_output_path = NULL;

struct region_data {
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

static inline nbt_node *safe_nbt_find_by_name(nbt_node *start_node,
		const char *name) {
	nbt_node *found_node;

	found_node = nbt_find_by_name(start_node, name);
	if (found_node == NULL) {
		ERR("Internal error while searching for node %s.", name);
		exit(1);
	}
	
	return found_node;
}

static inline void fetch_value_int(nbt_node *node, const char *name,
		const int32_t **dst) {
	if (*dst != NULL)
		return;

	*dst = &(safe_nbt_find_by_name(node, name)->payload.tag_int);
}

static inline void fetch_value_str(nbt_node *node, const char *name,
		const char **dst) {
	nbt_node *found_node;

	if (*dst != NULL)
		return;

	*dst = safe_nbt_find_by_name(node, name)->payload.tag_string;
}

size_t outf(FILE *fp, const char *format, nbt_node *node) {
	char buf[OUTPUT_BUF_SIZE];
	int buf_left = OUTPUT_BUF_SIZE - 1;
	size_t total = 0;
	const char *fmt_it = format;
	char *buf_it = buf;
	int ret, handle_ret;

	/* Caches */
	const char *text1 = NULL;
	const char *text2 = NULL;
	const char *text3 = NULL;
	const char *text4 = NULL;
	const int32_t *x = NULL;
	const int32_t *y = NULL;
	const int32_t *z = NULL;

	while (*fmt_it != 0) {
		if (*fmt_it == '%') {
			/* Format character */
			fmt_it++;
			handle_ret = 0;
			switch (*fmt_it) {
			case 'x':
				fetch_value_int(node, "x", &x);
				ret = snprintf(buf_it, buf_left, "%d", *x);
				handle_ret = 1;
				break;
			case 'y':
				fetch_value_int(node, "y", &y);
				ret = snprintf(buf_it, buf_left, "%d", *y);
				handle_ret = 1;
				break;
			case 'z':
				fetch_value_int(node, "z", &z);
				ret = snprintf(buf_it, buf_left, "%d", *z);
				handle_ret = 1;
				break;
			case 't':
				fetch_value_str(node, "Text1", &text1);
				if (*text1 != 0) {
					ret = snprintf(buf_it, buf_left, "%s",
							text1);
					handle_ret = 1;
				}
				break;
			case 'u':
				fetch_value_str(node, "Text2", &text2);
				if (*text2 != 0) {
					ret = snprintf(buf_it, buf_left, "%s",
							text2);
					handle_ret = 1;
				}
				break;
			case 'v':
				fetch_value_str(node, "Text3", &text3);
				if (*text3 != 0) {
					ret = snprintf(buf_it, buf_left, "%s",
							text3);
					handle_ret = 1;
				}
				break;
			case 'w':
				fetch_value_str(node, "Text4", &text4);
				if (*text4 != 0) {
					ret = snprintf(buf_it, buf_left, "%s",
							*text4);
					handle_ret = 1;
				}
				break;
			case '%':
				*buf_it = '%';
				buf_it++;
				buf_left--;
			default:
				ERR("Illegal format character: '%c'",
						*fmt_it);
				exit(1);
			}

			if (handle_ret) {
				if (ret < buf_left) {
					buf_it += ret;
					buf_left -= ret;
				}
				else {
					/* Size left in buffer was to small,
					 * indicate that buffer should be
					 * written and rewind format iterator */
					buf_left = 0;
					fmt_it -= 2;
				}
			}
		}
		else {
			/* Normal character */
			*buf_it = *fmt_it;
			buf_it++;
			buf_left--;
		}
		fmt_it++;

		if (buf_left == 0) {
			*buf_it = 0;
			if (fputs(buf, fp) == EOF) {
				ERR("Error while writing to file: %d", errno);
				exit(1);
			}
			buf_it = buf;
			buf_left = OUTPUT_BUF_SIZE - 1;
		}
	}

	*buf_it = 0;
	if (fputs(buf, fp) == EOF) {
		ERR("Error while writing to file: %d", errno);
		exit(1);
	}

	return total;
}

void queue_output(gpointer data, gpointer user_data) {
	FILE *fp = (FILE *)user_data;
	nbt_node *node = (nbt_node *)data;
	int x, y, z;
	char *text2, *text3, *text4;

	outf(fp, opt_output_format, node);

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
	ERR0("Usage: mcsign [ OPTIONS ]");
	ERR0("Fetches sign data from Minecraft region files and outputs data to one file");
	ERR0("per region file read.");
	ERR0("");
	ERR0("Mandatory arguments to long options are mandatory for short options too.");
	ERR0("  -f, --format=FORMAT      specify how the output is to be formatted. If this");
	ERR0("                           argument is not specified, the default format will");
	ERR0("                           be used (see below).");
	ERR0("  -o, --output-path=PATH   where files containing sign information will be");
	ERR0("                           written, one for each .mca or .mcr that contains");
	ERR0("                           at least one matching sign. Signs need to contain");
	ERR0("                           only '#map' without single quotes on the first row");
	ERR0("                           to be output in these");
	ERR0("  -w, --world-path=PATH    the path in the minecraft directory where the");
	ERR0("                           world is stored, usually 'world/'. This directory");
	ERR0("                           should contain, among other things, the 'region/'");
	ERR0("                           directory.");
	ERR0("  -h, --help               display this help and exit");
	ERR0("");
	ERR0("Output path and world path are required arguments.");
	ERR0("");
	ERR0("When started, mcsign will read region coordinates on standard input, waiting");
	ERR0("for an end of file.");
	ERR0("");
	ERR0("Default output format:");
	ERR( "%s", DEFAULT_OUTPUT_FORMAT);
	ERR0("The interpreted sequences in FORMAT are:");
	ERR0("  %%t  First row of text in sign (this is what mcsign match against)");
	ERR0("  %%u  Second row of text in sign");
	ERR0("  %%v  Third row of text in sign");
	ERR0("  %%w  Fourth row of text in sign");
	ERR0("  %%x  X coordinate of the sign");
	ERR0("  %%y  Y coordinate of the sign");
	ERR0("  %%z  Z coordinate of the sign");
	ERR0("");
	ERR0("Note: mcsign will erase any existing output file corresponding");
	ERR0("      to a region file if the region file is determined to not");
	ERR0("      contain a matching sign. This is done to enable");
	ERR0("      incremental re-runs.");
	ERR0("");
	ERR0("mcsign home page: <http://github.com/zqad/mcsign/>");
}


static int parse_options(int argc, char *argv[])
{
	char opt;
	int option_index = 0;
	static struct option long_options[] = {
		{"help",        no_argument,       0,  0 },
		{"format",      required_argument, 0,  0 },
		{"output-path", required_argument, 0,  0 },
		{"world-path",  required_argument, 0,  0 },
		{0,             0,                 0,  0 }
	};
	const char *short_options = "hf:o:w:";

	while (1) {
		opt = getopt_long(argc, argv, short_options,
				long_options, &option_index);
		if (opt == -1)
			break;

		/* Map long opts to short opts */
		if (opt == 0) {
			switch (option_index) {
			case 0:
				opt = 'h';
				break;
			case 1:
				opt = 'f';
				break;
			case 2:
				opt = 'o';
				break;
			case 3:
				opt = 'w';
				break;
			}
		}
		switch (opt) {
		case 'h':
			print_help();
			exit(1);
			break;
		case 'f':
			opt_output_format = optarg;
			break;
		case 'o':
			opt_output_path = optarg;
			break;
		case 'w':
			opt_world_path = optarg;
			break;
		default:
			exit(1);
			break;
		}
	}

	/* Check that we got all info needed */
	if (opt_output_path == NULL || opt_world_path == NULL) {
		ERR0("Output path and world path are required arguments");
		exit(1);
	}

	return optind;
}

int main(int argc, char *argv[]) {
	int r;
	char *filename;
	int rx, ry;
	int len;
	FILE *f;
	struct region_desc *region;
	struct region_data rdata;

	parse_options(argc, argv);

	while (scanf("%d %d", &rx, &ry) == 2) {

		DBG("main: %d %d", rx, ry);

		/* === Open and iterate inside region == */
		if (region_open(&region, opt_world_path, rx, ry)) {
			ERR("Error while opening region %d %d", rx, ry);
			continue;
		}

		len = asprintf(&filename, "%s/signs.%d.%d.in", opt_output_path,
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
