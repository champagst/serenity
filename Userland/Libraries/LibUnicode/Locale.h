/*
 * Copyright (c) 2021, Tim Flynn <trflynn89@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#pragma once

#include <AK/Optional.h>
#include <AK/String.h>
#include <AK/StringView.h>
#include <AK/Variant.h>
#include <AK/Vector.h>
#include <LibUnicode/Forward.h>

namespace Unicode {

struct LanguageID {
    String to_string() const;

    bool is_root { false };
    Optional<String> language {};
    Optional<String> script {};
    Optional<String> region {};
    Vector<String> variants {};
};

struct Keyword {
    String key {};
    Vector<String> types {};
};

struct LocaleExtension {
    Vector<String> attributes {};
    Vector<Keyword> keywords {};
};

struct TransformedField {
    String key;
    Vector<String> values {};
};

struct TransformedExtension {
    Optional<LanguageID> language {};
    Vector<TransformedField> fields {};
};

struct OtherExtension {
    char key {};
    Vector<String> values {};
};

using Extension = Variant<LocaleExtension, TransformedExtension, OtherExtension>;

struct LocaleID {
    String to_string() const;

    template<typename ExtensionType>
    void remove_extension_type()
    {
        auto tmp_extensions = move(extensions);

        for (auto& extension : tmp_extensions) {
            if (!extension.has<ExtensionType>())
                extensions.append(move(extension));
        }
    }

    LanguageID language_id {};
    Vector<Extension> extensions {};
    Vector<String> private_use_extensions {};
};

// Note: These methods only verify that the provided strings match the EBNF grammar of the
// Unicode identifier subtag (i.e. no validation is done that the tags actually exist).
bool is_unicode_language_subtag(StringView);
bool is_unicode_script_subtag(StringView);
bool is_unicode_region_subtag(StringView);
bool is_unicode_variant_subtag(StringView);
bool is_type_identifier(StringView);

Optional<LanguageID> parse_unicode_language_id(StringView);
Optional<LocaleID> parse_unicode_locale_id(StringView);
Optional<String> canonicalize_unicode_locale_id(LocaleID&);

String const& default_locale();
bool is_locale_available(StringView locale);

Optional<StringView> get_locale_language_mapping(StringView locale, StringView language);
Optional<StringView> get_locale_territory_mapping(StringView locale, StringView territory);
Optional<StringView> get_locale_script_mapping(StringView locale, StringView script);
Optional<StringView> get_locale_currency_mapping(StringView locale, StringView currency);

Optional<StringView> resolve_language_alias(StringView language);
Optional<StringView> resolve_territory_alias(StringView territory);
Optional<StringView> resolve_script_tag_alias(StringView script_tag);
Optional<StringView> resolve_variant_alias(StringView variant);
Optional<StringView> resolve_subdivision_alias(StringView subdivision);

String resolve_most_likely_territory(LanguageID const& language_id, StringView territory_alias);

}
