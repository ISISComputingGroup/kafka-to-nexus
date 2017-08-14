#pragma once

#include "HDFFile.h"
#include "Msg.h"
#include "logger.h"
#include <array>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace FileWriter {

/// Interface for reading essential information from the flatbuffer which is
/// needed for example to extract timing information and to identify the
/// responsible `HDFWriterModule`.

class FlatbufferReader {
public:
  typedef std::unique_ptr<FlatbufferReader> ptr;
  // Run the flatbuffer verification code and return the result.
  virtual bool verify(Msg const &msg) const = 0;
  // Extract the 'sourcename' from the flatbuffer message.
  virtual std::string sourcename(Msg const &msg) const = 0;
  // Extract the timestamp.
  // TODO discuss with Michele if he actually needs this, or if we always use
  // the Kafka timestamp.
  // uint64_t timestamp(Msg const & msg) const = 0;
};

using FBID = std::array<char, 4>;
FBID fbid_from_str(char const *x);

class FlatbufferReaderRegistry {
public:
  static std::map<FBID, FlatbufferReader::ptr> &items();
  static FlatbufferReader::ptr &find(FBID const &fbid);

  static void registrate(FBID fbid, FlatbufferReader::ptr &&item) {
    auto &m = items();
    if (m.find(fbid) != m.end()) {
      auto s =
          fmt::format("ERROR FlatbufferReader for FBID [{:.{}}] exists already",
                      fbid.data(), fbid.size());
      throw std::runtime_error(s);
    }
    m[fbid] = std::move(item);
  }

  template <typename T> class Registrar {
  public:
    Registrar(FBID fbid) {
      FlatbufferReaderRegistry::registrate(fbid, std::unique_ptr<T>(new T));
    }
  };
};
}
