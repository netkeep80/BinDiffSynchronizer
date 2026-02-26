// Debug script to investigate the obj_insert sorted-key bug.
// Inserts "c"=3, "a"=1, "b"=2 and inspects internal state after each insert.

#include <cstdio>
#include <cstring>
#include "../pjson.h"

// ---------------------------------------------------------------------------
// Helper: print the raw state of the object's pairs array.
// We access the fields directly through the public/internal layout.
// ---------------------------------------------------------------------------
void print_object_state(pjson_data& d, const char* label)
{
    printf("\n--- %s ---\n", label);

    unsigned sz         = d.payload.object_val.size;
    unsigned pairs_slot = d.payload.object_val.pairs_slot;

    printf("  size=%u  pairs_slot=%u\n", sz, pairs_slot);

    if (pairs_slot == 0) {
        printf("  (no pairs array allocated)\n");
        return;
    }

    unsigned cap = AddressManager<pjson_kv_pair>::GetCount(pairs_slot);
    printf("  cap=%u\n", cap);

    for (unsigned i = 0; i < sz; i++) {
        pjson_kv_pair& pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(pairs_slot, i);

        unsigned chars_slot = pair.key.chars.addr();
        const char* keystr = (chars_slot != 0)
                             ? &AddressManager<char>::GetArrayElement(chars_slot, 0)
                             : "(null)";

        printf("  pairs[%u]: chars_slot=%u  key=\"%s\"  value.type=%u\n",
               i, chars_slot, keystr,
               static_cast<unsigned>(pair.value.type));
    }
}

// ---------------------------------------------------------------------------
// Helper: manually run lower_bound and report what it finds for a key.
// ---------------------------------------------------------------------------
void trace_lower_bound(pjson_data& d, const char* search_key)
{
    printf("\n  [lower_bound trace for \"%s\"]\n", search_key);
    unsigned sz         = d.payload.object_val.size;
    unsigned pairs_slot = d.payload.object_val.pairs_slot;

    unsigned lo = 0, hi = sz;
    while (lo < hi) {
        unsigned mid = (lo + hi) / 2;
        pjson_kv_pair& pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(pairs_slot, mid);
        unsigned chars_slot = pair.key.chars.addr();
        const char* k = (chars_slot != 0)
                        ? &AddressManager<char>::GetArrayElement(chars_slot, 0)
                        : "";
        int cmp = std::strcmp(k, search_key);
        printf("    lo=%u hi=%u mid=%u key@mid=\"%s\" cmp=%d\n",
               lo, hi, mid, k, cmp);
        if (cmp < 0) lo = mid + 1;
        else         hi = mid;
    }
    printf("    => idx=%u\n", lo);

    if (lo < sz) {
        pjson_kv_pair& pair =
            AddressManager<pjson_kv_pair>::GetArrayElement(pairs_slot, lo);
        unsigned chars_slot = pair.key.chars.addr();
        const char* k = (chars_slot != 0)
                        ? &AddressManager<char>::GetArrayElement(chars_slot, 0)
                        : "(null)";
        printf("    pairs[%u].key = \"%s\"  chars_slot=%u\n", lo, k, chars_slot);
    } else {
        printf("    (idx >= size, would append at end)\n");
    }
}

int main()
{
    // -----------------------------------------------------------------------
    // Set up an empty persistent object.
    // -----------------------------------------------------------------------
    pjson_data d{};
    d.type = pjson_type::null;
    d.payload.uint_val = 0;
    pjson v(d);
    v.set_object();

    printf("Initial state:\n");
    print_object_state(d, "empty object");

    // -----------------------------------------------------------------------
    // Insert "c" = 3
    // -----------------------------------------------------------------------
    printf("\n=== Insert \"c\"=3 ===\n");
    pjson(v.obj_insert("c")).set_int(3);
    print_object_state(d, "after insert \"c\"");

    // -----------------------------------------------------------------------
    // Insert "a" = 1
    // -----------------------------------------------------------------------
    printf("\n=== Insert \"a\"=1 ===\n");
    pjson(v.obj_insert("a")).set_int(1);
    print_object_state(d, "after insert \"a\"");

    // -----------------------------------------------------------------------
    // Trace lower_bound for "b" BEFORE the third insert, to show the state.
    // -----------------------------------------------------------------------
    printf("\n=== Before inserting \"b\": tracing lower_bound(\"b\") ===\n");
    trace_lower_bound(d, "b");

    // -----------------------------------------------------------------------
    // Insert "b" = 2
    // The bug hypothesis: inserting "b" at index 1 shifts "c" from [1] to [2]
    // (shallow copy — both [1] and [2] share the same chars_slot for "c").
    // Then _assign_key for the new "b" at [1] calls chars.DeleteArray() on
    // the slot that is now ALSO held by [2], corrupting "c".
    // -----------------------------------------------------------------------
    printf("\n=== Insert \"b\"=2 ===\n");

    // Capture the chars_slot of "c" (currently at pairs[1]) BEFORE the insert.
    {
        unsigned pairs_slot = d.payload.object_val.pairs_slot;
        pjson_kv_pair& pair_c =
            AddressManager<pjson_kv_pair>::GetArrayElement(pairs_slot, 1);
        printf("  Before insert: pairs[1] (\"c\") chars_slot = %u\n",
               pair_c.key.chars.addr());
    }

    pjson(v.obj_insert("b")).set_int(2);

    // Immediately after: show all pairs including chars_slots.
    {
        unsigned sz         = d.payload.object_val.size;
        unsigned pairs_slot = d.payload.object_val.pairs_slot;
        printf("  After insert: size=%u  pairs_slot=%u\n", sz, pairs_slot);
        for (unsigned i = 0; i < sz; i++) {
            pjson_kv_pair& pair =
                AddressManager<pjson_kv_pair>::GetArrayElement(pairs_slot, i);
            unsigned chars_slot = pair.key.chars.addr();
            // Try to read the key string — if chars_slot was freed this may
            // return garbage or crash (which itself confirms the bug).
            const char* keystr = "(null)";
            if (chars_slot != 0) {
                // Read through the address manager — if slot was deleted the
                // slot number may have been reused or left as a dangling entry.
                keystr = &AddressManager<char>::GetArrayElement(chars_slot, 0);
            }
            printf("  After insert: pairs[%u] chars_slot=%u key=\"%s\"\n",
                   i, chars_slot, keystr);
        }
    }

    print_object_state(d, "after insert \"b\"");

    // -----------------------------------------------------------------------
    // obj_find for each key
    // -----------------------------------------------------------------------
    printf("\n=== obj_find results ===\n");
    for (const char* key : {"a", "b", "c"}) {
        trace_lower_bound(d, key);
        pjson_data* found = v.obj_find(key);
        if (found) {
            printf("  obj_find(\"%s\") -> FOUND  value=%lld\n",
                   key, static_cast<long long>(pjson(*found).get_int()));
        } else {
            printf("  obj_find(\"%s\") -> NOT FOUND\n", key);
        }
    }

    // -----------------------------------------------------------------------
    // Show all raw chars_slots for all 3 pairs to confirm sharing.
    // If pairs[1] and pairs[2] show the SAME chars_slot (or [2] shows 0 or
    // garbage after the insert), the shallow-copy/free bug is confirmed.
    // -----------------------------------------------------------------------
    printf("\n=== Final raw chars_slot dump ===\n");
    {
        unsigned sz         = d.payload.object_val.size;
        unsigned pairs_slot = d.payload.object_val.pairs_slot;
        for (unsigned i = 0; i < sz; i++) {
            pjson_kv_pair& pair =
                AddressManager<pjson_kv_pair>::GetArrayElement(pairs_slot, i);
            unsigned chars_slot = pair.key.chars.addr();
            printf("  pairs[%u].key.chars_slot = %u\n", i, chars_slot);
        }
    }

    v.free();
    printf("\nDone.\n");
    return 0;
}
