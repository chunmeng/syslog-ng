/*
 * Copyright (c) 2026 HENG Chun Meng
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

#include <criterion/criterion.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include "logrotate.h"
#include "apphook.h"
#include "reloc.h"

#define TEST_FILENAME "test_logrotate.log"

static void
setup(void)
{
  override_installation_path_for("${pkgdatadir}/smart-multi-line.fsm", TOP_SRCDIR "/lib/multi-line/smart-multi-line.fsm");
  app_startup();
}

static void
teardown(void)
{
  app_shutdown();
}

TestSuite(logrotate, .init = setup, .fini = teardown);

Test(logrotate, options_defaults)
{
  LogRotateOptions options;
  logrotate_options_defaults(&options);

  cr_assert_not(options.enable);
  cr_assert_eq(options.size, 1000000);
  cr_assert_eq(options.max_rotations, 1);
  cr_assert_not(options.pending);
  cr_assert_not(options.compress);
  cr_assert_null(options.compress_format);

  logrotate_options_destroy(&options);
}

Test(logrotate, is_enabled)
{
  LogRotateOptions options;
  logrotate_options_defaults(&options);

  cr_assert_not(logrotate_is_enabled(&options));
  options.enable = TRUE;
  cr_assert(logrotate_is_enabled(&options));

  logrotate_options_destroy(&options);
}

Test(logrotate, is_required)
{
  LogRotateOptions options;
  logrotate_options_defaults(&options);
  options.size = 100;

  cr_assert_not(logrotate_is_required(&options, 50));
  cr_assert(logrotate_is_required(&options, 100));
  cr_assert(logrotate_is_required(&options, 150));

  logrotate_options_destroy(&options);
}

Test(logrotate, set_compress)
{
  LogRotateOptions options;
  logrotate_options_defaults(&options);

  logrotate_set_compress(&options, "gz");
  cr_assert(options.compress);
  cr_assert_str_eq(options.compress_format, "gz");
  logrotate_options_destroy(&options);

  logrotate_options_defaults(&options);
  logrotate_set_compress(&options, "invalid");
  cr_assert(options.compress);
  cr_assert_str_eq(options.compress_format, "gz");
  logrotate_options_destroy(&options);

#if SYSLOG_NG_HAVE_LIBZIP
  logrotate_options_defaults(&options);
  logrotate_set_compress(&options, "zip");
  cr_assert(options.compress);
  cr_assert_str_eq(options.compress_format, "zip");
  logrotate_options_destroy(&options);
#endif
}

static void
create_test_file(const gchar *filename, const gchar *content)
{
  GError *error = NULL;
  gboolean res = g_file_set_contents(filename, content, -1, &error);
  cr_assert(res, "Failed to create test file %s: %s", filename, error ? error->message : "Unknown error");
}

Test(logrotate, basic_rotation)
{
  LogRotateOptions options;
  logrotate_options_defaults(&options);
  options.enable = TRUE;
  options.max_rotations = 2;
  options.size = 10;

  create_test_file(TEST_FILENAME, "test content");

  LogRotateStatus status = logrotate_do_rotate(&options, TEST_FILENAME);
  cr_assert_eq(status, LR_SUCCESS);

  cr_assert_not(g_file_test(TEST_FILENAME, G_FILE_TEST_EXISTS));
  cr_assert(g_file_test("test_logrotate.log.1", G_FILE_TEST_EXISTS));

  // Rotate again
  create_test_file(TEST_FILENAME, "new content");
  status = logrotate_do_rotate(&options, TEST_FILENAME);
  cr_assert_eq(status, LR_SUCCESS);

  cr_assert_not(g_file_test(TEST_FILENAME, G_FILE_TEST_EXISTS));
  cr_assert(g_file_test("test_logrotate.log.1", G_FILE_TEST_EXISTS));
  cr_assert(g_file_test("test_logrotate.log.2", G_FILE_TEST_EXISTS));

  // Rotate third time, .2 should be deleted
  create_test_file(TEST_FILENAME, "third content");
  status = logrotate_do_rotate(&options, TEST_FILENAME);
  cr_assert_eq(status, LR_SUCCESS);

  cr_assert_not(g_file_test(TEST_FILENAME, G_FILE_TEST_EXISTS));
  cr_assert(g_file_test("test_logrotate.log.1", G_FILE_TEST_EXISTS));
  cr_assert(g_file_test("test_logrotate.log.2", G_FILE_TEST_EXISTS));

  gchar *content = NULL;
  g_file_get_contents("test_logrotate.log.1", &content, NULL, NULL);
  cr_assert_str_eq(content, "third content");
  g_free(content);

  g_file_get_contents("test_logrotate.log.2", &content, NULL, NULL);
  cr_assert_str_eq(content, "new content");
  g_free(content);

  cr_assert_not(g_file_test("test_logrotate.log.3", G_FILE_TEST_EXISTS));

  logrotate_options_destroy(&options);
}

Test(logrotate, rotation_with_compression)
{
  LogRotateOptions options;
  logrotate_options_defaults(&options);
  options.enable = TRUE;
  options.max_rotations = 2;
  options.size = 10;
  logrotate_set_compress(&options, "gz");

  create_test_file(TEST_FILENAME, "compressible content that should be large enough to see some compression effect maybe");

  LogRotateStatus status = logrotate_do_rotate(&options, TEST_FILENAME);
  cr_assert_eq(status, LR_SUCCESS);

  cr_assert_not(g_file_test(TEST_FILENAME, G_FILE_TEST_EXISTS));
  cr_assert(g_file_test("test_logrotate.log.1.gz", G_FILE_TEST_EXISTS));

  // Rotate again
  create_test_file(TEST_FILENAME, "more content");
  status = logrotate_do_rotate(&options, TEST_FILENAME);
  cr_assert_eq(status, LR_SUCCESS);

  cr_assert_not(g_file_test(TEST_FILENAME, G_FILE_TEST_EXISTS));
  cr_assert(g_file_test("test_logrotate.log.1.gz", G_FILE_TEST_EXISTS));
  cr_assert(g_file_test("test_logrotate.log.2.gz", G_FILE_TEST_EXISTS));
  cr_assert_not(g_file_test("test_logrotate.log.3.gz", G_FILE_TEST_EXISTS));

  logrotate_options_destroy(&options);
}
