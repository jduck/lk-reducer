This is a helper program to clean unused files from a project source directory.
It's main purpose is for reducing the size of a Linux Kernel source tree.

lk-reducer is based on "cleanmysourcetree" by Jann Horn. Original idea by
Joshua J. Drake. This is a modified version of the second implementation of
this tool. You can find the unmodified second implementation at
https://git.thejh.net/?p=cleanmysourcetree.git

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

```console
dev:0:~$ lk-reducer /android/source/kernel/msm
dev:0:msm$
```

After installing monitoring, it will spawn a fresh shell within the provided
directory. Build the kernel as you would normally. After you're done, clean up
the build and exit the sub-shell.

```console
dev:0:msm$ make marlin_defconfig
dev:0:msm$ make -j$(NPROCS)
dev:0:msm$ make mrproper
```

Exiting will transfer control back to the reducer, which will proceed to
generate a file containing all of the files and their statuses in
"lk-reducer.out". You can use this file to then do further manipulations to
the source tree. The possible statuses are **A**ccessed, **U**ntouched, or
**G**enerated.

You could delete all the unaccessed files and directories:

```console
dev:0:msm$ grep ^U lk-reducer.out | cut -c 3- | (while read F; do rm -vf "$F"; done)
```

You could copy all of the essential files and directories to a new location:

```console
dev:0:msm$ grep ^A lk-reducer.out | cut -c 3- > lk-reducer-keep.out
dev:0:msm$ cpio -pvdm ../msm-reduced/ < lk-reducer-keep.out
dev:0:msm$ # Or, use tar if cpio isn't installed.
dev:0:msm$ tar cf - -T lk-reducer-keep.out | tar xf - -C ../msm-reduced/
```

Sample Use
----------
This is what it looked like when we used it in practice:

```console
dev:0:msm$ du -hs --exclude .git .
750M    .
dev:0:msm$ lk-reducer
dropping you into an interactive shell now. compile the project, then exit the shell.
dev:0:msm$ make marlin_defconfig
  HOSTCC  scripts/basic/fixdep
  HOSTCC  scripts/kconfig/conf.o
  SHIPPED scripts/kconfig/zconf.tab.c
  SHIPPED scripts/kconfig/zconf.lex.c
  SHIPPED scripts/kconfig/zconf.hash.c
  HOSTCC  scripts/kconfig/zconf.tab.o
  HOSTLD  scripts/kconfig/conf
drivers/soc/qcom/Kconfig:381:warning: choice value used outside its choice group
drivers/soc/qcom/Kconfig:386:warning: choice value used outside its choice group
#
# configuration written to .config
#
dev:0:msm$ make -j30 > ../msm-marlin-make.out 2>&1
dev:0:msm$ make mrproper
  CLEAN   .
  CLEAN   arch/arm64/kernel/vdso
  CLEAN   arch/arm64/kernel
  CLEAN   crypto/asymmetric_keys
  CLEAN   kernel/time
  CLEAN   kernel
  CLEAN   lib
  CLEAN   net/wireless
  CLEAN   security/selinux
  CLEAN   usr
  CLEAN   arch/arm64/boot/dts/htc
  CLEAN   arch/arm64/boot
  CLEAN   .tmp_versions
  CLEAN   scripts/basic
  CLEAN   scripts/dtc
  CLEAN   scripts/kconfig
  CLEAN   scripts/mod
  CLEAN   scripts/selinux/genheaders
  CLEAN   scripts/selinux/mdp
  CLEAN   scripts
  CLEAN   include/config include/generated arch/arm64/include/generated
  CLEAN   .config .version include/generated/uapi/linux/version.h Module.symvers
dev:0:msm$ exit
exit
processing remaining events...
inotify event collection phase is over, dumping results to "lk-reducer.out"...
cleanup complete
dev:0:msm$ grep -h ^A lk-reducer.out | sort -u | cut -c 3- | grep -v '\./\.git\/' > lk-reducer-keep.out
dev:0:msm$ mkdir ../msm-marlin-reduced
dev:0:msm$ tar cf - -T lk-reducer-keep.out | tar xf - -C ../msm-marlin-reduced/
dev:0:msm$ du -hs ../msm-marlin-reduced/
132M    ../msm-marlin-reduced/
```

750M down to 132M!! That's a lot less noise!

Authors
-------
This project is a collaboration between Joshua J. Drake ([@jduck](https://github.com/jduck/)) and Jann Horn ([@TheJH](https://github.com/thejh/)).
