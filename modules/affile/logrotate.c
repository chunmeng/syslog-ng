/*
 * Copyright (c) 2025 Alexander Zikulnig
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * As an additional exemption you are allowed to compile & link against the
 * OpenSSL libraries as published by the OpenSSL project. See the file
 * COPYING for details.
 *
 */

#include "logrotate.h"
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <zlib.h>
#include "messages.h"

// Add compression
#define COMPRESS_CHUNK 16384

static gint
logrotate_compress_file(FILE *source, FILE *dest)
{
  int ret, flush;
  unsigned have;
  z_stream strm;
  unsigned char in[COMPRESS_CHUNK];
  unsigned char out[COMPRESS_CHUNK];

  /* allocate deflate state */
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  ret = deflateInit2(&strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY);
  if (ret != Z_OK) {
    return ret;
  }

  /* compress until end of file */
  do {
    strm.avail_in = fread(in, 1, COMPRESS_CHUNK, source);
    if (ferror(source)) {
      (void)deflateEnd(&strm);
      return Z_ERRNO;
    }
    flush = feof(source) ? Z_FINISH : Z_NO_FLUSH;
    strm.next_in = in;
    /* run deflate() on input until output buffer not full, finish
       compression if all of source has been read in */
    do {
      strm.avail_out = COMPRESS_CHUNK;
      strm.next_out = out;

      ret = deflate(&strm, flush);    /* no bad return value */
      /* assert(ret != Z_STREAM_ERROR);  /* state not clobbered */

      have = COMPRESS_CHUNK - strm.avail_out;
      if (fwrite(out, 1, have, dest) != have || ferror(dest)) {
        (void)deflateEnd(&strm);
        return Z_ERRNO;
      }
    } while (strm.avail_out == 0);
    /* assert(strm.avail_in == 0);     /* all input will be used */

    /* done when last data in file processed */
  } while (flush != Z_FINISH);
  /* assert(ret == Z_STREAM_END);        /* stream will be complete */

  /* clean up and return */
  (void)deflateEnd(&strm);
  return Z_OK;
}

static gboolean
logrotate_compress(const char* srcFileName, const char* destFileName)
{
  FILE *source = NULL;
  FILE *dest = NULL;
  gboolean result = TRUE;
  int error = Z_OK;
  source = fopen(srcFileName,"rb");
  if (source == NULL) {
    msg_error("Error opening the source log file ",
              evt_tag_str("filename", srcFileName),
              evt_tag_errno(EVT_TAG_OSERROR, errno));
    return FALSE;
  }
  dest = fopen(destFileName, "wb");
  if (dest == NULL) {
    fclose(source);
    msg_error("Error creating the rotated log file ",
              evt_tag_str("filename", srcFileName),
              evt_tag_errno(EVT_TAG_OSERROR, errno));
    return FALSE;
  }
  error = logrotate_compress_file(source, dest);
  if (error != Z_OK) {
    result = FALSE;
  }
  fclose(source);
  fclose(dest);
  if (result == FALSE) {
    msg_error("Error compressing the rotated log file ",
              evt_tag_str("filename", destFileName),
              evt_tag_errno(EVT_TAG_OSERROR, error));
  }
  unlink(srcFileName);
  return result;
}

#define LR_DEFAULT_SIZE 1000000 // 1MB
#define LR_DEFAULT_ROTATIONS 1

void logrotate_options_defaults(LogRotateOptions *logrotate_options)
{
  logrotate_options->enable = FALSE;
  logrotate_options->size = LR_DEFAULT_SIZE;
  logrotate_options->max_rotations = LR_DEFAULT_ROTATIONS;
  logrotate_options->pending = FALSE;
  logrotate_options->compress = FALSE;
  logrotate_options->compress_format = NULL;
}

void logrotate_options_destroy(LogRotateOptions *logrotate_options)
{
  g_free(logrotate_options->compress_format);
}

void logrotate_set_compress(LogRotateOptions *logrotate_options, const gchar *format)
{
  logrotate_options->compress = TRUE;
  if (format == NULL || format[0] == '\0')
    {
      logrotate_options->compress_format = g_strdup("gz");
    }
  else
    {
      if (strcmp(format, "gz") != 0)
        {
          msg_warning("Unknown compression format, defaulting to gz", evt_tag_str("format", format));
          logrotate_options->compress_format = g_strdup("gz");
        }
      else
        {
          logrotate_options->compress_format = g_strdup(format);
        }
    }
}

gboolean logrotate_is_enabled(LogRotateOptions *logrotate_options)
{
  return logrotate_options && logrotate_options->enable;
}

gboolean logrotate_is_required(LogRotateOptions *logrotate_options, const gsize filesize)
{
  return (logrotate_options && filesize >= logrotate_options->size);
}

gboolean logrotate_is_pending(LogRotateOptions *logrotate_options)
{
  return logrotate_options && logrotate_options->pending;
}

gchar *get_log_file_name(const gchar *filename, gsize rotation_suffix, LogRotateOptions *options)
{
  if (rotation_suffix == 0)
    {
      return g_strdup_printf("%s", filename);
    }
  else
    {
      if (options->compress)
        return g_strdup_printf("%s.%ld.gz", filename, (long) rotation_suffix);
      else
        return g_strdup_printf("%s.%ld", filename, (long) rotation_suffix);
    }
}

gsize first_log_file_to_rotate(const gchar *filename, LogRotateOptions *options)
{
  gchar *log_filename;
  for (gsize i = 1; i <= options->max_rotations; i++)
    {
      log_filename = get_log_file_name(filename, i, options);
      gboolean log_file_exists = g_file_test(log_filename, G_FILE_TEST_EXISTS);
      g_free(log_filename);

      if (!log_file_exists)
        {
          return i - 1;
        }
    }

  return options->max_rotations;
}

gint rotate_log_file(const gchar *filename, gsize roation_suffix, LogRotateOptions *options)
{
  gchar *current_filename = get_log_file_name(filename, roation_suffix, options);
  gint res = 0;

  if (g_file_test(current_filename, G_FILE_TEST_EXISTS))
    {
      if (roation_suffix == options->max_rotations)
        {
          msg_debug("Deleting oldest log file",
                    evt_tag_str("filename", current_filename));
          res = remove(current_filename);

          if (res != 0)
            {
              msg_error("Error deleting log file during logrotate",
                        evt_tag_str("filename", current_filename),
                        evt_tag_errno("errno", errno));
            }
        }
      else
        {
          // if current file is the main log file, we compress it to .1.gz
          // if current file is .N.gz, we rename it to .(N+1).gz
          gchar *rotated_filename = get_log_file_name(filename, roation_suffix + 1, options);
          msg_info("Rotating log file",
                    evt_tag_str("filename", current_filename),
                    evt_tag_str("new_filename", rotated_filename));
          if (roation_suffix == 0) // 0 means the main log file
            {
              if (options->compress)
                {
                  if (logrotate_compress(current_filename, rotated_filename) == FALSE)
                    {
                      msg_error("Error compressing log file during logrotate",
                                evt_tag_str("filename", current_filename),
                                evt_tag_str("new_filename", rotated_filename),
                                evt_tag_errno("errno", errno));
                      res = -1;
                    }
                }
              else
                {
                   res = rename(current_filename, rotated_filename);
                }
            }
          else
            {
              res = rename(current_filename, rotated_filename);
            }
          g_free(rotated_filename);

          if (res != 0)
            {
              msg_error("Error renaming log file during logrotate",
                        evt_tag_str("filename", current_filename),
                        evt_tag_errno("errno", errno));
            }
        }
    }
  else
    {
      msg_error("Error rotating log file because it does not exist",
                evt_tag_str("filename", current_filename),
                evt_tag_errno("errno", errno));
      res = -1;
    }

  g_free(current_filename);

  return res;
}

/* TODO:
 * Do renaming in separate thread?
 * i.e. in writer thread delete oldest log file and create a new temp logfile --> filename_tmp
 * the writer only does this if not already a tmp file is created (otherwise the renaming thread is working)
 * checked is this condition via the gcond-lock waiting for the thread to finish.
 *
 * The renaming thread renames all other files including the tmp file to filename.1
 *
 */
LogRotateStatus logrotate_do_rotate(LogRotateOptions *logrotate_options, const gchar *filename)
{
  if (logrotate_options == NULL || filename == NULL)
    {
      return LR_ERROR;
    }

  msg_info("Maximum log file size reached, rotating log file ... ",
           evt_tag_str("filename", filename));

  // (1) check if main log file exists
  if (!g_file_test(filename, G_FILE_TEST_EXISTS))
    {
      msg_warning("WARNING: Logrotate could not find the main log file",
                  evt_tag_str("filename", filename),
                  evt_tag_errno("errno", errno));
      return LR_SUCCESS;
    }

  // (2) check for first rotated log file which does not exist
  gsize max_rotation_suffix = first_log_file_to_rotate(filename, logrotate_options);

  // (3) rename existing log files up to max_rotation_suffix
  for (gint i = max_rotation_suffix; i >= 0; i--)
    {
      if (rotate_log_file(filename, i, logrotate_options) != 0)
        {
          return LR_ERROR;
        }
    }

  // (4) report the log file to reopened
  return LR_SUCCESS;
}
