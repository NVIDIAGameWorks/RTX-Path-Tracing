#!/usr/bin/python
#
# Copyright (c) 2014-2021, NVIDIA CORPORATION. All rights reserved.
#
# Permission is hereby granted, free of charge, to any person obtaining a
# copy of this software and associated documentation files (the "Software"),
# to deal in the Software without restriction, including without limitation
# the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the
# Software is furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in
# all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
# THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
# FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
# DEALINGS IN THE SOFTWARE.


import tarfile
import os
import lz4.frame
import argparse
import sys
import io

parser = argparse.ArgumentParser(description = "Tar/LZ4 packaging tool", fromfile_prefix_chars='@')
parser.add_argument('inputs', nargs = '*')
parser.add_argument('--output', '-o', required = True, help = "Output file name")
parser.add_argument('--compress', '-c', default = 0, type = int, help = "LZ4 compression level, 0 = uncompressed")
parser.add_argument('--prefix', '-p', default = '', help="Path prefix for archive files")
parser.add_argument('--no-compress', '-n', action = 'append', default = [], help="File types to skip compression for")


args = parser.parse_args()

original_size = 0
compressed_size = 0

def normalize_path(path):
    path = os.path.normpath(path)
    if args.prefix:
        path = os.path.join(args.prefix, path)
    path = path.replace('\\', '/')
    return path

def process_file(path, tar):
    global original_size, compressed_size

    archive_path = normalize_path(path)
    print(archive_path)
    
    try:
        with open(path, 'rb') as file:
            contents = file.read()
    except:
        print("ERROR: Cannot read file: %s" % path)
        sys.exit(1)

    original_size += len(contents)

    extension = os.path.splitext(path)[1]

    if args.compress and (extension not in args.no_compress):
        contents = lz4.frame.compress(contents, compression_level = args.compress, store_size = True, return_bytearray = True)
        archive_path += '.lz4'

    compressed_size += len(contents)

    tarinfo = tarfile.TarInfo(archive_path)
    tarinfo.size = len(contents)
    tar.addfile(tarinfo, io.BytesIO(contents))
    
with tarfile.open(args.output, mode = 'w', format = tarfile.USTAR_FORMAT) as tar:
    for input_name in args.inputs:
        if os.path.isdir(input_name):
            # if the line references a directory, recursively collect everything from that directory
            for dirpath, dirnames, filenames in os.walk(input_name):
                for file_name in filenames:
                    path = os.path.join(dirpath, file_name)
                    process_file(path, tar)
        else:
            # just take one file
            process_file(input_name, tar)

if args.compress:
    print("Original size: {0:,} bytes, compressed size: {1:,} bytes (ratio = {2:.2f}x)"
        .format(original_size, compressed_size, float(original_size) / float(compressed_size)))