#define FUSE_USE_VERSION 31

#include <fuse3/fuse.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <limits.h>

struct mini_unionfs_state {
    char *lower_dir;
    char *upper_dir;
};

#define STATE ((struct mini_unionfs_state *) fuse_get_context()->private_data)

/* ---------- helpers ---------- */
static void make_path(char *out, const char *base, const char *rel) {
    snprintf(out, PATH_MAX, "%s%s", base, rel);
}

static int is_whiteout(const char *name) {
    return strncmp(name, ".wh.", 4) == 0;
}

static int check_whiteout(const char *path) {
    char wh[PATH_MAX];
    char *base = strrchr(path, '/');
    base = base ? base + 1 : (char*)path;
    snprintf(wh, PATH_MAX, "%s/.wh.%s", STATE->upper_dir, base);
    return access(wh, F_OK) == 0;
}

/* ---------- COPY-ON-WRITE ---------- */
static void ensure_upper_copy(const char *path) {
    char up[PATH_MAX], low[PATH_MAX];
    make_path(up, STATE->upper_dir, path);
    make_path(low, STATE->lower_dir, path);

    struct stat st;

    if (lstat(up, &st) == 0) return;
    if (lstat(low, &st) != 0) return;

    stat(low, &st);

    int src = open(low, O_RDONLY);
    int dst = open(up, O_WRONLY | O_CREAT | O_TRUNC, st.st_mode);

    char buf[4096];
    int n;
    while ((n = read(src, buf, sizeof(buf))) > 0)
        write(dst, buf, n);

    close(src);
    close(dst);
}

/* ---------- resolve ---------- */
static int resolve_path(const char *path, char *resolved) {
    if (check_whiteout(path)) return -ENOENT;

    char up[PATH_MAX], low[PATH_MAX];
    make_path(up, STATE->upper_dir, path);
    make_path(low, STATE->lower_dir, path);

    struct stat st;

    if (lstat(up, &st) == 0) {
        strcpy(resolved, up);
        return 0;
    }
    if (lstat(low, &st) == 0) {
        strcpy(resolved, low);
        return 1;
    }
    return -ENOENT;
}

/* ---------- getattr ---------- */
static int unionfs_getattr(const char *path, struct stat *stbuf,
                          struct fuse_file_info *fi) {
    (void) fi;
    char resolved[PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res < 0) return res;
    return lstat(resolved, stbuf);
}

/* ---------- readdir ---------- */
static int unionfs_readdir(const char *path, void *buf,
                           fuse_fill_dir_t filler,
                           off_t offset,
                           struct fuse_file_info *fi,
                           enum fuse_readdir_flags flags) {

    (void) offset; (void) fi; (void) flags;

    char up[PATH_MAX], low[PATH_MAX];
    make_path(up, STATE->upper_dir, path);
    make_path(low, STATE->lower_dir, path);

    filler(buf, ".", NULL, 0, 0);
    filler(buf, "..", NULL, 0, 0);

    char seen[1024][256];
    int count = 0;

    DIR *d = opendir(up);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name,".") || !strcmp(de->d_name,"..")) continue;
            if (is_whiteout(de->d_name)) continue;
            strcpy(seen[count++], de->d_name);
            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(d);
    }

    d = opendir(low);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) {
            if (!strcmp(de->d_name,".") || !strcmp(de->d_name,"..")) continue;

            int skip = 0;
            for (int i=0;i<count;i++)
                if (!strcmp(seen[i], de->d_name)) skip=1;

            char vpath[PATH_MAX];
            snprintf(vpath, PATH_MAX, "/%s", de->d_name);

            if (skip || check_whiteout(vpath)) continue;

            filler(buf, de->d_name, NULL, 0, 0);
        }
        closedir(d);
    }

    return 0;
}

/* ---------- open ---------- */
static int unionfs_open(const char *path, struct fuse_file_info *fi) {

    if ((fi->flags & O_ACCMODE) != O_RDONLY || (fi->flags & O_TRUNC)) {
        ensure_upper_copy(path);

        // ✅ FIX: handle truncate correctly
        if (fi->flags & O_TRUNC) {
            char up[PATH_MAX];
            make_path(up, STATE->upper_dir, path);
            truncate(up, 0);
        }
    }

    return 0;
}

/* ---------- read ---------- */
static int unionfs_read(const char *path, char *buf, size_t size,
                        off_t offset, struct fuse_file_info *fi) {
    (void) fi;

    char resolved[PATH_MAX];
    int res = resolve_path(path, resolved);
    if (res < 0) return res;

    int fd = open(resolved, O_RDONLY);
    int ret = pread(fd, buf, size, offset);
    close(fd);
    return ret;
}

/* ---------- write ---------- */
static int unionfs_write(const char *path, const char *buf,
                         size_t size, off_t offset,
                         struct fuse_file_info *fi) {

    ensure_upper_copy(path);

    char up[PATH_MAX];
    make_path(up, STATE->upper_dir, path);

    int fd = open(up, O_WRONLY | O_CREAT, 0644);
    int ret = pwrite(fd, buf, size, offset);
    close(fd);
    return ret;
}

/* ---------- truncate ---------- */
static int unionfs_truncate(const char *path, off_t size,
                            struct fuse_file_info *fi) {

    ensure_upper_copy(path);

    char up[PATH_MAX];
    make_path(up, STATE->upper_dir, path);

    return truncate(up, size);
}

/* ---------- unlink ---------- */
static int unionfs_unlink(const char *path) {

    char up[PATH_MAX], low[PATH_MAX], wh[PATH_MAX];
    make_path(up, STATE->upper_dir, path);
    make_path(low, STATE->lower_dir, path);

    struct stat st;

    if (lstat(up, &st) == 0)
        unlink(up);

    if (lstat(low, &st) == 0) {
        char *base = strrchr(path, '/');
        base = base ? base + 1 : (char*)path;

        snprintf(wh, PATH_MAX, "%s/.wh.%s", STATE->upper_dir, base);

        int fd = open(wh, O_CREAT | O_WRONLY, 0000);
        if (fd >= 0) close(fd);
    }

    return 0;
}

/* ---------- create ---------- */
static int unionfs_create(const char *path, mode_t mode,
                          struct fuse_file_info *fi) {
    char up[PATH_MAX];
    make_path(up, STATE->upper_dir, path);
    int fd = open(up, O_CREAT | O_WRONLY, mode);
    close(fd);
    return 0;
}

/* ---------- mkdir ---------- */
static int unionfs_mkdir(const char *path, mode_t mode) {
    char up[PATH_MAX];
    make_path(up, STATE->upper_dir, path);
    if (mkdir(up, mode) == -1) return -errno;
    return 0;
}

/* ---------- rmdir ---------- */
static int unionfs_rmdir(const char *path) {
    char up[PATH_MAX], low[PATH_MAX], wh[PATH_MAX];
    make_path(up, STATE->upper_dir, path);
    make_path(low, STATE->lower_dir, path);

    struct stat st;
    // If it exists in upper, try to remove it
    if (lstat(up, &st) == 0) {
        if (rmdir(up) == -1) return -errno;
    }

    // If it exists in lower, create a whiteout marker
    if (lstat(low, &st) == 0) {
        char *base = strrchr(path, '/');
        base = base ? base + 1 : (char*)path;
        snprintf(wh, PATH_MAX, "%s/.wh.%s", STATE->upper_dir, base);
        int fd = open(wh, O_CREAT | O_WRONLY, 0000);
        if (fd >= 0) close(fd);
    }
    return 0;
}

/* ---------- operations ---------- */
static struct fuse_operations unionfs_oper = {
    .getattr  = unionfs_getattr,
    .readdir  = unionfs_readdir,
    .open     = unionfs_open,
    .read     = unionfs_read,
    .write    = unionfs_write,
    .truncate = unionfs_truncate,
    .unlink   = unionfs_unlink,
    .create   = unionfs_create,
    .mkdir    = unionfs_mkdir,
    .rmdir    = unionfs_rmdir,
};

/* ---------- main ---------- */
int main(int argc, char *argv[]) {

    if (argc < 4) {
        fprintf(stderr, "Usage: %s lower upper mount\n", argv[0]);
        return 1;
    }

    struct mini_unionfs_state *state = malloc(sizeof(*state));
    state->lower_dir = realpath(argv[1], NULL);
    state->upper_dir = realpath(argv[2], NULL);

    // ✅ FIX: correct argument shifting (NO fuse_argv array)
    argv[1] = argv[3];
    for (int i = 4; i < argc; i++)
        argv[i - 2] = argv[i];

    return fuse_main(argc - 2, argv, &unionfs_oper, state);
}