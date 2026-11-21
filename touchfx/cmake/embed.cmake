# Turns a binary file into a C++ byte array. Usage: cmake -DIN=file -DOUT=file.cpp -P embed.cmake
file(READ "${IN}" hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${hex}")
file(WRITE "${OUT}" "#include <cstddef>\nextern const unsigned char kFontData[] = {${bytes}};\nextern const std::size_t kFontSize = sizeof(kFontData);\n")
