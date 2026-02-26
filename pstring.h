#pragma once
#include "persist.h"
#include <cstring>
#include <algorithm>

// pstring — персистная строка.
//
// Заголовок строки (pstring_data) тривиально копируем и может быть сохранён
// через persist<pstring_data>. Символьные данные хранятся в ПАП через fptr<char>.
//
// Ограничения:
//   - pstring_data тривиально копируема (без виртуальных функций, без нетривиальных
//     конструкторов/деструкторов).
//   - Символы хранятся в ПАП через AddressManager<char>.
//   - Нулевой терминатор всегда записывается последним символом.
//   - Пустая/нулевая pstring имеет chars.addr() == 0 и length == 0.
//
// Phase 3: поле length изменено с unsigned на uintptr_t для полной совместимости
//   с Phase 2 PAM API (PersistentAddressSpace использует uintptr_t для всех смещений).
//
// Использование:
//   pstring_data sd{};
//   pstring ps(sd);
//   ps.assign("hello");
//   // sd.chars.addr() содержит смещение в ПАП; sd.length == 5
//   // При следующем запуске загрузите sd из persist<pstring_data> и оберните снова.

/// Заголовок персистной строки (тривиально копируем).
struct pstring_data
{
    uintptr_t  length;   ///< Число символов (без учёта нулевого терминатора); uintptr_t для совместимости с Phase 2
    fptr<char> chars;    ///< Смещение в ПАП для массива символов; 0 = пусто
};

static_assert(std::is_trivially_copyable<pstring_data>::value,
              "pstring_data должна быть тривиально копируемой для использования с persist<T>");

// Phase 3: проверяем, что оба поля имеют размер void* (uintptr_t) для согласованности с Phase 2.
static_assert(sizeof(pstring_data::length) == sizeof(void*),
              "pstring_data::length должен иметь размер void* (Phase 3)");
static_assert(sizeof(pstring_data) == 2 * sizeof(void*),
              "pstring_data должна занимать 2 * sizeof(void*) байт (Phase 3)");

// pstring — тонкая не-владеющая обёртка над ссылкой pstring_data.
// Владелец pstring_data — вызывающий код (как правило, persist<pstring_data>).
class pstring
{
    pstring_data& _d;

public:
    explicit pstring(pstring_data& data) : _d(data) {}

    // assign: сохранить C-строку в ПАП.
    // Освобождает предыдущий массив символов (если есть), затем выделяет новый.
    void assign(const char* s)
    {
        if( _d.chars.addr() != 0 )
        {
            _d.chars.DeleteArray();
        }
        if( s == nullptr || s[0] == '\0' )
        {
            _d.length = 0;
            return;
        }
        uintptr_t len = static_cast<uintptr_t>(std::strlen(s));
        _d.length = len;
        // Выделяем len+1 символов (включая нулевой терминатор).
        _d.chars.NewArray(static_cast<unsigned>(len + 1));
        for( uintptr_t i = 0; i <= len; i++ )
            _d.chars[static_cast<unsigned>(i)] = s[i];
    }

    // c_str: вернуть raw-указатель на символьные данные.
    // Действителен, пока AddressManager<char> жив и слот не освобождён.
    const char* c_str() const
    {
        if( _d.chars.addr() == 0 ) return "";
        return &(_d.chars[0]);
    }

    uintptr_t size() const { return _d.length; }
    bool      empty() const { return _d.length == 0; }

    // clear: освободить символьные данные и обнулить длину.
    void clear()
    {
        if( _d.chars.addr() != 0 )
        {
            _d.chars.DeleteArray();
        }
        _d.length = 0;
    }

    // operator[]: доступ к символу по индексу (без проверки границ).
    char& operator[](uintptr_t idx)       { return _d.chars[static_cast<unsigned>(idx)]; }
    const char& operator[](uintptr_t idx) const { return _d.chars[static_cast<unsigned>(idx)]; }

    bool operator==(const char* s) const
    {
        if( s == nullptr ) return _d.length == 0;
        return std::strcmp(c_str(), s) == 0;
    }

    bool operator==(const pstring& other) const
    {
        if( _d.length != other._d.length ) return false;
        if( _d.length == 0 ) return true;
        return std::strcmp(c_str(), other.c_str()) == 0;
    }

    bool operator!=(const char* s) const { return !(*this == s); }
    bool operator!=(const pstring& other) const { return !(*this == other); }

    bool operator<(const pstring& other) const
    {
        return std::strcmp(c_str(), other.c_str()) < 0;
    }
};
