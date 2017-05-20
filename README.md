This is a helper program to clean unused files from a project source directory.
It's main purpose is for reducing the size of a Linux Kernel source tree.

lk-reducer is based on "cleanmysourcetree" by Jann Horn. Original idea by
Joshua J. Drake. This is represents the third imlpementation of this tool.

Motiviation
-----------
The Linux Kernel has grown terribly bloated over the years. It contains
hundreds of drivers and other code that are often not needed to build a single
kernel. This creates a great deal of noise -- especially when conducting
something like a source code audit. Wouldn't it be nice if we could get rid of
everything we don't need?

How it Works
------------
This program uses the Linux *inotify* subsystem to monitor for accesses and
modifications within a directory hierarchy. By watching which files are and are
not accessed during a successful build, we can determine which files are
unecessary and thus able to be removed.

Dependencies
------------
This code depends on the "uthash" library. You can install it via a package
if your distribution has one (uthash-dev on Ubuntu) or from the original
repository at https://github.com/troydhanson/uthash

Usage
-----
To use this program, start by invoking it with the path to the linux kernel
source you're planning to reduce.  If no target directory is given, it defaults
to the current directory.

```
$ lk-reducer /android/source/kernel/msm
$
```

After installing monitoring, it will spawn a fresh shell within the provided
directory. Build the kernel as you would normally. After you're done, clean up
the build and exit the sub-shell.

```
$ make marlin_defconfig
$ make
$ make mrproper
```

Exiting will transfer control back to the reducer, which will proceed to
generate a file containing all of the files and their statuses in
"lk-reducer.out". You can use this file to then do further manipulations to
the source tree. The possible statuses are **A**ccessed, **U**ntouched, or
**G**enerated.

You could delete all the unaccessed files and directories:

```
$ grep ^U lk-reducer.out | cut -c 3- | (while read F; do rm -vf "$F"; done)
```

You could copy all of the essential files and directories to a new location:

```
$ grep ^A lk-reducer.out | cut -c 3- > lk-reducer-keep.out
$ tar cf - -T lk-reducer-keep.out | tar xf - -C ../msm-reduced/
```

Authors
-------
This project is a collaboration between Joshua J. Drake (jduck) and Jann Horn (TheJH).
