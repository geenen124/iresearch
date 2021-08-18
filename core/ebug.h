#ifndef EBUG_H
#define EBUG_H

#include <utils/string.hpp>
#include <map>
#include <mutex>
#include <set>
#include <thread>

extern std::mutex mapMutex;


extern std::mutex fileMutex;
extern std::map<std::thread::id, std::string> __filenames;
extern std::string __filename;

//       compressed,   uncompressed
extern std::map<irs::bstring, irs::bstring> m;
               //        offset    file         uncompressed
extern std::map<std::pair<size_t, std::string>, irs::bstring> s;

#endif // EBUG_H
