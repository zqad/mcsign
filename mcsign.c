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

char *opt_output_path = NULL;

#define DEFAULT_WORKERS 1
int opt_workers = DEFAULT_WORKERS;

enum input_format {
	white_space,
	null,
};
enum input_format opt_input_format = white_space;

struct region_data {
	char *filename;
	FILE *fp;
	int written;
};

struct work {
	char *filename;
};

bool cnbt_map_sign(nbt_node *node, void *aux) {
	GQueue **queue = (GQueue **)aux;
	nbt_node *id_node;
	nbt_node *t1_node;
	
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
							text4);
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
		if (fp == NULL) {
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

void worker(gpointer data, gpointer user_data) {
	GAsyncQueue *buffer_queue = (GAsyncQueue *) user_data;
	struct work *work = (struct work *)data;
	int len;
	struct region_desc *region;
	struct region_data rdata;
	char *fn_iter;
	char *filename;

	DBG("worker: Got work: %p %s", work, work->filename);
	/* === Open and iterate inside region == */
	if (region_open(&region, work->filename)) {
		ERR("Error while opening region file '%s'", work->filename);
		return;
	}

	/* === Build the destination file name === */

	/* Find the base name of the file (file name w/o path) */
	fn_iter = &work->filename[strlen(work->filename) - 1];
	while (*fn_iter != '/' && fn_iter != work->filename)
		fn_iter--;

	/* Construct the target file name */
	len = asprintf(&filename, "%s/%s.sign", opt_output_path,
			fn_iter);
	if (len < 0)
		return;

	rdata.filename = filename;
	rdata.fp = NULL;

	/* Return the buffer */
	free(work->filename);
	work->filename = NULL;
	g_async_queue_push(buffer_queue, work);

	foreach_part_in_region(region, region_iterator, &rdata);

	/* Remove file if nothing was found in the region file */
	if (rdata.fp == NULL)
		unlink(filename);
	else
		fclose(rdata.fp);

	free(filename);
	region_close(region);
}

struct input_context {
	char *buffer;
	int bsize;
	int bpos;
};

void init_input_context(struct input_context *ic) {
	ic->buffer = malloc(1024);
	if (ic->buffer == NULL) {
		perror("mcsign");
		exit(1);
	}
	ic->bsize = 1024;
	ic->bpos = 0;
}

int static inline does_match(char c) {
	switch (c) {
	case '\t':
	case '\n':
	case '\r':
	case ' ':
		if (opt_input_format == white_space)
			return 1;
		break;

	case 0:
		if (opt_input_format == null)
			return 1;
		break;
	}

	return 0;
}

char *get_input(struct input_context *ic) {
	ssize_t r;
	char *tmp;
	int i, start_position;

	/* Real EOF indication */
	if (ic->buffer == NULL)
		return NULL;
	
	while (1) {
		/* Skip past any matched delimiter-characters in the beginning
		 * of the buffer */
		i = 0;
		while (i < ic->bpos && does_match(ic->buffer[i]))
			i++;

		/* Record start and search for next delimiter */
		start_position = i;
		while (i < ic->bpos && !does_match(ic->buffer[i]))
			i++;

		/* Do we have a match? */
		if (i <= ic->bpos && does_match(ic->buffer[i])
					&& i != start_position) {
			ic->buffer[i] = 0;

			tmp = strdup(&ic->buffer[start_position]);

			/* Increment i, as it is now pointing to the end of
			 * the previous delimited substring */
			i++;
			/* Anything worth saving? */
			if (i < ic->bpos)
				memmove(ic->buffer, &ic->buffer[i],
						ic->bpos - i);
			ic->bpos -= i;

			return tmp;
		}

		/* Do a read */
		r = read(STDIN_FILENO, &ic->buffer[ic->bpos],
				ic->bsize - ic->bpos);
		ic->bpos += r;
		/* Handle EOF */
		if (r == 0) {
			free(ic->buffer);
			ic->buffer = NULL;
			return NULL;
		}

		/* Grow buffer if we hit the ceiling */
		if (ic->bpos == ic->bsize) {
			ic->buffer = realloc(ic->buffer, ic->bsize * 2);
			if (ic->buffer == NULL) {
				perror("mcsign");
				exit(1);
			}
			ic->bsize *= 2;
		}

	}

}

void print_help(void) {
	ERR0("Usage: mcsign [ OPTIONS ]");
	ERR0("Fetches sign data from Minecraft region files and outputs data to one file");
	ERR0("per region file read.");
	ERR0("");
	ERR0("Mandatory arguments to long options are mandatory for short options too.");
	ERR0("  -0, --null               data on standard is terminated by a null characted");
	ERR0("                           (like find -print0 and xargs -0");
	ERR0("  -f, --format=FORMAT      specify how the output is to be formatted. If this");
	ERR0("                           argument is not specified, the default format will");
	ERR0("                           be used (see below).");
	ERR0("  -o, --output-path=PATH   where files containing sign information will be");
	ERR0("                           written, one for each .mca or .mcr that contains");
	ERR0("                           at least one matching sign. Signs need to contain");
	ERR0("                           only '#map' without single quotes on the first row");
	ERR0("                           to be output in these");
	ERR0("  -t, --threads=THREADS    the number of worker threads to be spawned,");
	ERR( "                           default: %d", DEFAULT_WORKERS);
	ERR0("  -h, --help               display this help and exit");
	ERR0("");
	ERR0("Output path is a required argument.");
	ERR0("");
	ERR0("When started, mcsign will read region file paths on standard input, waiting");
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
	ERR0("Note: Destination file names are generated from the source file name, meaning");
	ERR0("      that if two source files with the same file name (e.g. region/r.0.0.mca");
	ERR0("      and region/DIM-1/r.0.0.mca) is written to standard in, the first output");
	ERR0("      file will be overwritten.");
	ERR0("");
	ERR0("mcsign home page: <http://github.com/zqad/mcsign/>");
}

static int parse_options(int argc, char *argv[]) {
	char opt;
	int option_index = 0;
	static struct option long_options[] = {
		{"help",        no_argument,       0,  0 },
		{"format",      required_argument, 0,  0 },
		{"output-path", required_argument, 0,  0 },
		{"threads",     required_argument, 0,  0 },
		{"null",        required_argument, 0,  0 },
		{0,             0,                 0,  0 }
	};
	const char *short_options = "hf:o:t:0";

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
				opt = 't';
				break;
			case 4:
				opt = '0';
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
		case 't':
			if (sscanf(optarg, "%u", &opt_workers) != 1 ||
					opt_workers < 1) {
				ERR0("Number of workers expected to be >1");
				exit(1);
			}
			break;
		case '0':
			opt_input_format = null;
			break;
		default:
			exit(1);
			break;
		}
	}

	/* Check that we got all info needed */
	if (opt_output_path == NULL) {
		ERR0("Output path is a required argument");
		exit(1);
	}

	return optind;
}

int main(int argc, char *argv[]) {
	int i;
	char *filename;
	GAsyncQueue *buffer_queue;
	struct work *work_buffers;
	struct work *work;
	GError *gerror;
	GThreadPool *worker_pool;
	struct input_context input_context;

	parse_options(argc, argv);

	buffer_queue = g_async_queue_new();

	/* Prepare buffers, 10*workers ought to be enough for anyone */
	work_buffers = calloc(10 * opt_workers, sizeof(struct work));
	if (work_buffers == NULL) {
		perror("mcsign");
		exit(1);
	}
	for (i = 0; i < 10 * opt_workers; i++)
		g_async_queue_push(buffer_queue, &work_buffers[i]);

	/* Start workers */
	worker_pool = g_thread_pool_new(worker, buffer_queue, opt_workers,
			TRUE, &gerror);
	if (worker_pool == NULL) {
		ERR("Error while allocating pool: %s", gerror->message);
		exit(1);
	}

	init_input_context(&input_context);
	while (filename = get_input(&input_context)) {

		work = (struct work *)g_async_queue_pop(buffer_queue);
		work->filename = filename;
		
		if (!g_thread_pool_push(worker_pool, work, &gerror)) {
			ERR("Error while pushing work to pool: %s",
					gerror->message);
			exit(1);
		}

		DBG("main: pushed %d %d", rx, ry);

	}

	/* Kill workers */
	g_thread_pool_free(worker_pool, FALSE, TRUE); /* finish queue & wait
							 for completion */

	g_async_queue_unref(buffer_queue);
	/* All workers has exited, so we can safely free the work buffers */
	free(work_buffers);

	return 0;
}
