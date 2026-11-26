// Regenerate romfs/date-patterns.tsv with the host's ICU:
// c++ -std=c++17 tools/make_date_patterns.cpp $(pkg-config --cflags --libs icu-i18n icu-uc) -o /tmp/sw2ps2-date-patterns
// /tmp/sw2ps2-date-patterns > romfs/date-patterns.tsv
#include <unicode/dtptngen.h>
#include <unicode/smpdtfmt.h>
#include <unicode/timezone.h>

#include <iostream>
#include <map>
#include <memory>
#include <string>

int main() {
  int32_t count = 0;
  const auto* locales = icu::Locale::getAvailableLocales(count);
  std::map<std::string, std::string> patterns;
  for (int32_t i = 0; i < count; i++) {
    UErrorCode status = U_ZERO_ERROR;
    const auto tag = locales[i].toLanguageTag<std::string>(status);
    auto locale = locales[i];
    locale.setKeywordValue("calendar", "gregorian", status);
    locale.setKeywordValue("numbers", "latn", status);
    std::unique_ptr<icu::DateTimePatternGenerator> generator(
        icu::DateTimePatternGenerator::createInstance(locale, status));
    if (U_FAILURE(status) || !generator) return 1;
    icu::SimpleDateFormat formatter(generator->getBestPattern("yyyyMMdd", status), locale, status);
    if (U_FAILURE(status)) return 1;
    formatter.setTimeZone(*icu::TimeZone::getGMT());
    icu::UnicodeString example;
    formatter.format(2142806400000.0, example);  // 2037-11-26 UTC
    std::string pattern;
    example.toUTF8String(pattern);
    for (const auto& [digits, field] : std::initializer_list<std::pair<std::string, std::string>>{
             {"2037", "%Y"}, {"11", "%m"}, {"26", "%d"}}) {
      const auto pos = pattern.find(digits);
      if (pos == std::string::npos) {
        std::cerr << tag << ": unexpected date: " << pattern << '\n';
        return 1;
      }
      pattern.replace(pos, digits.size(), field);
    }
    patterns.emplace(tag, pattern);
  }
  std::cout << "# Numeric Gregorian date patterns from ICU " U_ICU_VERSION "\n";
  for (const auto& [tag, pattern] : patterns) std::cout << tag << '\t' << pattern << '\n';
}
