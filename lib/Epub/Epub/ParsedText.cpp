#include "ParsedText.h"

#include <GfxRenderer.h>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <vector>

constexpr int MAX_COST = std::numeric_limits<int>::max();

void ParsedText::addWord(std::string word, const EpdFontFamily::Style fontStyle) {
  if (word.empty()) return;

  words.push_back(std::move(word));
  wordStyles.push_back(fontStyle);
}

// Helper function to split a UTF-8 string into individual characters
static std::vector<std::string> splitUtf8Chars(const std::string& str) {
  std::vector<std::string> chars;
  const char* p = str.c_str();
  while (*p) {
    int charLen = 1;
    const unsigned char c = static_cast<unsigned char>(*p);
    if ((c & 0xF8) == 0xF0) {
      charLen = 4;
    } else if ((c & 0xF0) == 0xE0) {
      charLen = 3;
    } else if ((c & 0xE0) == 0xC0) {
      charLen = 2;
    }
    chars.push_back(std::string(p, charLen));
    p += charLen;
  }
  return chars;
}

// Character-wrap mode: greedy line filling with justified alignment (1.0x-1.5x spacing)
// If spacing would exceed 1.5x, split words at character boundaries to fill the line
void ParsedText::layoutCharacterWrap(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                     const int spaceWidth,
                                     const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                     const bool includeLastLine) {
  const int pageWidth = viewportWidth;
  // Spacing range: 1.0x to 1.5x of normal space width
  const int minSpacing = spaceWidth;
  const int maxSpacing = spaceWidth + (spaceWidth / 2);  // 1.5x

  // Add paragraph indent to first word
  if ((style == TextBlock::JUSTIFIED || style == TextBlock::LEFT_ALIGN) && paragraphIndent && !words.empty()) {
    words.front().insert(0, "\xe3\x80\x80");  // U+3000 ideographic space
  }

  while (!words.empty()) {
    std::vector<std::string> lineWordsVec;
    std::vector<int> lineWordWidths;
    std::vector<EpdFontFamily::Style> lineWordStylesVec;

    // Phase 1: Greedily collect words/characters to fill the line
    // Target: spacing should be between minSpacing and maxSpacing
    int totalWordWidth = 0;

    while (!words.empty()) {
      const std::string& word = words.front();
      const EpdFontFamily::Style wordStyle = wordStyles.front();
      const int wordWidth = renderer.getTextWidth(fontId, word.c_str(), wordStyle);

      // Calculate what spacing would be if we add this word
      int newTotalWidth = totalWordWidth + wordWidth;
      int newGapCount = lineWordsVec.size();  // gaps = word count (before adding new word)
      int newSpareSpace = pageWidth - newTotalWidth;
      int newSpacing = (newGapCount > 0) ? (newSpareSpace / newGapCount) : maxSpacing + 1;

      if (lineWordsVec.empty()) {
        // First word - must add something
        if (wordWidth <= pageWidth) {
          // Whole word fits
          lineWordsVec.push_back(word);
          lineWordWidths.push_back(wordWidth);
          lineWordStylesVec.push_back(wordStyle);
          totalWordWidth = wordWidth;
          words.pop_front();
          wordStyles.pop_front();
        } else {
          // Word too long - split it
          auto chars = splitUtf8Chars(word);
          std::string partial;
          size_t charsFit = 0;
          for (size_t i = 0; i < chars.size(); i++) {
            std::string test = partial + chars[i];
            int testWidth = renderer.getTextWidth(fontId, test.c_str(), wordStyle);
            if (testWidth > pageWidth) break;
            partial = test;
            charsFit = i + 1;
          }
          if (charsFit == 0) {
            charsFit = 1;
            partial = chars[0];
          }
          int partialWidth = renderer.getTextWidth(fontId, partial.c_str(), wordStyle);
          lineWordsVec.push_back(partial);
          lineWordWidths.push_back(partialWidth);
          lineWordStylesVec.push_back(wordStyle);
          totalWordWidth = partialWidth;

          if (charsFit < chars.size()) {
            std::string remainder;
            for (size_t i = charsFit; i < chars.size(); i++) remainder += chars[i];
            words.front() = remainder;
          } else {
            words.pop_front();
            wordStyles.pop_front();
          }
        }
      } else if (newSpacing >= minSpacing) {
        // Adding this word keeps spacing >= minSpacing - add it
        lineWordsVec.push_back(word);
        lineWordWidths.push_back(wordWidth);
        lineWordStylesVec.push_back(wordStyle);
        totalWordWidth = newTotalWidth;
        words.pop_front();
        wordStyles.pop_front();

        // If spacing is now within range, we might be done with this line
        if (newSpacing <= maxSpacing) {
          // Perfect! But check if we can fit more
          continue;
        }
      } else {
        // Adding whole word would make spacing < minSpacing
        // Try to add partial characters from this word
        int currentGapCount = lineWordsVec.size();
        // We want: (pageWidth - totalWordWidth - partialWidth) / currentGapCount >= minSpacing
        // So: partialWidth <= pageWidth - totalWordWidth - currentGapCount * minSpacing
        int maxPartialWidth = pageWidth - totalWordWidth - currentGapCount * minSpacing;

        if (maxPartialWidth > 0) {
          auto chars = splitUtf8Chars(word);
          std::string partial;
          size_t charsFit = 0;
          for (size_t i = 0; i < chars.size(); i++) {
            std::string test = partial + chars[i];
            int testWidth = renderer.getTextWidth(fontId, test.c_str(), wordStyle);
            if (testWidth > maxPartialWidth) break;
            partial = test;
            charsFit = i + 1;
          }

          if (charsFit > 0) {
            int partialWidth = renderer.getTextWidth(fontId, partial.c_str(), wordStyle);
            lineWordsVec.push_back(partial);
            lineWordWidths.push_back(partialWidth);
            lineWordStylesVec.push_back(wordStyle);
            totalWordWidth += partialWidth;

            if (charsFit < chars.size()) {
              std::string remainder;
              for (size_t i = charsFit; i < chars.size(); i++) remainder += chars[i];
              words.front() = remainder;
            } else {
              words.pop_front();
              wordStyles.pop_front();
            }
          }
        }
        // Line is full
        break;
      }
    }

    // Phase 2: Check if spacing is too large, fill with more characters
    while (!words.empty() && lineWordsVec.size() >= 1) {
      int gapCount = lineWordsVec.size();
      int spareSpace = pageWidth - totalWordWidth;
      int spacing = (gapCount > 0) ? (spareSpace / gapCount) : 0;

      if (spacing <= maxSpacing) break;  // Spacing is acceptable

      // Spacing too large - try to add characters from next word
      const std::string& nextWord = words.front();
      const EpdFontFamily::Style nextStyle = wordStyles.front();
      auto chars = splitUtf8Chars(nextWord);

      // Calculate max width for partial word to keep spacing <= maxSpacing
      // (pageWidth - totalWordWidth - partialWidth) / gapCount <= maxSpacing
      // partialWidth >= pageWidth - totalWordWidth - gapCount * maxSpacing
      int minPartialWidth = pageWidth - totalWordWidth - gapCount * maxSpacing;
      // Also ensure spacing >= minSpacing after adding
      // (pageWidth - totalWordWidth - partialWidth) / gapCount >= minSpacing
      // partialWidth <= pageWidth - totalWordWidth - gapCount * minSpacing
      int maxPartialWidth = pageWidth - totalWordWidth - gapCount * minSpacing;

      if (maxPartialWidth <= 0) break;  // Can't fit anything

      std::string partial;
      int partialWidth = 0;
      size_t charsFit = 0;
      for (size_t i = 0; i < chars.size(); i++) {
        std::string test = partial + chars[i];
        int testWidth = renderer.getTextWidth(fontId, test.c_str(), nextStyle);
        if (testWidth > maxPartialWidth) break;
        partial = test;
        partialWidth = testWidth;
        charsFit = i + 1;
      }

      if (charsFit == 0) break;  // Can't fit any character

      // Add partial
      lineWordsVec.push_back(partial);
      lineWordWidths.push_back(partialWidth);
      lineWordStylesVec.push_back(nextStyle);
      totalWordWidth += partialWidth;

      if (charsFit < chars.size()) {
        std::string remainder;
        for (size_t i = charsFit; i < chars.size(); i++) remainder += chars[i];
        words.front() = remainder;
      } else {
        words.pop_front();
        wordStyles.pop_front();
      }
    }

    // Phase 3: Calculate final positions for justified alignment
    bool isLastLine = words.empty();
    int gapCount = lineWordsVec.size() - 1;
    int spareSpace = pageWidth - totalWordWidth;

    std::list<std::string> lineWords;
    std::list<uint16_t> lineXPos;
    std::list<EpdFontFamily::Style> lineWordStyles;

    if (isLastLine || gapCount <= 0) {
      // Last line or single word: left align with normal spacing
      int xpos = 0;
      for (size_t i = 0; i < lineWordsVec.size(); i++) {
        lineXPos.push_back(static_cast<uint16_t>(xpos));
        lineWords.push_back(lineWordsVec[i]);
        lineWordStyles.push_back(lineWordStylesVec[i]);
        xpos += lineWordWidths[i] + minSpacing;
      }
    } else {
      // Justified: distribute spare space evenly across gaps
      // Use fixed-point arithmetic for even distribution
      int baseSpacing = spareSpace / gapCount;
      int extraPixels = spareSpace % gapCount;  // Distribute these across first N gaps

      int xpos = 0;
      for (size_t i = 0; i < lineWordsVec.size(); i++) {
        lineXPos.push_back(static_cast<uint16_t>(xpos));
        lineWords.push_back(lineWordsVec[i]);
        lineWordStyles.push_back(lineWordStylesVec[i]);

        if (i < lineWordsVec.size() - 1) {
          int gap = baseSpacing + (static_cast<int>(i) < extraPixels ? 1 : 0);
          xpos += lineWordWidths[i] + gap;
        }
      }
    }

    // Process the line
    if (!lineWords.empty() && (!isLastLine || includeLastLine)) {
      TextBlock::Style lineStyle = isLastLine ? TextBlock::LEFT_ALIGN : TextBlock::JUSTIFIED;
      processLine(
          std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos), std::move(lineWordStyles), lineStyle));
    }
  }
}

// Consumes data to minimize memory usage
void ParsedText::layoutAndExtractLines(const GfxRenderer& renderer, const int fontId, const uint16_t viewportWidth,
                                       const std::function<void(std::shared_ptr<TextBlock>)>& processLine,
                                       const bool includeLastLine) {
  if (words.empty()) {
    return;
  }

  const int pageWidth = viewportWidth;
  const int spaceWidth = renderer.getSpaceWidth(fontId);

  // Use character wrap mode for Korean text with justified alignment
  if (characterWrap && style == TextBlock::JUSTIFIED) {
    layoutCharacterWrap(renderer, fontId, viewportWidth, spaceWidth, processLine, includeLastLine);
    return;
  }

  // Standard layout algorithm (Knuth-Plass)
  const auto wordWidths = calculateWordWidths(renderer, fontId);
  const auto lineBreakIndices = computeLineBreaks(pageWidth, spaceWidth, wordWidths);
  const size_t lineCount = includeLastLine ? lineBreakIndices.size() : lineBreakIndices.size() - 1;

  for (size_t i = 0; i < lineCount; ++i) {
    extractLine(i, pageWidth, spaceWidth, wordWidths, lineBreakIndices, processLine);
  }
}

std::vector<uint16_t> ParsedText::calculateWordWidths(const GfxRenderer& renderer, const int fontId) {
  const size_t totalWordCount = words.size();

  std::vector<uint16_t> wordWidths;
  wordWidths.reserve(totalWordCount);

  // add ideographic space (U+3000) at the beginning of first word in paragraph to indent
  // Note: Using U+3000 instead of EM-SPACE (U+2003) for Korean font compatibility
  if ((style == TextBlock::JUSTIFIED || style == TextBlock::LEFT_ALIGN) && paragraphIndent) {
    std::string& first_word = words.front();
    first_word.insert(0, "\xe3\x80\x80");
  }

  auto wordsIt = words.begin();
  auto wordStylesIt = wordStyles.begin();

  while (wordsIt != words.end()) {
    wordWidths.push_back(renderer.getTextWidth(fontId, wordsIt->c_str(), *wordStylesIt));

    std::advance(wordsIt, 1);
    std::advance(wordStylesIt, 1);
  }

  return wordWidths;
}

std::vector<size_t> ParsedText::computeLineBreaks(const int pageWidth, const int spaceWidth,
                                                  const std::vector<uint16_t>& wordWidths) const {
  const size_t totalWordCount = words.size();

  // DP table to store the minimum badness (cost) of lines starting at index i
  std::vector<int> dp(totalWordCount);
  // 'ans[i]' stores the index 'j' of the *last word* in the optimal line starting at 'i'
  std::vector<size_t> ans(totalWordCount);

  // Base Case
  dp[totalWordCount - 1] = 0;
  ans[totalWordCount - 1] = totalWordCount - 1;

  for (int i = totalWordCount - 2; i >= 0; --i) {
    int currlen = -spaceWidth;
    dp[i] = MAX_COST;

    for (size_t j = i; j < totalWordCount; ++j) {
      // Current line length: previous width + space + current word width
      currlen += wordWidths[j] + spaceWidth;

      if (currlen > pageWidth) {
        break;
      }

      int cost;
      if (j == totalWordCount - 1) {
        cost = 0;  // Last line
      } else {
        const int remainingSpace = pageWidth - currlen;
        // Use long long for the square to prevent overflow
        const long long cost_ll = static_cast<long long>(remainingSpace) * remainingSpace + dp[j + 1];

        if (cost_ll > MAX_COST) {
          cost = MAX_COST;
        } else {
          cost = static_cast<int>(cost_ll);
        }
      }

      if (cost < dp[i]) {
        dp[i] = cost;
        ans[i] = j;  // j is the index of the last word in this optimal line
      }
    }

    // Handle oversized word: if no valid configuration found, force single-word line
    // This prevents cascade failure where one oversized word breaks all preceding words
    if (dp[i] == MAX_COST) {
      ans[i] = i;  // Just this word on its own line
      // Inherit cost from next word to allow subsequent words to find valid configurations
      if (i + 1 < static_cast<int>(totalWordCount)) {
        dp[i] = dp[i + 1];
      } else {
        dp[i] = 0;
      }
    }
  }

  // Stores the index of the word that starts the next line (last_word_index + 1)
  std::vector<size_t> lineBreakIndices;
  size_t currentWordIndex = 0;

  while (currentWordIndex < totalWordCount) {
    size_t nextBreakIndex = ans[currentWordIndex] + 1;

    // Safety check: prevent infinite loop if nextBreakIndex doesn't advance
    if (nextBreakIndex <= currentWordIndex) {
      // Force advance by at least one word to avoid infinite loop
      nextBreakIndex = currentWordIndex + 1;
    }

    lineBreakIndices.push_back(nextBreakIndex);
    currentWordIndex = nextBreakIndex;
  }

  return lineBreakIndices;
}

void ParsedText::extractLine(const size_t breakIndex, const int pageWidth, const int spaceWidth,
                             const std::vector<uint16_t>& wordWidths, const std::vector<size_t>& lineBreakIndices,
                             const std::function<void(std::shared_ptr<TextBlock>)>& processLine) {
  const size_t lineBreak = lineBreakIndices[breakIndex];
  const size_t lastBreakAt = breakIndex > 0 ? lineBreakIndices[breakIndex - 1] : 0;
  const size_t lineWordCount = lineBreak - lastBreakAt;

  // Calculate total word width for this line
  int lineWordWidthSum = 0;
  for (size_t i = lastBreakAt; i < lineBreak; i++) {
    lineWordWidthSum += wordWidths[i];
  }

  // Calculate spacing
  const int spareSpace = pageWidth - lineWordWidthSum;

  int spacing = spaceWidth;
  const bool isLastLine = breakIndex == lineBreakIndices.size() - 1;

  if (style == TextBlock::JUSTIFIED && !isLastLine && lineWordCount >= 2) {
    spacing = spareSpace / (lineWordCount - 1);
  }

  // Calculate initial x position
  uint16_t xpos = 0;
  if (style == TextBlock::RIGHT_ALIGN) {
    xpos = spareSpace - (lineWordCount - 1) * spaceWidth;
  } else if (style == TextBlock::CENTER_ALIGN) {
    xpos = (spareSpace - (lineWordCount - 1) * spaceWidth) / 2;
  }

  // Pre-calculate X positions for words
  std::list<uint16_t> lineXPos;
  for (size_t i = 0; i < lineWordCount; i++) {
    lineXPos.push_back(xpos);
    xpos += wordWidths[lastBreakAt + i] + spacing;
  }

  // Iterators always start at the beginning as we are moving content with splice below
  auto wordEndIt = words.begin();
  auto wordStyleEndIt = wordStyles.begin();
  std::advance(wordEndIt, lineWordCount);
  std::advance(wordStyleEndIt, lineWordCount);

  // *** CRITICAL STEP: CONSUME DATA USING SPLICE ***
  std::list<std::string> lineWords;
  lineWords.splice(lineWords.begin(), words, words.begin(), wordEndIt);
  std::list<EpdFontFamily::Style> lineWordStyles;
  lineWordStyles.splice(lineWordStyles.begin(), wordStyles, wordStyles.begin(), wordStyleEndIt);

  processLine(std::make_shared<TextBlock>(std::move(lineWords), std::move(lineXPos), std::move(lineWordStyles), style));
}
