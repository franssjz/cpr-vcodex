#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Reads Sudoku puzzle banks straight from the SD card (e.g.
// /sudoku/easy.txt) without any preprocessing. The on-disk format is the
// public-domain "sudoku-exchange" layout: one fixed-width record per line,
//
//     <12-char id> <81 puzzle digits> <rating>\n
//
// where the puzzle digits use '1'-'9' for givens and '0'/'.' for blanks.
// Plain "81 digits per line" files are also accepted.
//
// Because every record is the same length, there is no need to index the
// whole file (a bank can hold hundreds of thousands of puzzles, far too many
// to keep an offset table for in the ESP32-C3's RAM). open() measures the
// stride from the first line, then loadPuzzle() seeks straight to
// index * stride and reads a single record -- O(1) time and effectively zero
// extra RAM, which is the whole reason the files can be used as-is.
class SudokuPuzzleBank {
 public:
  // Scans the SD card directory for .txt puzzle banks.
  static std::vector<std::string> listBankPaths(const std::string& directory);

  // Builds a display name from a bank file path (file name without extension).
  static std::string displayNameFromPath(const std::string& path);

  bool open(const std::string& path);
  void close();

  bool isOpen() const { return puzzleCount_ > 0; }
  int puzzleCount() const { return puzzleCount_; }
  const std::string& path() const { return path_; }

  // Reads puzzle `index` (0-based) into outPuzzle as an 81-character string.
  // Returns false on a bad index or a malformed record.
  bool loadPuzzle(int index, std::string& outPuzzle) const;

 private:
  std::string path_;
  uint32_t stride_ = 0;     // bytes from the start of one record to the next
  int puzzleCount_ = 0;
};
