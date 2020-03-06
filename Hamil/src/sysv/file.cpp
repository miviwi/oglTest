#include <sysv/file.h>
#include <util/polystorage.h>
#include <os/path.h>

#include <optional>
#include <memory>

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <cstdio>

#include <config>

#if __sysv
#  include <unistd.h>
#  include <fcntl.h>
#  include <glob.h>
#  include <dirent.h>
#  include <fnmatch.h>
#  include <sys/types.h>
#  include <sys/stat.h>
#  include <sys/mman.h>
#  include <linux/limits.h>
#endif

namespace sysv {

using SysvStat = struct stat;

struct FileData {
#if __sysv
  int fd = -1;

  // Lazy-initialized 
  mutable std::optional<SysvStat> st = std::nullopt;
  mutable std::unique_ptr<char[]> full_path = nullptr;
  // ----------------
#endif

  void cleanup()
  {
#if __sysv
    close(fd);
#endif
  }

};

struct FileQueryData {
#if __sysv
  std::optional<os::Path> pattern = std::nullopt;
  DIR *dir = nullptr;
#endif

  void cleanup()
  {
#if __sysv
    if(dir) closedir(dir);
#endif
  }
};

File *File::alloc()
{
  return WithPolymorphicStorage::alloc<File, FileData>();
}

void File::destroy(File *f)
{
  WithPolymorphicStorage::destroy<FileData>(f);
}

File::File()
{
  new(storage<FileData>()) FileData();
}

File::~File()
{
  // Only CHECK the ref-count so it's not decremented twice
  if(refs() > 1) return;

  if(isOpen()) data().cleanup();
}

void File::doOpen(const char *path, Access access, Share share, OpenMode mode)
{
#if __sysv
  int oflag = 0;
  if(access == Read) {
    oflag = O_RDONLY;
  } else if(access == Write) {
    oflag = O_WRONLY;
  } else if((access & Read) && (access & Write)) {
    oflag = O_RDWR;
  } else {
    throw FileOpenError();
  }

  switch(mode) {
  case CreateAlways:     oflag |= O_CREAT|O_TRUNC; break;
  case CreateNew:        oflag |= O_CREAT|O_EXCL; break;
  case OpenAlways:       oflag |= O_APPEND; break;
  case OpenExisting:     break;
  case TruncateExisting: oflag |= O_TRUNC; break;
  }

  int fd = ::open(path, oflag, 0755);
  if(fd < 0) throw FileOpenError();

  // Store the file descriptor
  data().fd = fd;

  // No need for a fcntl(FSETLK) call
  if(share == (ShareRead|ShareWrite) || mode == CreateAlways) return;

  struct flock lock;
  memset(&lock, 0, sizeof(struct flock));

  if(share == ShareNone) {
    lock.l_type = F_WRLCK;
  } else if(share == ShareWrite) {
    lock.l_type = F_RDLCK;
  }

  lock.l_whence = SEEK_SET;
  lock.l_start = 0;
  lock.l_len = 0;     // 0 is a special value which signifies the entire file

  int fcntl_err = fcntl(fd, F_SETLK, &lock);
  if(fcntl_err) {
    close(fd);
    data().fd = -1;

    throw FileOpenError();
  }
#endif
}

size_t File::size() const
{
#if __sysv
  stat();

  auto st = data().st.value();
  return (size_t)st.st_size;
#endif
}

const char *File::fullPath() const
{
#if __sysv
  if(data().full_path) return data().full_path.get();   // We've fetched the full path before

  assert(data().fd > 0 && "fullPath() can be called ONLY after open()!");

  char proc_fd_path[32];

  auto proc_fd_path_len = snprintf(proc_fd_path, sizeof(proc_fd_path),
      "/proc/self/fd/%d", data().fd);
  assert((size_t)proc_fd_path_len < sizeof(proc_fd_path));

  char path_buf[PATH_MAX];
  int full_path_len = readlink(proc_fd_path, path_buf, sizeof(path_buf));

  if(full_path_len < 0) throw GetFileInfoError();

  // Allocate some memory...
  data().full_path.reset(new char[full_path_len+1]);

  //   ...copy the path into that memory...
  memcpy(data().full_path.get(), path_buf, full_path_len);

  //   ...and write a '\0' at the end because readlink doesn't do it :)
  data().full_path[full_path_len] = '\0';

  return data().full_path.get();
#else
  return nullptr;
#endif
}

size_t File::read(void *buf, size_t sz)
{
#if __sysv
  assert(data().fd > 0 && "read() called before open()!");

  auto num_read = ::read(data().fd, buf, sz);
  if(num_read < 0) return ReadWriteFailed;

  return (size_t)num_read;
#endif

  return ReadWriteFailed;
}

size_t File::read(void *buf)
{
#if __sysv
  return read(buf, size());
#endif

  return ReadWriteFailed;
}

size_t File::write(const void *buf, size_t sz)
{
#if __sysv
  assert(data().fd > 0 && "write() called before open()!");

  auto num_written = ::write(data().fd, buf, sz);
  if(num_written < 0) return ReadWriteFailed;

  return (size_t)num_written;
#endif

  return ReadWriteFailed;
}

os::File& File::seek(Seek whence_, size_t offset)
{
#if __sysv
  int whence = -1;
  switch(whence_) {
  case SeekBegin:   whence = SEEK_SET; break;
  case SeekCurrent: whence = SEEK_CUR; break;
  case SeekEnd:     whence = SEEK_END; break;
  }

  auto seek_result = lseek(data().fd, offset, whence);
  assert(seek_result >= 0);
#endif

  return *this;
}

size_t File::seekOffset() const
{
#if __sysv
  auto offset = lseek(data().fd, 0, SEEK_CUR);

  // Make sure the call succeeded
  if(offset < 0) return SeekOffsetInvalid;

  return offset;
#else
  return SeekOffsetInvalid;
#endif
}

bool File::flush()
{
#if __sysv
  assert(data().fd > 0 && "flush() called before open()!");

  auto err = fsync(data().fd);

  return !err;
#else
  return false;
#endif
}

void File::stat() const
{
#if __sysv
  if(data().st.has_value()) return;     // Check if stat() hasn't been called before...

  // ...and if not perform the syscall
  assert(data().fd > 0 && "stat() can only be called AFTER open()!");

  data().st.emplace();
  auto stat_err = ::fstat(data().fd, &data().st.value());

  if(stat_err) throw GetFileInfoError();
#endif
}

FileData& File::data()
{
  return *storage<FileData>();
}

const FileData& File::data() const
{
  return *storage<FileData>();
}

FileView::FileView(os::File *file, size_t size, const char *name) :
  os::FileView(file, size, name)
{
}

FileView::~FileView()
{
  unmap();
}

void *FileView::doMap(Protect protect, size_t offset)
{
#if __sysv
  auto file = (sysv::File *)origin();

  int prot = 0;
  if(protect == File::ProtectNone) {
    prot = PROT_NONE;
  } else {
    if(protect & File::ProtectRead) prot |= PROT_READ;
    if(protect & File::ProtectWrite) prot |= PROT_WRITE;
    if(protect & File::ProtectExecute) prot |= PROT_EXEC;
  }

  int flags = MAP_SHARED;

  auto ptr = mmap(NULL, size(), prot, flags, file->storage<FileData>()->fd, offset);
  assert(ptr);

  return ptr;
#endif

  return nullptr;
}

void FileView::doFlush(Size size)
{
#if __sysv
  auto sync_err = msync(m_ptr, size, MS_SYNC);
  assert(!sync_err);
#endif
}

void FileView::unmap()
{
#if __sysv
  if(!m_ptr) return;

  auto unmap_err = munmap(m_ptr, size());
  assert(!unmap_err);
#endif
}

FileQuery *FileQuery::alloc()
{
  return WithPolymorphicStorage::alloc<FileQuery, FileQueryData>();
}

void FileQuery::destroy(FileQuery *q)
{
  WithPolymorphicStorage::destroy<FileQueryData>(q);
}

FileQuery::FileQuery()
{
  new(storage<FileQueryData>()) FileQueryData();
}

FileQuery::~FileQuery()
{
  // Only CHECK the ref-count so it's not decremented twice
  if(refs() > 1) return;

  data().cleanup();
}

void FileQuery::foreach(IterFn fn)
{
#if __sysv
  assert(data().dir && data().pattern &&
      "foreach() called on an invalid FileQuery object!");

  const auto& pattern = data().pattern.value();
  auto pattern_back = pattern.back();

  auto dir = data().dir;
  while(auto ent = readdir(dir)) {
    std::string name = ent->d_name;

    // Skip the "." (current directory) and ".." (parent directory) entries
    if(!strcmp(name.data(), ".") || !strcmp(name.data(), "..")) continue;

    // Match the entry to the pattern
    int match_err = fnmatch(pattern_back.data(), name.data(), 0);
    if(match_err == FNM_NOMATCH) continue;

    // Make sure no errors have occured
    assert(!match_err);

    bool is_dir = ent->d_type & DT_DIR;
    unsigned attrs = is_dir ? IsDirectory : IsFile;

    fn(name.data(), (Attributes)attrs);
  }
#endif
}

void FileQuery::doOpen(const char *path)
{
#if __sysv
  os::Path p = path;

  auto dir = opendir(p.enclosingDir().data());
  if(!dir) throw Error();

  data().pattern.emplace(std::move(p));
  data().dir = dir;
#endif
}

FileQueryData& FileQuery::data()
{
  return *WithPolymorphicStorage::storage<FileQueryData>();
}

bool current_working_directory(const char *dir)
{
#if __sysv
  auto chdir_err = chdir(dir);
  if(chdir_err) return false;

  return true;
#endif
}

}
