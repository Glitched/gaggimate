#ifndef RAWFILE_H
#define RAWFILE_H

#include <cstddef>
#include <fcntl.h>
#include <unistd.h>

// A LittleFS file behind a POSIX descriptor instead of the Arduino File wrapper. The wrapper opens with
// fopen(), and newlib hands every FILE a 4 KB stdio buffer from internal RAM on its first read or write.
// Heap-traced on 2026-09-05 that buffer was the largest single allocation live during a shot, and the index
// file at the end of the shot added a second one, on a heap whose floor was 17 KB. The shot writer batches
// into its own buffer already, so the stdio one only cost RAM and a copy; LittleFS keeps its own per-file
// cache either way, so flash traffic is unchanged. Paths are the logical LittleFS paths the plugin uses
// ("/h/123.slog"); the mount point is added in open(). The simulator maps them onto its host directory.
class RawFile {
  public:
    RawFile() = default;
    ~RawFile() { close(); }
    RawFile(const RawFile &) = delete;
    RawFile &operator=(const RawFile &) = delete;

    // flags as for ::open, e.g. O_RDONLY, O_RDWR, O_WRONLY | O_CREAT | O_TRUNC
    bool open(const char *logicalPath, int flags);
    void close();
    explicit operator bool() const { return fd >= 0; }
    // Whole-buffer read and write: true only when every byte moved.
    bool read(void *buf, size_t len) const;
    bool write(const void *buf, size_t len) const;
    bool seek(long offset, int whence) const; // SEEK_SET, SEEK_CUR, SEEK_END

  private:
    int fd = -1;
};

#endif // RAWFILE_H
