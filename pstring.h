#pragma once
#include "persist.h"
#include <cstring>
#include <algorithm>

// pstring — персистная строка, аналог std::string.
//
// Объекты pstring могут находиться ТОЛЬКО внутри ПАП.
// Для работы с pstring из обычного кода используйте fptr<pstring>.
//
// Требования:
//   - Конструктор и деструктор приватные; создание только через ПАМ (Тр.2, Тр.11).
//   - При загрузке образа ПАП конструкторы не вызываются (Тр.10).
//   - Символьные данные хранятся в ПАП через fptr<char>.
//   - Нулевой терминатор всегда записывается последним символом.
//   - Пустая/нулевая pstring имеет chars.addr() == 0 и length == 0.
//
// Phase 3: поле length имеет тип uintptr_t для полной совместимости
//   с Phase 2 PAM API (PersistentAddressSpace использует uintptr_t для всех смещений).
//
// Использование:
//   fptr<pstring> fps;
//   fps.New();                  // выделяем pstring в ПАП (нулевая инициализация)
//   fps->assign("hello");
//   // fps->c_str() == "hello"
//   fps.Delete();

struct pstring
{
    uintptr_t  length;   ///< Число символов (без учёта нулевого терминатора); uintptr_t для совместимости с Phase 2
    fptr<char> chars;    ///< Смещение в ПАП для массива символов; 0 = пусто

    // assign: сохранить C-строку в ПАП.
    // Освобождает предыдущий массив символов (если есть), затем выделяет новый.
    void assign(const char* s)
    {
        if( chars.addr() != 0 )
        {
            chars.DeleteArray();
        }
        if( s == nullptr || s[0] == '\0' )
        {
            length = 0;
            return;
        }
        uintptr_t len = static_cast<uintptr_t>(std::strlen(s));
        length = len;
        // Выделяем len+1 символов (включая нулевой терминатор).
        chars.NewArray(static_cast<unsigned>(len + 1));
        for( uintptr_t i = 0; i <= len; i++ )
            chars[static_cast<unsigned>(i)] = s[i];
    }

    // c_str: вернуть raw-указатель на символьные данные.
    // Действителен, пока ПАМ жив и слот не освобождён.
    const char* c_str() const
    {
        if( chars.addr() == 0 ) return "";
        return &(chars[0]);
    }

    uintptr_t size() const { return length; }
    bool      empty() const { return length == 0; }

    // clear: освободить символьные данные и обнулить длину.
    void clear()
    {
        if( chars.addr() != 0 )
        {
            chars.DeleteArray();
        }
        length = 0;
    }

    // operator[]: доступ к символу по индексу (без проверки границ).
    char& operator[](uintptr_t idx)       { return chars[static_cast<unsigned>(idx)]; }
    const char& operator[](uintptr_t idx) const { return chars[static_cast<unsigned>(idx)]; }

    bool operator==(const char* s) const
    {
        if( s == nullptr ) return length == 0;
        return std::strcmp(c_str(), s) == 0;
    }

    bool operator==(const pstring& other) const
    {
        if( length != other.length ) return false;
        if( length == 0 ) return true;
        return std::strcmp(c_str(), other.c_str()) == 0;
    }

    bool operator!=(const char* s) const { return !(*this == s); }
    bool operator!=(const pstring& other) const { return !(*this == other); }

    bool operator<(const pstring& other) const
    {
        return std::strcmp(c_str(), other.c_str()) < 0;
    }

private:
    // Создание pstring на стеке или как статической переменной запрещено.
    // Используйте fptr<pstring>::New() для создания в ПАП (Тр.11).
    pstring() = default;
    ~pstring() = default;

    // Разрешаем доступ к приватному конструктору только для фабричных методов ПАМ.
    template<class U, unsigned A> friend class AddressManager;
    friend class PersistentAddressSpace;
};

// Phase 3: проверяем, что оба поля имеют размер void* (uintptr_t) для согласованности с Phase 2.
static_assert(sizeof(pstring::length) == sizeof(void*),
              "pstring::length должен иметь размер void* (Phase 3)");
static_assert(sizeof(pstring) == 2 * sizeof(void*),
              "pstring должна занимать 2 * sizeof(void*) байт (Phase 3)");

// Примечание: pstring НЕ является тривиально копируемым (private конструктор/деструктор),
// но это допустимо, поскольку ПАМ выделяет сырую память без вызова конструкторов (Тр.10).
// pstring хранится в ПАП как сырые байты, без вызова конструктора/деструктора.
