// Copyright (C) Mihai Preda

#pragma once

// Translation of the user-facing messages marked with _(), using GNU gettext catalogs (see po/).
// Nothing here throws or stops the program: whatever goes wrong (no catalog, no locale, no libintl,
// a translation whose printf conversions differ from the English) leaves the English text.

#ifndef PRPLL_NLS
#define PRPLL_NLS 0
#endif

#ifdef __GNUC__
#define I18N_FORMAT_ARG __attribute__((format_arg(1)))
#else
#define I18N_FORMAT_ARG
#endif

// Chooses the language (-lang on the command line, else the environment / Windows display language)
// and loads its catalog.  Call once, first thing in main(), before anything is printed.
void initI18n(int argc, char **argv) noexcept;

#if PRPLL_NLS
const char *tr(const char *msgid) noexcept I18N_FORMAT_ARG;
#else
inline const char *tr(const char *msgid) noexcept I18N_FORMAT_ARG;
inline const char *tr(const char *msgid) noexcept { return msgid; }
#endif

#define _(s) tr(s)
