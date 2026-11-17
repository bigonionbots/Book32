#pragma once
// SdFat-style file API (FsFile, SdMan, O_* flags) over InkDeck's ebook storage
// (EbookFS: the MicroSD card on the LILYGO, mounted at /ebooks). The audio
// backend was written against freeink-sdk's SDCardManager; this keeps its
// sources unchanged.
#include <Arduino.h>
#include <FS.h>
#include <fcntl.h>
#include <cstring>
#include "Book32FS.h"

class FsFile {
 public:
  FsFile() = default;
  explicit FsFile(fs::File file) : file_(file) {}

  explicit operator bool() { return static_cast<bool>(file_); }
  bool isOpen() { return static_cast<bool>(file_); }

  int read() { return file_.read(); }
  int read(void* buffer, size_t size) { return static_cast<int>(file_.read(static_cast<uint8_t*>(buffer), size)); }
  size_t write(const void* buffer, size_t size) { return file_.write(static_cast<const uint8_t*>(buffer), size); }
  size_t write(uint8_t byte) { return file_.write(byte); }
  size_t write(const char* text) { return file_.write(reinterpret_cast<const uint8_t*>(text), strlen(text)); }
  bool seekSet(uint64_t position) { return file_.seek(static_cast<uint32_t>(position), fs::SeekSet); }
  bool seekCur(int64_t offset) { return file_.seek(static_cast<uint32_t>(offset), fs::SeekCur); }
  uint64_t curPosition() { return file_.position(); }
  uint64_t fileSize() { return file_.size(); }
  uint64_t size() { return file_.size(); }
  int available() { return file_.available(); }
  void flush() { file_.flush(); }
  bool sync() { file_.flush(); return true; }
  bool close() { file_.close(); return true; }
  bool isDirectory() { return file_ && file_.isDirectory(); }

  /** Writes the entry's name (not its path), like SdFat. */
  size_t getName(char* name, size_t size) {
    if (size == 0) return 0;
    const char* n = file_ ? file_.name() : "";
    if (const char* slash = strrchr(n, '/')) n = slash + 1;
    strncpy(name, n, size - 1);
    name[size - 1] = '\0';
    return strlen(name);
  }

  FsFile openNextFile() { return FsFile(file_.openNextFile()); }
  void rewindDirectory() { file_.rewindDirectory(); }

 private:
  fs::File file_;
};

class SDCardManager {
 public:
  FsFile open(const char* path, int flags = O_RDONLY) {
    const int access = flags & O_ACCMODE;
    if (access == O_RDONLY) return FsFile(EbookFS.open(path, FILE_READ));
    if (flags & O_TRUNC) return FsFile(EbookFS.open(path, FILE_WRITE, true));
    if (flags & O_APPEND) return FsFile(EbookFS.open(path, FILE_APPEND, true));
    // Read/write or write without truncation keeps the contents, as SdFat does.
    if (EbookFS.exists(path)) return FsFile(EbookFS.open(path, "r+"));
    if (flags & O_CREAT) return FsFile(EbookFS.open(path, "w+", true));
    return FsFile();
  }
  bool exists(const char* path) { return EbookFS.exists(path); }
  bool mkdir(const char* path) { return EbookFS.exists(path) || EbookFS.mkdir(path); }
  bool remove(const char* path) { return EbookFS.remove(path); }
  bool rename(const char* from, const char* to) { return EbookFS.rename(from, to); }
  bool rmdir(const char* path) { return EbookFS.rmdir(path); }
  /** Removes a folder and everything in it. */
  bool removeDir(const char* path);
};

extern SDCardManager SdMan;
