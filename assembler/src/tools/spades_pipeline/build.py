#!/usr/bin/env python
import os
import support
import shutil
import fcntl
from os import path


def build_spades(dir):
    if not path.exists(path.join(dir, "Makefile")):
        support.sys_call('cmake src', dir)

    support.sys_call('make debruijn', dir)

def syncFiles(src, dest):
    if path.isfile(src):
        if not path.exists(dest):
            shutil.copy2(src, dest)
        elif str(path.getmtime(src)) != str(path.getmtime(dest)):
            shutil.copy2(src, dest)
    else:
        if not path.exists(dest):
            os.makedirs(dest)
        for file in os.listdir(dest):
            if not file == "k.hpp":
                destFile = path.join(dest, file)
                if path.isfile(destFile):
                    if not path.exists(path.join(src, file)):
                        os.unlink(destFile)

        for file in os.listdir(src):
            syncFiles(path.join(src, file), path.join(dest, file))

def build_k(spades_folder, str_k, spades_home):

    build_folder_k = path.join(spades_folder, "build" + str_k)

    syncFiles(path.join(spades_home, "src"), path.join(build_folder_k, "src"))
    syncFiles(path.join(spades_home, "ext"), path.join(build_folder_k, "ext"))

    kFile = path.join(build_folder_k, "src/debruijn/k.hpp")
    if not path.exists(kFile):
        fo = open(kFile, "w")
        fo.write("#pragma once\n\n")
        fo.write("namespace debruijn_graph {\n")
        fo.write("  const size_t K = " + str_k + ";\n")
        fo.write("}\n")
        fo.close()

    print("\n== Compiling with K=" + str_k + " ==\n")
    build_spades(build_folder_k)

def build_spades_n_copy(cfg, spades_home):

    precompiled_folder = path.join(os.getenv('HOME'), '.spades/precompiled/')

    print("\n== Compilation started ==\n")

    for K in cfg.iterative_K:

        if not path.exists(precompiled_folder) :
            os.makedirs(precompiled_folder)

        str_k = str(K)

        lockFlag = path.join(precompiled_folder, "lock") + str_k

        fo = open(lockFlag, "w")
        fcntl.lockf(fo, fcntl.LOCK_EX)
        try :
            build_k(precompiled_folder, str_k, spades_home)
        finally :
            fcntl.lockf(fo, fcntl.LOCK_UN)

    print("\n== Compilation finished successfully ==\n")