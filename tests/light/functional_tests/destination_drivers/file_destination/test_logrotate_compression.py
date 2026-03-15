#!/usr/bin/env python
#############################################################################
# Copyright (c) 2026 HENG Chun Meng
#
# This program is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License version 2 as published
# by the Free Software Foundation, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
#
# As an additional exemption you are allowed to compile & link against the
# OpenSSL libraries as published by the OpenSSL project. See the file
# COPYING for details.
#
#############################################################################
import glob
import math
import os
import gzip
from time import sleep


def test_logrotate_compression(config, syslog_ng):
    file_name = "logfile-compression.log"
    max_size = 10000
    max_rotations = 2

    # Calculate messages needed to trigger rotation
    message = "message-text"
    msg_len = len(message.encode('utf-8')) + 1 # +1 for newline
    msg_per_file = math.floor(max_size / msg_len)
    # Generate enough messages to fill current file + 2 rotations
    counter = msg_per_file * (max_rotations + 1)

    generator_source = config.create_example_msg_generator_source(num=counter, freq=0.0001, template=config.stringify(message))

    # Enable compression with gz format
    logrotate_options = "enable(yes), rotations(" + str(max_rotations) + "), size(" + str(max_size) + "), compress(gz)"

    file_destination = config.create_file_destination(file_name=file_name, template="\"${MSG}\\n\"", logrotate=logrotate_options)

    config.create_logpath(statements=[generator_source, file_destination])
    syslog_ng.start(config)

    # wait until all messages have been written
    sleep(5)

    # verify that rotated logfiles have .gz extension
    # We expect: file_name (current), file_name.1.gz, file_name.2.gz

    assert os.path.exists(file_name)
    assert os.path.exists(file_name + ".1.gz")
    assert os.path.exists(file_name + ".2.gz")

    # Verify that .1.gz is a valid gzip file
    try:
        with gzip.open(file_name + ".1.gz", 'rb') as f:
            content = f.read()
            assert len(content) > 0
    except Exception as e:
        assert False, f"Failed to decompress log file: {e}"

    # Verify that .2.gz is a valid gzip file
    try:
        with gzip.open(file_name + ".2.gz", 'rb') as f:
            content = f.read()
            assert len(content) > 0
    except Exception as e:
        assert False, f"Failed to decompress log file: {e}"

