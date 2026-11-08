// SPDX-License-Identifier: GPL-2.0-or-later
// SPMCore - 10-Language Localization Module
#include "spm/spm_core.h"
#include "spm_localization_data.inl"
#include <cstring>
#include <string>
#include <unordered_map>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {

struct TranslationTable {
    const TranslationEntry* entries;
    std::unordered_map<std::string, const char*> lookup;
};

static std::unordered_map<std::string, TranslationTable> s_tables;
static bool s_initialized = false;

static void InitLanguageTable(const char* langCode, const TranslationEntry* entries) {
    if (!langCode || !entries) return;
    TranslationTable& table = s_tables[langCode];
    table.entries = entries;
    for (size_t i = 0; entries[i].key != nullptr; ++i) {
        if (entries[i].val != nullptr) {
            table.lookup[entries[i].key] = entries[i].val;
        }
    }
}

static void EnsureInitialized() {
    if (s_initialized) return;

    InitLanguageTable("es_419", kTranslations_es_419);
    InitLanguageTable("es_ES",  kTranslations_es_ES);
    InitLanguageTable("en",     kTranslations_en);
    InitLanguageTable("pt_BR",  kTranslations_pt_BR);
    InitLanguageTable("fr",     kTranslations_fr);
    InitLanguageTable("de",     kTranslations_de);
    InitLanguageTable("it",     kTranslations_it);
    InitLanguageTable("ru",     kTranslations_ru);
    InitLanguageTable("ja",     kTranslations_ja);
    InitLanguageTable("zh_CN",  kTranslations_zh_CN);

    s_initialized = true;
}

} // namespace

const char* SPM_DetectSystemLanguage(void) {
#ifdef _WIN32
    LANGID langId = GetUserDefaultUILanguage();
    WORD primary = PRIMARYLANGID(langId);
    WORD sub = SUBLANGID(langId);

    switch (primary) {
        case LANG_SPANISH:
            return (sub == SUBLANG_SPANISH || sub == SUBLANG_SPANISH_MODERN) ? "es_ES" : "es_419";
        case LANG_PORTUGUESE:
            return "pt_BR";
        case LANG_FRENCH:
            return "fr";
        case LANG_GERMAN:
            return "de";
        case LANG_ITALIAN:
            return "it";
        case LANG_RUSSIAN:
            return "ru";
        case LANG_JAPANESE:
            return "ja";
        case LANG_CHINESE:
            return "zh_CN";
        case LANG_ENGLISH:
        default:
            return "en";
    }
#else
    const char* envLang = getenv("LANG");
    if (!envLang) envLang = getenv("LC_ALL");
    if (!envLang) return "en";

    if (strstr(envLang, "es_ES")) return "es_ES";
    if (strstr(envLang, "es_"))  return "es_419";
    if (strstr(envLang, "pt_BR")) return "pt_BR";
    if (strstr(envLang, "pt_"))  return "pt_BR";
    if (strstr(envLang, "fr_"))  return "fr";
    if (strstr(envLang, "de_"))  return "de";
    if (strstr(envLang, "it_"))  return "it";
    if (strstr(envLang, "ru_"))  return "ru";
    if (strstr(envLang, "ja_"))  return "ja";
    if (strstr(envLang, "zh_"))  return "zh_CN";
    return "en";
#endif
}

const char* SPM_GetLocalizedString(const char* langCode, const char* key, const char* fallback) {
    if (!key || key[0] == '\0') return fallback ? fallback : "";

    EnsureInitialized();

    std::string targetLang = (langCode && strcmp(langCode, "auto") != 0 && langCode[0] != '\0')
        ? langCode
        : SPM_DetectSystemLanguage();

    auto itTable = s_tables.find(targetLang);
    if (itTable != s_tables.end()) {
        auto itVal = itTable->second.lookup.find(key);
        if (itVal != itTable->second.lookup.end()) {
            return itVal->second;
        }
    }

    // Fallback 1: English
    if (targetLang != "en") {
        auto itEn = s_tables.find("en");
        if (itEn != s_tables.end()) {
            auto itVal = itEn->second.lookup.find(key);
            if (itVal != itEn->second.lookup.end()) {
                return itVal->second;
            }
        }
    }

    // Fallback 2: Provided fallback or key name
    return fallback ? fallback : key;
}
