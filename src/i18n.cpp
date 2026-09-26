// Copyright (C) Mihai Preda

#include "i18n.h"

#include <cstring>

namespace {

// The "-lang" value from the command line: nullptr when absent, "" when given without a value.
// Args splits the command line into "-key value" pairs the same way.
const char *commandLineLang(int argc, char **argv) {
  const char *lang = nullptr;
  for (int i = 1; i < argc; ++i) {
    if (argv[i] && !strcmp(argv[i], "-lang")) {
      lang = (i + 1 < argc && argv[i + 1] && argv[i + 1][0] != '-') ? argv[i + 1] : "";
    }
  }
  return lang;
}

} // namespace

#if !PRPLL_NLS

void initI18n(int, char **) noexcept {}

#else

#include <cstdlib>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

// MSVC builds load libintl at run time, so that a missing or broken DLL means English rather than
// a program that cannot start.  Everywhere else libintl is linked (it is part of glibc on Linux).
#ifdef _MSC_VER
#define I18N_DYNAMIC 1
#else
#define I18N_DYNAMIC 0
#include <clocale>
#include <libintl.h>
#endif

#ifndef PRPLL_LOCALEDIR
#define PRPLL_LOCALEDIR "/usr/share/locale"
#endif

namespace fs = std::filesystem;
using std::string;
using std::string_view;

namespace {

const char *const TEXT_DOMAIN = "prpll";

bool enabled = false;
std::mutex cacheMutex;
std::unordered_map<const char *, const char *> cache;

#if I18N_DYNAMIC
using DgettextFn    = char *(*)(const char *, const char *);
using BindFn        = char *(*)(const char *, const char *);
using WBindFn       = wchar_t *(*)(const char *, const wchar_t *);
using SetlocaleFn   = char *(*)(int, const char *);

// LC_MESSAGES as libintl defines it on Windows, where the C runtime has no such category.
constexpr int INTL_LC_MESSAGES = 1729;

DgettextFn dgettextFn = nullptr;
#endif

const char *lookup(const char *msgid) {
#if I18N_DYNAMIC
  return dgettextFn(TEXT_DOMAIN, msgid);
#else
  return dgettext(TEXT_DOMAIN, msgid);
#endif
}

// The printf conversions of s, in order, each from '%' through its conversion letter ("%%" is not one).
// Returns false if s has a '%' that does not start a well-formed conversion.
bool conversions(const char *s, std::vector<string_view>& out) {
  for (const char *p = s; *p; ++p) {
    if (*p != '%') { continue; }
    const char *start = p++;
    if (*p == '%') { continue; }
    while (*p && strchr("-+ #0'", *p)) { ++p; }
    while ((*p >= '0' && *p <= '9') || *p == '*' || *p == '$') { ++p; }
    if (*p == '.') {
      ++p;
      while ((*p >= '0' && *p <= '9') || *p == '*' || *p == '$') { ++p; }
    }
    while (*p && strchr("hlLqjztI0123456789", *p)) { ++p; }
    if (!*p || !strchr("diouxXeEfFgGaAcspn", *p)) { return false; }
    out.emplace_back(start, p + 1 - start);
  }
  return true;
}

// A translation is used only if it has exactly the English conversions (flags, width, precision,
// length and specifier, in the same order), so it cannot misread the printf arguments.
bool sameConversions(const char *english, const char *translated) {
  std::vector<string_view> a, b;
  return conversions(english, a) && conversions(translated, b) && a == b;
}

bool isCLocale(string_view s) { return s.empty() || s == "C" || s == "POSIX"; }

bool asksForEnglish(string_view s) {
  return isCLocale(s) || s.starts_with("C.") || s == "en" || s.starts_with("en_") || s.starts_with("en-");
}

// A language tag such as "es", "es_ES", "es-ES", "sr@latin" or "pt_BR.UTF-8".  gettext builds a file
// path from it, so nothing else is accepted.
bool isValidTag(string_view s) {
  if (s.empty() || s.size() > 64) { return false; }
  for (char c : s) {
    bool const ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '@' || c == '.';
    if (!ok) { return false; }
  }
  return s[0] != '.';
}

// "es-ES" -> "es_ES", and the codeset dropped: "es_ES.UTF-8@euro" -> "es_ES@euro".
string normalizeTag(string_view s) {
  string tag;
  bool inCodeset = false;
  for (char c : s) {
    if (c == '.') { inCodeset = true; continue; }
    if (c == '@') { inCodeset = false; }
    if (!inCodeset) { tag += (c == '-') ? '_' : c; }
  }
  return tag;
}

const char *nonEmptyEnv(const char *name) {
  const char *v = getenv(name);
  return (v && *v) ? v : nullptr;
}

void setEnv(const char *name, const string& value) {
#ifdef _WIN32
  _putenv_s(name, value.c_str());
  SetEnvironmentVariableA(name, value.c_str());
  // libintl reads the environment through its own C runtime, which can be another one than ours and may
  // have copied the environment before this point (msvcrt.dll does so when it is loaded).
  using PutenvFn = int (__cdecl *)(const char *, const char *);
  for (const wchar_t *crt : {L"ucrtbase.dll", L"msvcrt.dll"}) {
    if (HMODULE const m = GetModuleHandleW(crt)) {
      if (auto const putenvF = reinterpret_cast<PutenvFn>(reinterpret_cast<void *>(GetProcAddress(m, "_putenv_s")))) {
        putenvF(name, value.c_str());
      }
    }
  }
#else
  setenv(name, value.c_str(), 1);
#endif
}

fs::path exeDir() {
  std::error_code ec;
#ifdef _WIN32
  std::wstring buf(MAX_PATH, L'\0');
  for (;;) {
    DWORD const n = GetModuleFileNameW(nullptr, buf.data(), DWORD(buf.size()));
    if (n == 0) { return {}; }
    if (n < buf.size()) { buf.resize(n); break; }
    if (buf.size() > 32768) { return {}; }
    buf.resize(buf.size() * 2);
  }
  return fs::path{buf}.parent_path();
#else
  fs::path const exe = fs::read_symlink("/proc/self/exe", ec);
  return ec ? fs::path{} : exe.parent_path();
#endif
}

bool isDir(const fs::path& p) {
  std::error_code ec;
  return !p.empty() && fs::is_directory(p, ec) && !ec;
}

#ifdef _WIN32

UINT originalConsoleCP = 0;

void restoreConsole() {
  if (originalConsoleCP) { SetConsoleOutputCP(originalConsoleCP); }
}

bool isWindows10OrLater() {
  using RtlGetVersionFn = LONG (WINAPI *)(PRTL_OSVERSIONINFOW);
  HMODULE const ntdll = GetModuleHandleW(L"ntdll.dll");
  if (!ntdll) { return false; }
  auto const rtlGetVersion = reinterpret_cast<RtlGetVersionFn>(reinterpret_cast<void *>(GetProcAddress(ntdll, "RtlGetVersion")));
  if (!rtlGetVersion) { return false; }
  RTL_OSVERSIONINFOW info{};
  info.dwOSVersionInfoSize = sizeof(info);
  return rtlGetVersion(&info) == 0 && info.dwMajorVersion >= 10;
}

// The Windows display language as a gettext tag, e.g. "es_ES"; empty if it can't be read.
string displayLanguage() {
  wchar_t name[LOCALE_NAME_MAX_LENGTH] = {};
  if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), name, LOCALE_NAME_MAX_LENGTH, 0)) { return {}; }
  string tag;
  for (const wchar_t *p = name; *p; ++p) {
    if (*p > 0x7f) { return {}; }
    tag += char(*p);
  }
  return isValidTag(tag) ? normalizeTag(tag) : string{};
}

// The path in the ANSI code page, or empty if it has characters that code page can't represent.
string ansiPath(const fs::path& p) {
  std::wstring const w = p.wstring();
  if (w.empty()) { return {}; }
  BOOL usedDefault = FALSE;
  int const n = WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(), -1, nullptr, 0, nullptr, &usedDefault);
  if (n <= 0 || usedDefault) { return {}; }
  string s(n, '\0');
  if (WideCharToMultiByte(CP_ACP, WC_NO_BEST_FIT_CHARS, w.c_str(), -1, s.data(), n, nullptr, &usedDefault) != n || usedDefault) { return {}; }
  s.resize(n - 1);
  return s;
}

#endif

#if I18N_DYNAMIC

template<typename F> F intlProc(HMODULE dll, const char *name) {
  FARPROC p = GetProcAddress(dll, (string{"libintl_"} + name).c_str());
  if (!p) { p = GetProcAddress(dll, name); }
  return reinterpret_cast<F>(reinterpret_cast<void *>(p));
}

// Loads libintl (intl-8.dll, as vcpkg names it, or libintl-8.dll) from the exe's own directory.
HMODULE loadIntl(const fs::path& dir) {
  DWORD oldMode = 0;
  SetThreadErrorMode(SEM_FAILCRITICALERRORS | SEM_NOOPENFILEERRORBOX, &oldMode);
  HMODULE dll = nullptr;
  for (const wchar_t *name : {L"intl-8.dll", L"libintl-8.dll"}) {
    dll = LoadLibraryExW((dir / name).wstring().c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (dll) { break; }
  }
  SetThreadErrorMode(oldMode, nullptr);
  return dll;
}

bool initWindows(const char *lang) {
  if (!isWindows10OrLater()) { return false; }

  string language;
  if (lang) {
    language = lang;
  } else if (!nonEmptyEnv("LANGUAGE") && !nonEmptyEnv("LC_ALL") && !nonEmptyEnv("LC_MESSAGES") && !nonEmptyEnv("LANG")) {
    language = displayLanguage();
    if (language.empty() || asksForEnglish(language)) { return false; }
  }

  fs::path const dir = exeDir();
  fs::path const localeDir = dir / "locale";
  if (dir.empty() || !isDir(localeDir)) { return false; }

  HMODULE const dll = loadIntl(dir);
  if (!dll) { return false; }
  if (!language.empty()) { setEnv("LANGUAGE", language); }
  // libintl ignores LANGUAGE under a "C" locale from LC_ALL / LC_MESSAGES / LANG; -lang wins over those.
  if (lang) {
    if (nonEmptyEnv("LC_ALL")) { setEnv("LC_ALL", ""); }
    setEnv("LC_MESSAGES", lang);
  }
  auto const dgettextF      = intlProc<DgettextFn>(dll, "dgettext");
  auto const bindF          = intlProc<BindFn>(dll, "bindtextdomain");
  auto const wbindF         = intlProc<WBindFn>(dll, "wbindtextdomain");
  auto const codesetF       = intlProc<BindFn>(dll, "bind_textdomain_codeset");
  auto const setlocaleF     = intlProc<SetlocaleFn>(dll, "setlocale");
  if (!dgettextF || !bindF || !codesetF || !setlocaleF) { return false; }

  if (!setlocaleF(INTL_LC_MESSAGES, lang ? lang : "")) { return false; }

  if (wbindF) {
    if (!wbindF(TEXT_DOMAIN, localeDir.wstring().c_str())) { return false; }
  } else {
    string const ansi = ansiPath(localeDir);
    if (ansi.empty() || !bindF(TEXT_DOMAIN, ansi.c_str())) { return false; }
  }
  if (!codesetF(TEXT_DOMAIN, "UTF-8")) { return false; }

  // The catalog text is UTF-8; the console has to show it as such (as gpuowl-N.log stores it).
  // No console (output redirected, a service) leaves nothing to change.
  UINT const cp = GetConsoleOutputCP();
  if (cp && cp != CP_UTF8) {
    if (!SetConsoleOutputCP(CP_UTF8)) { return false; }
    originalConsoleCP = cp;
    atexit(restoreConsole);
  }

  dgettextFn = dgettextF;
  return true;
}

#else

// The language asked for through the environment, in gettext's order.  Empty if none, or if the
// locale is explicitly C/POSIX, which asks for the untranslated messages.
string envLanguage() {
  const char *locale = nullptr;
  for (const char *name : {"LC_ALL", "LC_MESSAGES", "LANG"}) {
    if ((locale = nonEmptyEnv(name))) { break; }
  }
  if (locale && isCLocale(locale)) { return {}; }
  if (const char *v = nonEmptyEnv("LANGUAGE")) { return v; }
  return (locale && isValidTag(locale)) ? normalizeTag(locale) : string{};
}

bool initLinked(const char *lang) {
#ifdef _WIN32
  if (!isWindows10OrLater()) { return false; }
  if (!lang && !nonEmptyEnv("LANGUAGE") && !nonEmptyEnv("LC_ALL") && !nonEmptyEnv("LC_MESSAGES") && !nonEmptyEnv("LANG")) {
    string const ui = displayLanguage();
    if (ui.empty() || asksForEnglish(ui)) { return false; }
    setEnv("LANGUAGE", ui);
  }
#endif

  if (lang) { setEnv("LANGUAGE", lang); }

  // Only the message catalog category: printf, strtod and sscanf keep the "C" locale, so savefile
  // headers, results and the numbers read from the command line and config.txt keep a '.'.
  const char *const current = setlocale(LC_MESSAGES, "");

  // gettext ignores LANGUAGE while the message locale is "C", which is what an unset LANG or a
  // locale that is not installed gives.  A C.UTF-8 (or en_US.UTF-8) message locale lets it through.
  if (!current || isCLocale(current)) {
    string const wanted = lang ? string{lang} : envLanguage();
    if (wanted.empty()) { return false; }
    if (!nonEmptyEnv("LANGUAGE")) { setEnv("LANGUAGE", wanted); }
    if (!setlocale(LC_MESSAGES, "C.UTF-8") && !setlocale(LC_MESSAGES, "en_US.UTF-8")) { return false; }
  }

  fs::path localeDir = exeDir();
  if (!localeDir.empty()) { localeDir /= "locale"; }
  if (!isDir(localeDir)) {
#ifdef _WIN32
    return false;
#else
    localeDir = PRPLL_LOCALEDIR;
    if (!isDir(localeDir)) { return false; }
#endif
  }

#ifdef _WIN32
  string const dirName = ansiPath(localeDir);
  if (dirName.empty()) { return false; }
#else
  string const dirName = localeDir.string();
#endif
  if (!bindtextdomain(TEXT_DOMAIN, dirName.c_str())) { return false; }
  if (!bind_textdomain_codeset(TEXT_DOMAIN, "UTF-8")) { return false; }

#ifdef _WIN32
  UINT const cp = GetConsoleOutputCP();
  if (cp && cp != CP_UTF8) {
    if (!SetConsoleOutputCP(CP_UTF8)) { return false; }
    originalConsoleCP = cp;
    atexit(restoreConsole);
  }
#endif
  return true;
}

#endif

} // namespace

void initI18n(int argc, char **argv) noexcept {
  try {
    const char *lang = commandLineLang(argc, argv);
    string tag;
    if (lang) {
      if (!isValidTag(lang) || asksForEnglish(lang)) { return; }
      tag = normalizeTag(lang);
      if (tag.empty()) { return; }
      lang = tag.c_str();
    }
#if I18N_DYNAMIC
    enabled = initWindows(lang);
#else
    enabled = initLinked(lang);
#endif
  } catch (...) {
    enabled = false;
  }
}

const char *tr(const char *msgid) noexcept {
  if (!enabled || !msgid || !*msgid) { return msgid; }
  try {
    std::lock_guard const lock{cacheMutex};
    if (auto it = cache.find(msgid); it != cache.end()) { return it->second; }
    const char *s = lookup(msgid);
    if (!s || !sameConversions(msgid, s)) { s = msgid; }
    cache.emplace(msgid, s);
    return s;
  } catch (...) {
    return msgid;
  }
}

#endif
