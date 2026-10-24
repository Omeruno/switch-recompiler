#pragma once
#include <cstddef>
#include <string>
#include <sys/types.h>

namespace rt {
// Actual native operations; injectable to exercise deterministic disk/I/O errors.
// Close consumes the descriptor even when reporting an error; never retry close.
class FileCopyIo {
public:
    virtual ~FileCopyIo()=default;
    virtual int OpenRead(const std::string&);
    virtual int OpenReplace(const std::string&); // owned preference slot only
    virtual int CreateExclusive(const std::string&);
    virtual ssize_t Read(int,void*,size_t);
    virtual ssize_t Write(int,const void*,size_t);
    virtual int Flush(int);
    virtual int Close(int);
    virtual int Rename(const std::string&,const std::string&);
    virtual int Remove(const std::string&);
};
} // namespace rt
