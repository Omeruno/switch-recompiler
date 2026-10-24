#include "file_copy_io.h"
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
namespace rt {
int FileCopyIo::OpenRead(const std::string& p) {return open(p.c_str(),O_RDONLY);}
int FileCopyIo::CreateExclusive(const std::string& p) {return open(p.c_str(),O_CREAT|O_EXCL|O_WRONLY,0600);}
ssize_t FileCopyIo::Read(int fd,void* p,size_t n) {return read(fd,p,n);}
ssize_t FileCopyIo::Write(int fd,const void* p,size_t n) {return write(fd,p,n);}
int FileCopyIo::Flush(int fd) {return fsync(fd);}
int FileCopyIo::Close(int fd) {return close(fd);}
int FileCopyIo::Rename(const std::string& a,const std::string& b) {return std::rename(a.c_str(),b.c_str());}
int FileCopyIo::Remove(const std::string& p) {return unlink(p.c_str());}
} // namespace rt
