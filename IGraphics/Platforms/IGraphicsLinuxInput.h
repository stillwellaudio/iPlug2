/*
 ==============================================================================

 This file is part of the iPlug 2 library. Copyright (C) the iPlug 2 developers.

 See LICENSE.txt for more info.

 ==============================================================================
*/

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace iplug {
namespace igraphics {
namespace linux_input {

inline float DeviceToLogical(float coordinate, float scale)
{
  return scale > 0.f ? coordinate / scale : coordinate;
}

inline float LogicalToDevice(float coordinate, float scale)
{
  return scale > 0.f ? coordinate * scale : coordinate;
}

inline int XdndRootX(long packedCoordinates)
{
  return static_cast<int16_t>(static_cast<uint16_t>(packedCoordinates >> 16));
}

inline int XdndRootY(long packedCoordinates)
{
  return static_cast<int16_t>(static_cast<uint16_t>(packedCoordinates));
}

inline int HexNibble(char character)
{
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'a' && character <= 'f') return 10 + character - 'a';
  if (character >= 'A' && character <= 'F') return 10 + character - 'A';
  return -1;
}

inline bool PercentDecode(const std::string& input, std::string& output)
{
  output.clear();
  output.reserve(input.size());

  for (size_t index = 0; index < input.size(); ++index)
  {
    if (input[index] != '%')
    {
      output.push_back(input[index]);
      continue;
    }

    if (index + 2 >= input.size())
      return false;

    const int high = HexNibble(input[index + 1]);
    const int low = HexNibble(input[index + 2]);
    if (high < 0 || low < 0)
      return false;

    const char decoded = static_cast<char>((high << 4) | low);
    if (decoded == '\0')
      return false;

    output.push_back(decoded);
    index += 2;
  }

  return true;
}

inline std::vector<std::string> ParseTextUriList(const std::string& payload)
{
  std::vector<std::string> paths;
  size_t start = 0;
  while (start < payload.size())
  {
    size_t end = payload.find_first_of("\r\n", start);
    if (end == std::string::npos)
      end = payload.size();

    const std::string line = payload.substr(start, end - start);
    if (!line.empty() && line[0] != '#')
    {
      constexpr const char* prefix = "file://";
      if (line.compare(0, 7, prefix) == 0)
      {
        std::string encodedPath = line.substr(7);
        if (encodedPath.compare(0, 10, "localhost/") == 0)
          encodedPath.erase(0, 9);

        if (!encodedPath.empty() && encodedPath[0] == '/')
        {
          std::string decodedPath;
          if (PercentDecode(encodedPath, decodedPath))
            paths.push_back(std::move(decodedPath));
        }
      }
    }

    start = end + 1;
    while (start < payload.size() && (payload[start] == '\r' || payload[start] == '\n'))
      ++start;
  }

  return paths;
}

} // namespace linux_input
} // namespace igraphics
} // namespace iplug
