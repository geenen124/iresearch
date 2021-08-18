#include <ebug.h>

std::mutex mapMutex;
std::mutex fileMutex;
std::map<std::thread::id, std::string> __filenames;
std::string __filename;

//       compressed,   uncompressed
std::map<irs::bstring, irs::bstring> m;
               //        offset    file         uncompressed
std::map<std::pair<size_t, std::string>, irs::bstring> s;
