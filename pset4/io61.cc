#include "io61.hh"
#include <cerrno>
#include <climits>
#include <sys/stat.h>
#include <sys/types.h>

// io61.c
//    YOUR CODE HERE!

// io61_file
//    Data structure for io61 file wrappers. Add your own stuff.
struct io61_file {
  int fd;
  int mode;
  static constexpr off_t bufsize = 8192;
  char cbuf[bufsize];
  off_t tag;
  off_t beg_tag; // start of buffer
  off_t end_tag; // end of buffer
  off_t pos_tag; // current position in buffer
};

// io61_fdopen(fd, mode)
//    Return a new io61_file for file descriptor `fd`. `mode` is
//    either O_RDONLY for a read-only file or O_WRONLY for a
//    write-only file. You need not support read/write files.

io61_file *io61_fdopen(int fd, int mode) {
  assert(fd >= 0);
  io61_file *f = new io61_file;
  f->fd = fd;
  f->mode = mode;
  return f;
}

// io61_close(f)
//    Close the io61_file `f` and release all its resources.

int io61_close(io61_file *f) {
  io61_flush(f);
  int r = close(f->fd);
  delete f;
  return r;
}

void io61_fill(io61_file *f) {
  f->beg_tag = f->pos_tag = f->end_tag;
  ssize_t nread = read(f->fd, f->cbuf, f->bufsize);
  if (nread >= 0) {
    f->end_tag = f->beg_tag + nread;
  }
}

// io61_readc(f)
//    Read a single (unsigned) character from `f` and return it. Returns EOF
//    (which is -1) on error or end-of-file.

int io61_readc(io61_file *f) {
  if (f->pos_tag == f->end_tag) {
    io61_fill(f);
    if (f->pos_tag == f->end_tag) {
      return EOF;
    }
  }
  unsigned char c = f->cbuf[f->pos_tag - f->beg_tag];
  ++f->pos_tag;
  return c;
}

// io61_fill(f)
// fills the read cache with new data

// io61_read(f, buf, sz)
//    Read up to `sz` characters from `f` into `buf`. Returns the number of
//    characters read on success; normally this is `sz`. Returns a short
//    count, which might be zero, if the file ended before `sz` characters
//    could be read. Returns -1 if an error occurred before any characters
//    were read.

ssize_t io61_read(io61_file *f, char *buf, size_t sz) {
  // tracking variables
  size_t bytes_read = 0;
  size_t req_bytes = 0;

  while (bytes_read < sz) {
    // fill the buffer if we are outside of it
    if (f->pos_tag >= f->end_tag) {
      io61_fill(f);
      // if we are still out we are done
      if (f->pos_tag >= f->end_tag) {
        break;
      }
    }
    // calculate amount of bytes remaining to be read
    req_bytes = f->end_tag - f->pos_tag;
    if (sz - bytes_read < req_bytes) {
      req_bytes = sz - bytes_read;
    }
    // read and update counters
    memcpy(&buf[bytes_read], &f->cbuf[f->pos_tag - f->beg_tag], req_bytes);
    f->pos_tag += req_bytes;
    bytes_read += req_bytes;
  }

  // check for failure
  if (bytes_read == 0) {
    return -1;
  }

  return bytes_read;

  // Note: This function never returns -1 because `io61_readc`
  // does not distinguish between error and end-of-file.
  // Your final version should return -1 if a system call indicates
  // an error.
}

// io61_writec(f)
//    Write a single character `ch` to `f`. Returns 0 on success or
//    -1 on error.

int io61_writec(io61_file *f, int ch) {
  // if we are over our buffer flush
  if (f->end_tag == f->beg_tag + f->bufsize) {
    io61_flush(f);
  }
  f->cbuf[f->pos_tag - f->beg_tag] = ch;
  ++f->pos_tag;
  ++f->end_tag;
  return 0;
}

// io61_write(f, buf, sz)
//    Write `sz` characters from `buf` to `f`. Returns the number of
//    characters written on success; normally this is `sz`. Returns -1 if
//    an error occurred before any characters were written.

ssize_t io61_write(io61_file *f, const char *buf, size_t sz) {
  // keep track of how many bytes we have written and need to write
  size_t bytes_written = 0;
  size_t rec_bytes = 0;

  // loop over bytes buffer by buffer
  while (bytes_written < sz) {
    if (f->end_tag == f->beg_tag + f->bufsize) {
      io61_flush(f); // flush buffer if full
    }
    // calculate bytes still needing to be written
    rec_bytes = f->bufsize - (f->pos_tag - f->beg_tag);
    if (sz - bytes_written < rec_bytes) {
      rec_bytes = sz - bytes_written;
    }
    memcpy(&f->cbuf[f->pos_tag - f->beg_tag], &buf[bytes_written], rec_bytes);

    // update tracking counters
    f->pos_tag += rec_bytes;
    f->end_tag += rec_bytes;
    bytes_written += rec_bytes;
  }
  return bytes_written;
}

// io61_flush(f)
//    Forces a write of all buffered data written to `f`.
//    If `f` was opened read-only, io61_flush(f) may either drop all
//    data buffered for reading, or do nothing.

int io61_flush(io61_file *f) {
  if (f->mode == O_RDONLY) {
    return 0;
  }
  ssize_t n = write(f->fd, f->cbuf, f->pos_tag - f->beg_tag);
  f->beg_tag = f->pos_tag;
  if (n >= 0) {
    return 0;
  } else {
    return n;
  }
}

// io61_seek(f, pos)
//    Change the file pointer for file `f` to `pos` bytes into the file.
//    Returns 0 on success and -1 on failure.

int io61_seek(io61_file *f, off_t pos) {
  if (f->mode == O_WRONLY) {
    io61_flush(f);
  }
  // if the position is within the current file buffer then we are done
  if (pos < f->end_tag && pos >= f->beg_tag) {
    f->pos_tag = pos;
    return 0;
  }
  // otherwise need to seek
  off_t new_pos = pos;
  if (f->mode == O_RDONLY) {
    new_pos = (pos / f->bufsize) * f->bufsize;
  }
  off_t r = lseek(f->fd, new_pos, SEEK_SET);
  if (f->mode == O_RDONLY) {
    f->end_tag = new_pos;
    io61_fill(f);
  } else {
    f->beg_tag = f->end_tag = pos;
  }
  f->pos_tag = pos;

  // check for success and return
  if (r == new_pos) {
    return 0;
  } else {
    return -1;
  }
}

// You shouldn't need to change these functions.

// io61_open_check(filename, mode)
//    Open the file corresponding to `filename` and return its io61_file.
//    If `!filename`, returns either the standard input or the
//    standard output, depending on `mode`. Exits with an error message if
//    `filename != nullptr` and the named file cannot be opened.

io61_file *io61_open_check(const char *filename, int mode) {
  int fd;
  if (filename) {
    fd = open(filename, mode, 0666);
  } else if ((mode & O_ACCMODE) == O_RDONLY) {
    fd = STDIN_FILENO;
  } else {
    fd = STDOUT_FILENO;
  }
  if (fd < 0) {
    fprintf(stderr, "%s: %s\n", filename, strerror(errno));
    exit(1);
  }
  return io61_fdopen(fd, mode & O_ACCMODE);
}

// io61_filesize(f)
//    Return the size of `f` in bytes. Returns -1 if `f` does not have a
//    well-defined size (for instance, if it is a pipe).

off_t io61_filesize(io61_file *f) {
  struct stat s;
  int r = fstat(f->fd, &s);
  if (r >= 0 && S_ISREG(s.st_mode)) {
    return s.st_size;
  } else {
    return -1;
  }
}
