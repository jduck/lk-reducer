#define _GNU_SOURCE

#include <dirent.h>
#include <unistd.h>
#include <stdio.h>
#include <err.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>
#include <poll.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/inotify.h>
#include <sys/wait.h>
#include <sys/prctl.h>
#include <uthash.h>

enum filestate {
  FSTATE_ACCESSED,
  FSTATE_UNTOUCHED,
  FSTATE_GENERATED
};

// describes a file or a directory that was created during compilation
typedef struct {
  UT_hash_handle hh;
  enum filestate state;
  char name[]; /* path relative to project root; hashmap key */
} file;
file *hashed_files = NULL;

// describes a directory that existed before compilation
typedef struct {
  UT_hash_handle hh;
  int watch_descriptor; /* hashmap key */
  char name[];
} directory;
directory *hashed_dirs = NULL;

int inotify_fd = -1;
unsigned long watch_cnt = 0;

void *xmalloc(size_t len) {
  void *ret = malloc(len);
  if (ret == NULL && len != 0)
    err(1, "memory allocation failure");
  return ret;
}

static void add_file(const char *file_path) {
  file *f;
  HASH_FIND_STR(hashed_files, file_path, f);
  if (f)
    errx(1, "we tried to add the file '%s' twice, probably because you're messing "
      "around in the filesystem and removing and adding files during the initial scan "
      "step. stop it.", file_path);

  f = xmalloc(sizeof(*f) + strlen(file_path) + 1);
  strcpy(f->name, file_path);
  f->state = FSTATE_UNTOUCHED;
  HASH_ADD_STR(hashed_files, name, f);
}

static void add_dir(const char *dir_path) {
  directory *d = xmalloc(sizeof(*d) + strlen(dir_path) + 1);
  strcpy(d->name, dir_path);

  // Don't register watches on files to avoid running into the fs.inotify.max_user_watches limit.
  // Instead, register watches on all directories. There shouldn't be too many of those.
  d->watch_descriptor = inotify_add_watch(inotify_fd, dir_path,
    IN_OPEN | IN_CREATE | IN_MOVED_TO | IN_EXCL_UNLINK | IN_ONLYDIR);
  if (d->watch_descriptor == -1) {
    if (errno == ENOSPC) {
      // inotify returns ENOSPC when max user watches is reached. Help the user...
      fprintf(stderr, "WARNING: You ran out of inotify watches. Did you check the README? Try: sudo sysctl fs.inotify.max_user_watches=16384\n");
    }
    err(1, "unable to add inotify watch for '%s' (after %lu)", dir_path, watch_cnt);
  }
  watch_cnt++;

  directory *d_existing;
  HASH_FIND_INT(hashed_dirs, &d->watch_descriptor, d_existing);
  if (d_existing)
    errx(1, "the kernel says we watched the same directory twice. whatever you're doing "
      "to make the filesystem behave that way, stop it.");
  HASH_ADD_INT(hashed_dirs, watch_descriptor, d);
}

static void add_files_recursive(const char *current_path) {
  DIR *d = opendir(current_path);
  if (d == NULL)
    err(1, "unable to open directory '%s'", current_path);

  struct dirent *entry;
  while (1) {
    errno = 0;
    if (!(entry = readdir(d))) {
      if (errno != 0)
        errx(1, "readdir failed");
      break;
    }
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;

    char file_path[strlen(current_path) + 1 + strlen(entry->d_name) + 1];
    sprintf(file_path, "%s/%s", current_path, entry->d_name);

    struct stat st;
    if (lstat(file_path, &st))
      err(1, "unable to stat '%s'", file_path);
    switch (st.st_mode & S_IFMT) {
      case S_IFREG:
        add_file(file_path);
        break;
      case S_IFDIR:
        // First recurse, then add the watch. This avoids spamming ourselves with irrelevant
        // directory open events.
        add_files_recursive(file_path);
        break;
      default:
        break;
    }
  }
  add_dir(current_path);
  closedir(d);
}

volatile bool child_quit = false;

void handle_sigchld(int n) {
  if (waitpid(-1, NULL, WNOHANG) == -1)
    err(1, "wait for child failed");
  child_quit = true;
  puts("processing remaining events...");
}

void usage(void) {
  puts("invocation: lk-reducer [<target directory>]");
  exit(1);
}

int main(int argc, char **argv) {
  if (argc == 2) {
    if (strcmp(argv[1], "-h") == 0)
      usage();
    if (chdir(argv[1]))
      err(1, "unable to chdir to specified directory");
  } else if (argc < 1 || argc > 2) {
    usage();
  }

  // collect filenames, init watches
  inotify_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
  if (inotify_fd == -1)
    err(1, "unable to open inotify fd");
  add_files_recursive(".");

  sigset_t sigchild_mask;
  sigemptyset(&sigchild_mask);
  sigaddset(&sigchild_mask, SIGCHLD);
  if (sigprocmask(SIG_SETMASK, &sigchild_mask, NULL))
    err(1, "sigprocmask failed");
  if (signal(SIGCHLD, handle_sigchld) == SIG_ERR)
    err(1, "unable to register signal handler");

  pid_t child = fork();
  if (child == -1)
    err(1, "can't fork");
  if (child == 0) {
    prctl(PR_SET_PDEATHSIG, SIGTERM); /* stupid racy API :/ */
    puts("dropping you into an interactive shell now. compile the project, then "
      "exit the shell.");
    // uhhh... yeah.
    if (system("$SHELL") == -1)
    err(1, "failed to execute a sub-shell");
    exit(0);
  }

  // process inotify events until the child is dead
  // and there are no more pending events
  struct pollfd fds[1] = { { .fd = inotify_fd, .events = POLLIN } };
  struct timespec zero_timeout_ts = { .tv_sec = 0, .tv_nsec = 0};
  sigset_t empty_mask;
  sigemptyset(&empty_mask);
  while (1) {
    int r = ppoll(fds, 1, child_quit ? &zero_timeout_ts : NULL, &empty_mask);
    if (r == -1 && errno == EINTR)
      continue;
    if (r == -1)
      err(1, "ppoll failed");
    if (r == 0)
      break;

    char buf[20 * (sizeof(struct inotify_event) + NAME_MAX + 1)];
    ssize_t read_res = read(inotify_fd, buf, sizeof(buf));
    if (read_res == -1)
      err(1, "read from inotify fd failed");
    if (read_res < sizeof(struct inotify_event))
      errx(1, "short/empty read from inotify");
    struct inotify_event *e = (void*)buf;
    while ((char*)e != buf + read_res) {
      if (e->mask & IN_Q_OVERFLOW)
        errx(1, "inotify queue overflow detected");
      if (e->len != 0 && (e->mask & (IN_OPEN | IN_CREATE | IN_MOVED_TO))) {
        directory *dir;
        HASH_FIND_INT(hashed_dirs, &e->wd, dir);
        if (dir == NULL)
          errx(1, "unable to find dir by inotify wd, bug!");
        char path[strlen(dir->name) + e->len + 2];
        sprintf(path, "%s/%s", dir->name, e->name);
        file *f;
        HASH_FIND_STR(hashed_files, path, f);
        // if f is NULL, this is a file/directory generated during compilation
        // or a pre-existing directory
        if (f != NULL) {
          if (f->state == FSTATE_UNTOUCHED)
            f->state = FSTATE_ACCESSED;
        } else if (e->mask & (IN_CREATE | IN_MOVED_TO)) {
          f = xmalloc(sizeof(*f) + strlen(path) + 1);
          strcpy(f->name, path);
          f->state = FSTATE_GENERATED;
          HASH_ADD_STR(hashed_files, name, f);
        }
      }
      // step to next event in buffer. yuck.
      e = (struct inotify_event *)((char *)e + sizeof(struct inotify_event) + e->len);
    }
  }

  // reset signal handling
  signal(SIGCHLD, SIG_DFL);
  if (sigprocmask(SIG_SETMASK, &empty_mask, NULL))
    err(1, "sigprocmask failed");

  close(inotify_fd);
  puts("inotify event collection phase is over, dumping results to \"lk-reducer.out\"...");

  // output all of the files with their state
  FILE *of = fopen("lk-reducer.out", "w");
  for (file *f = hashed_files; f != NULL; f = f->hh.next) {
    if (f->state == FSTATE_ACCESSED)
      fprintf(of, "A %s\n", f->name);
    else if (f->state == FSTATE_GENERATED)
      fprintf(of, "G %s\n", f->name);
    else if (f->state == FSTATE_UNTOUCHED)
      fprintf(of, "U %s\n", f->name);
    else
      fprintf(of, "? %s\n", f->name);
  }
  fclose(of);
  puts("cleanup complete");
  return 0;
}
