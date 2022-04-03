//#ifdef WIN32
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
#define WINDOWS
#define realpath(N,R) _fullpath((R),(N), _MAX_PATH)
#undef ERROR
#undef NO_ERROR
#undef DELETE
#undef s_host

#endif // WIN32

#ifndef PLATFORM_HPP
#define PLATFORM_HPP

#ifdef WIN32

#define PATH_MAX 1000

#define S_IRWXU 0
#define S_IRWXG 0
#define S_IROTH 0
#define S_IXOTH 0
#define S_ISDIR(mode) 0
#define S_ISREG(mode) 1
#define S_ISCHR(mode) 1
#define S_ISBLK(mode) 1
#define S_ISFIFO(mode) 1
#define S_ISLNK(mode) 1
#define S_ISSOCK(mode) 1

int chdir(const char *pathname);
int mkdir(const char *pathname, int mode);
int unlink(const char *pathname);
char* realpath(const char *pathname, char *result);

typedef int pid_t;

struct stat {
  int     st_dev;
  int     st_ino;
  int    st_mode;
  int   st_nlink;
  int     st_uid;
  int     st_gid;
  int     st_rdev;
  size_t     st_size;
  int st_blksize;
  int  st_blocks;
  int st_atime;
  int st_mtime;
  int st_ctime;
};

int stat(const char *pathname, struct stat *st);

#define DT_DIR 0

struct DIR;

struct dirent {
  unsigned char d_type;
  char d_name[256];
};

DIR* opendir(const char *pathname);
int closedir(DIR *dirp);
struct dirent* readdir(DIR *dirp);

#define SIGHUP 1
#define SIGTSTP 3
#define SIGQUIT 3
#define SIGTRAP 3
#define SIGKILL 3
#define SIGBUS 3
#define SIGSYS 3
#define SIGPIPE 3
#define SIGALRM 3
#define SIGURG 3
#define SIGSTOP 3
#define SIGCONT 3
#define SIGCHLD 3
#define SIGTTIN 3
#define SIGTTOU 3
#define SIGIO 3
#define SIGXCPU 3
#define SIGXFSZ 3
#define SIGVTALRM 3
#define SIGPROF 3
#define SIGWINCH 3

#else

#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>

#endif // WIN32

#endif // PLATFORM_HPP
