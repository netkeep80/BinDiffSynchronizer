// =============================================================================
// Task 3.3.1 — Study: nlohmann::basic_json<> template parameter requirements
// =============================================================================
//
// This file documents the minimum interface required by each template
// parameter of nlohmann::basic_json, based on a reading of json.hpp v3.11.3.
//
// Run: compiled as part of the test suite (or as a standalone experiment).
//
// Template signature:
//   template<
//     template<typename U, typename V, typename... Args> class ObjectType  = std::map,
//     template<typename U, typename... Args>             class ArrayType   = std::vector,
//     class    StringType      = std::string,
//     class    BooleanType     = bool,
//     class    NumberIntegerType   = std::int64_t,
//     class    NumberUnsignedType  = std::uint64_t,
//     class    NumberFloatType     = double,
//     template<typename U>    class AllocatorType = std::allocator,
//     template<typename T, typename SFINAE = void> class JSONSerializer = adl_serializer,
//     class    BinaryType      = std::vector<std::uint8_t>,
//     class    CustomBaseClass = void
//   > class basic_json;
//
// ============================================================================
// StringType requirements (derived from nlohmann/json.hpp)
// ============================================================================
//
// Types / aliases:
//   StringType::value_type                — character type (usually char)
//
// Construction / assignment:
//   StringType()                          — default constructible
//   StringType(const StringType&)         — copy-constructible
//   StringType(StringType&&)              — move-constructible
//   StringType& operator=(const StringType&) — copy-assignable
//   StringType& operator=(StringType&&)      — move-assignable
//   StringType(size_t count, value_type c)   — fill constructor (used in
//       e.g. padding/indent)
//
// Capacity:
//   size_t size() const                   — number of characters
//   bool   empty() const                  — true if size() == 0
//
// Access:
//   value_type& operator[](size_t idx)    — random access (used during
//       Unicode escape processing / serialisation)
//   const value_type& operator[](size_t) const
//
// Modifiers:
//   void  push_back(value_type c)         — append single char (used in
//       lexer token accumulation)
//   StringType& append(const StringType&) — append string (used in
//       serialisation helpers)
//   StringType& append(Iterator, Iterator)— range append
//   StringType& append(const char*, size_t)— buffer append
//   StringType& operator+=(const StringType&)
//   StringType& operator+=(value_type c)
//
// Iterators:
//   iterator       begin()
//   const_iterator begin() const
//   iterator       end()
//   const_iterator end() const
//   // Required for std::back_inserter compatibility used in BSON/CBOR parsing.
//
// Search:
//   size_t find(value_type c, size_t pos = 0) const — first occurrence of c
//   size_t find_first_of(const char* chars, size_t pos) const
//   static const size_t npos                — "not found" sentinel
//
// Sub-string:
//   StringType substr(size_t pos, size_t len = npos) const
//
// Comparison:
//   bool operator==(const StringType&) const
//   bool operator!=(const StringType&) const
//   bool operator< (const StringType&) const  — for std::map key ordering
//   // plus comparisons with string literals / std::string
//
// Other:
//   std::hash<StringType>                  — must be hashable for unordered
//       containers (if ObjectType is unordered_map)
//
// ============================================================================
// ObjectType requirements
// ============================================================================
//
//   template<typename K, typename V, typename... Args> class ObjectType
//
// ObjectType<K, V, Args...> must model an AssociativeContainer (std::map
// concept):
//   - value_type   = std::pair<const K, V>
//   - key_type     = K
//   - mapped_type  = V
//   - iterator, const_iterator (bidirectional)
//   - begin(), end(), find(), insert(), emplace(), erase(), count(), at()
//   - operator[]  (or equivalent)
//   - ObjectType(std::initializer_list<value_type>) constructor
//   - size(), empty()
//
// ============================================================================
// ArrayType requirements
// ============================================================================
//
//   template<typename T, typename... Args> class ArrayType
//
// ArrayType<T, Args...> must model a SequenceContainer (std::vector concept):
//   - value_type = T
//   - iterator, const_iterator
//   - begin(), end(), push_back(), emplace_back(), insert()
//   - operator[], at(), size(), empty(), resize(), reserve(), clear()
//   - ArrayType(std::initializer_list<T>) constructor
//
// ============================================================================
// Plan for Task 3.3.2 — Extending persistent_string to StringType
// ============================================================================
//
// The following methods need to be added to jgit::persistent_string to satisfy
// the StringType requirements:
//
//   1. value_type alias (char)
//   2. size_t npos static constant (= SIZE_MAX or std::string::npos)
//   3. persistent_string(size_t count, char c)       — fill constructor
//   4. char& operator[](size_t idx)
//   5. const char& operator[](size_t idx) const
//   6. void push_back(char c)
//   7. persistent_string& append(const persistent_string&)
//   8. persistent_string& append(const char* s, size_t n)
//   9. template<typename Iter> persistent_string& append(Iter first, Iter last)
//  10. persistent_string& operator+=(const persistent_string&)
//  11. persistent_string& operator+=(char c)
//  12. char* begin()  /  const char* begin() const
//  13. char* end()    /  const char* end()   const
//  14. size_t find(char c, size_t pos = 0) const
//  15. size_t find_first_of(const char* s, size_t pos = 0) const
//  16. persistent_string substr(size_t pos, size_t len = npos) const
//  17. persistent_string(const persistent_string&) — already default
//  18. operator==(const char*), operator<(const persistent_string&) — already present
//
// NOTE: Adding these methods does NOT break std::is_trivially_copyable —
// member functions don't affect trivial copyability (it only depends on
// special member functions: constructors, destructor, copy/move assign).
// We DO need to be careful about:
//   - The fill constructor: if it is the ONLY user-provided constructor, it
//     might prevent implicit default constructor generation.  We keep the
//     existing default constructor explicitly.
//   - push_back / append: these modify the buffer, which is fine for a
//     fixed-size struct.  If len + 1 > LONG_BUF_SIZE the call is a no-op
//     (data clamped); document this limitation.
//
// std::is_trivially_copyable WILL be preserved if:
//   - All special member functions remain trivial (default/=default).
//   - We add only ordinary member functions.
//   - We add no virtual functions.
//   - We add no non-trivial base classes.
//
// The fill constructor `persistent_string(size_t, char)` IS a user-provided
// constructor, so it makes the type no longer "trivially default constructible".
// However it does NOT affect std::is_trivially_copyable, which only checks
// the COPY/MOVE constructor and destructor.
//
// std::is_trivially_copyable check after all additions:
//   - Copy constructor: =default  (trivial) ✓
//   - Destructor:       implicitly trivial ✓
//   - Copy assign:      =default ✓
//   → std::is_trivially_copyable remains TRUE.
//
// ============================================================================

// This file is intentionally a documentation/experiment file.
// It can be compiled by including the relevant headers and adding a main().

#include <string>
#include <type_traits>
#include <iostream>

// Demonstrate what properties std::string has for reference:
static_assert(!std::is_trivially_copyable<std::string>::value,
              "std::string is NOT trivially copyable — confirms we need a custom type");

int main()
{
    std::cout << "std::is_trivially_copyable<std::string>: "
              << std::is_trivially_copyable<std::string>::value << "\n";
    return 0;
}
