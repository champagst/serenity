/*
 * Copyright (c) 2021, Tim Flynn <trflynn89@pm.me>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/AllOf.h>
#include <AK/AnyOf.h>
#include <AK/CharacterTypes.h>
#include <AK/QuickSort.h>
#include <AK/TypeCasts.h>
#include <LibJS/Runtime/Array.h>
#include <LibJS/Runtime/GlobalObject.h>
#include <LibJS/Runtime/Intl/AbstractOperations.h>
#include <LibJS/Runtime/Intl/Locale.h>
#include <LibUnicode/Locale.h>

namespace JS::Intl {

// 6.2.2 IsStructurallyValidLanguageTag ( locale ), https://tc39.es/ecma402/#sec-isstructurallyvalidlanguagetag
Optional<Unicode::LocaleID> is_structurally_valid_language_tag(StringView locale)
{
    auto contains_duplicate_variant = [](auto& variants) {
        if (variants.is_empty())
            return false;

        quick_sort(variants);

        for (size_t i = 0; i < variants.size() - 1; ++i) {
            if (variants[i].equals_ignoring_case(variants[i + 1]))
                return true;
        }

        return false;
    };

    // IsStructurallyValidLanguageTag returns true if all of the following conditions hold, false otherwise:

    // locale can be generated from the EBNF grammar for unicode_locale_id in Unicode Technical Standard #35 LDML § 3.2 Unicode Locale Identifier;
    auto locale_id = Unicode::parse_unicode_locale_id(locale);
    if (!locale_id.has_value())
        return {};

    // locale does not use any of the backwards compatibility syntax described in Unicode Technical Standard #35 LDML § 3.3 BCP 47 Conformance;
    // https://unicode.org/reports/tr35/#BCP_47_Conformance
    if (locale.contains('_') || locale_id->language_id.is_root || !locale_id->language_id.language.has_value())
        return {};

    // the unicode_language_id within locale contains no duplicate unicode_variant_subtag subtags; and
    if (contains_duplicate_variant(locale_id->language_id.variants))
        return {};

    // if locale contains an extensions* component, that component
    Vector<char> unique_keys;
    for (auto& extension : locale_id->extensions) {
        // does not contain any other_extensions components with duplicate [alphanum-[tTuUxX]] subtags,
        // contains at most one unicode_locale_extensions component,
        // contains at most one transformed_extensions component, and
        char key = extension.visit(
            [](Unicode::LocaleExtension const&) { return 'u'; },
            [](Unicode::TransformedExtension const&) { return 't'; },
            [](Unicode::OtherExtension const& ext) { return static_cast<char>(to_ascii_lowercase(ext.key)); });

        if (unique_keys.contains_slow(key))
            return {};
        unique_keys.append(key);

        // if a transformed_extensions component that contains a tlang component is present, then
        // the tlang component contains no duplicate unicode_variant_subtag subtags.
        if (auto* transformed = extension.get_pointer<Unicode::TransformedExtension>()) {
            auto& language = transformed->language;
            if (language.has_value() && contains_duplicate_variant(language->variants))
                return {};
        }
    }

    return locale_id;
}

// 6.2.3 CanonicalizeUnicodeLocaleId ( locale ), https://tc39.es/ecma402/#sec-canonicalizeunicodelocaleid
String canonicalize_unicode_locale_id(Unicode::LocaleID& locale)
{
    // Note: This implementation differs from the spec in how Step 3 is implemented. The spec assumes
    // the input to this method is a string, and is written such that operations are performed on parts
    // of that string. LibUnicode gives us the parsed locale in a structure, so we can mutate that
    // structure directly. From a footnote in the spec:
    //
    // The third step of this algorithm ensures that a Unicode locale extension sequence in the
    // returned language tag contains:
    //     * only the first instance of any attribute duplicated in the input, and
    //     * only the first keyword for a given key in the input.
    for (auto& extension : locale.extensions) {
        if (!extension.has<Unicode::LocaleExtension>())
            continue;

        auto& locale_extension = extension.get<Unicode::LocaleExtension>();

        auto attributes = move(locale_extension.attributes);
        for (auto& attribute : attributes) {
            if (!locale_extension.attributes.contains_slow(attribute))
                locale_extension.attributes.append(move(attribute));
        }

        auto keywords = move(locale_extension.keywords);
        for (auto& keyword : keywords) {
            if (!any_of(locale_extension.keywords, [&](auto const& k) { return k.key == keyword.key; }))
                locale_extension.keywords.append(move(keyword));
        }

        break;
    }

    // 1. Let localeId be the string locale after performing the algorithm to transform it to canonical syntax per Unicode Technical Standard #35 LDML § 3.2.1 Canonical Unicode Locale Identifiers.
    // 2. Let localeId be the string localeId after performing the algorithm to transform it to canonical form.
    auto locale_id = Unicode::canonicalize_unicode_locale_id(locale);
    VERIFY(locale_id.has_value());

    // 4. Return localeId.
    return locale_id.release_value();
}

// 6.3.1 IsWellFormedCurrencyCode ( currency ), https://tc39.es/ecma402/#sec-canonicalcodefordisplaynames
static bool is_well_formed_currency_code(StringView currency)
{
    // 1. Let normalized be the result of mapping currency to upper case as described in 6.1.
    // 2. If the number of elements in normalized is not 3, return false.
    if (currency.length() != 3)
        return false;

    // 3. If normalized contains any character that is not in the range "A" to "Z" (U+0041 to U+005A), return false.
    if (!all_of(currency, is_ascii_alpha))
        return false;

    // 4. Return true.
    return true;
}

// 9.2.1 CanonicalizeLocaleList ( locales ), https://tc39.es/ecma402/#sec-canonicalizelocalelist
Vector<String> canonicalize_locale_list(GlobalObject& global_object, Value locales)
{
    auto& vm = global_object.vm();

    // 1. If locales is undefined, then
    if (locales.is_undefined()) {
        // a. Return a new empty List.
        return {};
    }

    // 2. Let seen be a new empty List.
    Vector<String> seen;

    Object* object = nullptr;
    // 3. If Type(locales) is String or Type(locales) is Object and locales has an [[InitializedLocale]] internal slot, then
    if (locales.is_string() || (locales.is_object() && is<Locale>(locales.as_object()))) {
        // a. Let O be CreateArrayFromList(« locales »).
        object = Array::create_from(global_object, { locales });
    }
    // 4. Else,
    else {
        // a. Let O be ? ToObject(locales).
        object = locales.to_object(global_object);
        if (vm.exception())
            return {};
    }

    // 5. Let len be ? ToLength(? Get(O, "length")).
    auto length_value = object->get(vm.names.length);
    if (vm.exception())
        return {};
    auto length = length_value.to_length(global_object);
    if (vm.exception())
        return {};

    // 6. Let k be 0.
    // 7. Repeat, while k < len,
    for (size_t k = 0; k < length; ++k) {
        // a. Let Pk be ToString(k).
        auto property_key = PropertyName { k };

        // b. Let kPresent be ? HasProperty(O, Pk).
        auto key_present = object->has_property(property_key);
        if (vm.exception())
            return {};

        // c. If kPresent is true, then
        if (key_present) {
            // i. Let kValue be ? Get(O, Pk).
            auto key_value = object->get(property_key);
            if (vm.exception())
                return {};

            // ii. If Type(kValue) is not String or Object, throw a TypeError exception.
            if (!key_value.is_string() && !key_value.is_object()) {
                vm.throw_exception<TypeError>(global_object, ErrorType::NotAnObjectOrString, key_value.to_string_without_side_effects());
                return {};
            }

            String tag;

            // iii. If Type(kValue) is Object and kValue has an [[InitializedLocale]] internal slot, then
            if (key_value.is_object() && is<Locale>(key_value.as_object())) {
                // 1. Let tag be kValue.[[Locale]].
                tag = static_cast<Locale const&>(key_value.as_object()).locale();
            }
            // iv. Else,
            else {
                // 1. Let tag be ? ToString(kValue).
                tag = key_value.to_string(global_object);
                if (vm.exception())
                    return {};
            }

            // v. If IsStructurallyValidLanguageTag(tag) is false, throw a RangeError exception.
            auto locale_id = is_structurally_valid_language_tag(tag);
            if (!locale_id.has_value()) {
                vm.throw_exception<RangeError>(global_object, ErrorType::IntlInvalidLanguageTag, tag);
                return {};
            }

            // vi. Let canonicalizedTag be CanonicalizeUnicodeLocaleId(tag).
            auto canonicalized_tag = JS::Intl::canonicalize_unicode_locale_id(*locale_id);

            // vii. If canonicalizedTag is not an element of seen, append canonicalizedTag as the last element of seen.
            if (!seen.contains_slow(canonicalized_tag))
                seen.append(move(canonicalized_tag));
        }

        // d. Increase k by 1.
    }

    return seen;
}

// 9.2.2 BestAvailableLocale ( availableLocales, locale ), https://tc39.es/ecma402/#sec-bestavailablelocale
static Optional<String> best_available_locale(StringView const& locale)
{
    // 1. Let candidate be locale.
    StringView candidate = locale;

    // 2. Repeat,
    while (true) {
        // a. If availableLocales contains an element equal to candidate, return candidate.
        if (Unicode::is_locale_available(candidate))
            return candidate;

        // b. Let pos be the character index of the last occurrence of "-" (U+002D) within candidate. If that character does not occur, return undefined.
        auto pos = candidate.find_last('-');
        if (!pos.has_value())
            return {};

        // c. If pos ≥ 2 and the character "-" occurs at index pos-2 of candidate, decrease pos by 2.
        if ((*pos >= 2) && (candidate[*pos - 2] == '-'))
            pos = *pos - 2;

        // d. Let candidate be the substring of candidate from position 0, inclusive, to position pos, exclusive.
        candidate = candidate.substring_view(0, *pos);
    }
}

struct MatcherResult {
    String locale;
    Vector<Unicode::Extension> extensions {};
};

// 9.2.3 LookupMatcher ( availableLocales, requestedLocales ), https://tc39.es/ecma402/#sec-lookupmatcher
static MatcherResult lookup_matcher(Vector<String> const& requested_locales)
{
    // 1. Let result be a new Record.
    MatcherResult result {};

    // 2. For each element locale of requestedLocales, do
    for (auto const& locale : requested_locales) {
        auto locale_id = Unicode::parse_unicode_locale_id(locale);
        VERIFY(locale_id.has_value());

        auto extensions = move(locale_id->extensions);
        locale_id->private_use_extensions.clear();

        // a. Let noExtensionsLocale be the String value that is locale with any Unicode locale extension sequences removed.
        auto no_extensions_locale = JS::Intl::canonicalize_unicode_locale_id(*locale_id);

        // b. Let availableLocale be BestAvailableLocale(availableLocales, noExtensionsLocale).
        auto available_locale = best_available_locale(no_extensions_locale);

        // c. If availableLocale is not undefined, then
        if (available_locale.has_value()) {
            // i. Set result.[[locale]] to availableLocale.
            result.locale = available_locale.release_value();

            // ii. If locale and noExtensionsLocale are not the same String value, then
            if (locale != no_extensions_locale) {
                // 1. Let extension be the String value consisting of the substring of the Unicode locale extension sequence within locale.
                // 2. Set result.[[extension]] to extension.
                result.extensions = move(extensions);
            }

            // iii. Return result.
            return result;
        }
    }

    // 3. Let defLocale be DefaultLocale().
    // 4. Set result.[[locale]] to defLocale.
    result.locale = Unicode::default_locale();

    // 5. Return result.
    return result;
}

// 9.2.4 BestFitMatcher ( availableLocales, requestedLocales ), https://tc39.es/ecma402/#sec-bestfitmatcher
static MatcherResult best_fit_matcher(Vector<String> const& requested_locales)
{
    // The algorithm is implementation dependent, but should produce results that a typical user of the requested locales would
    // perceive as at least as good as those produced by the LookupMatcher abstract operation.
    return lookup_matcher(requested_locales);
}

// 9.2.6 InsertUnicodeExtensionAndCanonicalize ( locale, extension ), https://tc39.es/ecma402/#sec-insert-unicode-extension-and-canonicalize
String insert_unicode_extension_and_canonicalize(Unicode::LocaleID locale, Unicode::LocaleExtension extension)
{
    // Note: This implementation differs from the spec in how the extension is inserted. The spec assumes
    // the input to this method is a string, and is written such that operations are performed on parts
    // of that string. LibUnicode gives us the parsed locale in a structure, so we can mutate that
    // structure directly.
    locale.extensions.append(move(extension));
    return JS::Intl::canonicalize_unicode_locale_id(locale);
}

// 9.2.7 ResolveLocale ( availableLocales, requestedLocales, options, relevantExtensionKeys, localeData ), https://tc39.es/ecma402/#sec-resolvelocale
LocaleResult resolve_locale(Vector<String> const& requested_locales, LocaleOptions const& options, [[maybe_unused]] Vector<StringView> relevant_extension_keys)
{
    // 1. Let matcher be options.[[localeMatcher]].
    auto const& matcher = options.locale_matcher;
    MatcherResult matcher_result;

    // 2. If matcher is "lookup", then
    if (matcher.is_string() && (matcher.as_string().string() == "lookup"sv)) {
        // a. Let r be LookupMatcher(availableLocales, requestedLocales).
        matcher_result = lookup_matcher(requested_locales);
    }
    // 3. Else,
    else {
        // a. Let r be BestFitMatcher(availableLocales, requestedLocales).
        matcher_result = best_fit_matcher(requested_locales);
    }

    // 4. Let foundLocale be r.[[locale]].
    auto found_locale = move(matcher_result.locale);

    // 5. Let result be a new Record.
    LocaleResult result {};

    // 6. Set result.[[dataLocale]] to foundLocale.

    // FIXME: Currently, the only caller to this method has an empty [[RelevantExtensionKeys]] internal slot,
    //        so this block isn't testable. When a caller has a non-empty slot, implement the below steps.
    //
    // 7. If r has an [[extension]] field, then
    //     a. Let components be ! UnicodeExtensionComponents(r.[[extension]]).
    //     b. Let keywords be components.[[Keywords]].
    // 8. Let supportedExtension be "-u".
    // 9. For each element key of relevantExtensionKeys, do
    //     a. Let foundLocaleData be localeData.[[<foundLocale>]].
    //     b. Assert: Type(foundLocaleData) is Record.
    //     c. Let keyLocaleData be foundLocaleData.[[<key>]].
    //     d. Assert: Type(keyLocaleData) is List.
    //     e. Let value be keyLocaleData[0].
    //     f. Assert: Type(value) is either String or Null.
    //     g. Let supportedExtensionAddition be "".
    //     h. If r has an [[extension]] field, then
    //         i. If keywords contains an element whose [[Key]] is the same as key, then
    //             1. Let entry be the element of keywords whose [[Key]] is the same as key.
    //             2. Let requestedValue be entry.[[Value]].
    //             3. If requestedValue is not the empty String, then
    //                 a. If keyLocaleData contains requestedValue, then
    //                     i. Let value be requestedValue.
    //                     ii. Let supportedExtensionAddition be the string-concatenation of "-", key, "-", and value.
    //             4. Else if keyLocaleData contains "true", then
    //                 a. Let value be "true".
    //                 b. Let supportedExtensionAddition be the string-concatenation of "-" and key.
    //     i. If options has a field [[<key>]], then
    //         i. Let optionsValue be options.[[<key>]].
    //         ii. Assert: Type(optionsValue) is either String, Undefined, or Null.
    //         iii. If Type(optionsValue) is String, then
    //             1. Let optionsValue be the string optionsValue after performing the algorithm steps to transform Unicode extension values to canonical syntax per Unicode Technical Standard #35 LDML § 3.2.1 Canonical Unicode Locale Identifiers, treating key as ukey and optionsValue as uvalue productions.
    //             2. Let optionsValue be the string optionsValue after performing the algorithm steps to replace Unicode extension values with their canonical form per Unicode Technical Standard #35 LDML § 3.2.1 Canonical Unicode Locale Identifiers, treating key as ukey and optionsValue as uvalue productions.
    //             3. If optionsValue is the empty String, then
    //                 a. Let optionsValue be "true".
    //         iv. If keyLocaleData contains optionsValue, then
    //             1. If SameValue(optionsValue, value) is false, then
    //                 a. Let value be optionsValue.
    //                 b. Let supportedExtensionAddition be "".
    //     j. Set result.[[<key>]] to value.
    //     k. Append supportedExtensionAddition to supportedExtension.
    // 10. If the number of elements in supportedExtension is greater than 2, then
    //     a. Let foundLocale be InsertUnicodeExtensionAndCanonicalize(foundLocale, supportedExtension).

    // 11. Set result.[[locale]] to foundLocale.
    result.locale = move(found_locale);

    // 12. Return result.
    return result;
}

// 9.2.12 CoerceOptionsToObject ( options ), https://tc39.es/ecma402/#sec-coerceoptionstoobject
Object* coerce_options_to_object(GlobalObject& global_object, Value options)
{
    // 1. If options is undefined, then
    if (options.is_undefined()) {
        // a. Return ! OrdinaryObjectCreate(null).
        return Object::create(global_object, nullptr);
    }

    // 2. Return ? ToObject(options).
    return options.to_object(global_object);
}

// 9.2.13 GetOption ( options, property, type, values, fallback ), https://tc39.es/ecma402/#sec-getoption
Value get_option(GlobalObject& global_object, Value options, PropertyName const& property, Value::Type type, Vector<StringView> const& values, Fallback fallback)
{
    auto& vm = global_object.vm();

    // 1. Assert: Type(options) is Object.
    VERIFY(options.is_object());

    // 2. Let value be ? Get(options, property).
    auto value = options.get(global_object, property);
    if (vm.exception())
        return {};

    // 3. If value is undefined, return fallback.
    if (value.is_undefined()) {
        return fallback.visit(
            [](Empty) { return js_undefined(); },
            [](bool f) { return Value(f); },
            [&vm](StringView f) { return Value(js_string(vm, f)); });
    }

    // 4. Assert: type is "boolean" or "string".
    VERIFY((type == Value::Type::Boolean) || (type == Value::Type::String));

    // 5. If type is "boolean", then
    if (type == Value::Type::Boolean) {
        // a. Let value be ! ToBoolean(value).
        value = Value(value.to_boolean());
    }
    // 6. If type is "string", then
    else {
        // a. Let value be ? ToString(value).
        value = value.to_primitive_string(global_object);
        if (vm.exception())
            return {};
    }

    // 7. If values is not undefined and values does not contain an element equal to value, throw a RangeError exception.
    if (!values.is_empty()) {
        // Note: Every location in the spec that invokes GetOption with type=boolean also has values=undefined.
        VERIFY(value.is_string());
        if (!values.contains_slow(value.as_string().string())) {
            vm.throw_exception<RangeError>(global_object, ErrorType::OptionIsNotValidValue, value.to_string_without_side_effects(), property.as_string());
            return {};
        }
    }

    // 8. Return value.
    return value;
}

// 12.1.1 CanonicalCodeForDisplayNames ( type, code ), https://tc39.es/ecma402/#sec-canonicalcodefordisplaynames
Value canonical_code_for_display_names(GlobalObject& global_object, DisplayNames::Type type, StringView code)
{
    auto& vm = global_object.vm();

    // 1. If type is "language", then
    if (type == DisplayNames::Type::Language) {
        // a. If code does not match the unicode_language_id production, throw a RangeError exception.
        if (!Unicode::parse_unicode_language_id(code).has_value()) {
            vm.throw_exception<RangeError>(global_object, ErrorType::OptionIsNotValidValue, code, "language"sv);
            return {};
        }

        // b. If IsStructurallyValidLanguageTag(code) is false, throw a RangeError exception.
        auto locale_id = is_structurally_valid_language_tag(code);
        if (!locale_id.has_value()) {
            vm.throw_exception<RangeError>(global_object, ErrorType::IntlInvalidLanguageTag, code);
            return {};
        }

        // c. Set code to CanonicalizeUnicodeLocaleId(code).
        // d. Return code.
        auto canonicalized_tag = JS::Intl::canonicalize_unicode_locale_id(*locale_id);
        return js_string(vm, move(canonicalized_tag));
    }

    // 2. If type is "region", then
    if (type == DisplayNames::Type::Region) {
        // a. If code does not match the unicode_region_subtag production, throw a RangeError exception.
        if (!Unicode::is_unicode_region_subtag(code)) {
            vm.throw_exception<RangeError>(global_object, ErrorType::OptionIsNotValidValue, code, "region"sv);
            return {};
        }

        // b. Let code be the result of mapping code to upper case as described in 6.1.
        // c. Return code.
        return js_string(vm, code.to_uppercase_string());
    }

    // 3. If type is "script", then
    if (type == DisplayNames::Type::Script) {
        // a. If code does not match the unicode_script_subtag production, throw a RangeError exception.
        if (!Unicode::is_unicode_script_subtag(code)) {
            vm.throw_exception<RangeError>(global_object, ErrorType::OptionIsNotValidValue, code, "script"sv);
            return {};
        }

        // b. Let code be the result of mapping the first character in code to upper case, and mapping the second, third, and fourth character in code to lower case, as described in 6.1.
        // c. Return code.
        return js_string(vm, code.to_titlecase_string());
    }

    // 4. Assert: type is "currency".
    VERIFY(type == DisplayNames::Type::Currency);

    // 5. If ! IsWellFormedCurrencyCode(code) is false, throw a RangeError exception.
    if (!is_well_formed_currency_code(code)) {
        vm.throw_exception<RangeError>(global_object, ErrorType::OptionIsNotValidValue, code, "currency"sv);
        return {};
    }

    // 6. Let code be the result of mapping code to upper case as described in 6.1.
    // 7. Return code.
    return js_string(vm, code.to_uppercase_string());
}

}
