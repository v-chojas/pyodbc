
#include "pyodbc.h"
#include "dbspecific.h"

void PrintBytes(void* p, size_t len)
{
    unsigned char* pch = (unsigned char*)p;
    for (size_t i = 0; i < len; i++)
        printf("%02x ", (int)pch[i]);
    printf("\n");
}

#define _MAKESTR(n) case n: return #n
const char* SqlTypeName(SQLSMALLINT n)
{
    switch (n)
    {
        _MAKESTR(SQL_UNKNOWN_TYPE);
        _MAKESTR(SQL_CHAR);
        _MAKESTR(SQL_VARCHAR);
        _MAKESTR(SQL_LONGVARCHAR);
        _MAKESTR(SQL_NUMERIC);
        _MAKESTR(SQL_DECIMAL);
        _MAKESTR(SQL_INTEGER);
        _MAKESTR(SQL_SMALLINT);
        _MAKESTR(SQL_FLOAT);
        _MAKESTR(SQL_REAL);
        _MAKESTR(SQL_DOUBLE);
        _MAKESTR(SQL_DATETIME);
        _MAKESTR(SQL_WCHAR);
        _MAKESTR(SQL_WVARCHAR);
        _MAKESTR(SQL_WLONGVARCHAR);
        _MAKESTR(SQL_TYPE_DATE);
        _MAKESTR(SQL_TYPE_TIME);
        _MAKESTR(SQL_TYPE_TIMESTAMP);
        _MAKESTR(SQL_SS_TIME2);
        _MAKESTR(SQL_SS_XML);
        _MAKESTR(SQL_BINARY);
        _MAKESTR(SQL_VARBINARY);
        _MAKESTR(SQL_LONGVARBINARY);
    }
    return "unknown";
}

const char* CTypeName(SQLSMALLINT n)
{
    switch (n)
    {
        _MAKESTR(SQL_C_CHAR);
        _MAKESTR(SQL_C_WCHAR);
        _MAKESTR(SQL_C_LONG);
        _MAKESTR(SQL_C_SHORT);
        _MAKESTR(SQL_C_FLOAT);
        _MAKESTR(SQL_C_DOUBLE);
        _MAKESTR(SQL_C_NUMERIC);
        _MAKESTR(SQL_C_DEFAULT);
        _MAKESTR(SQL_C_DATE);
        _MAKESTR(SQL_C_TIME);
        _MAKESTR(SQL_C_TIMESTAMP);
        _MAKESTR(SQL_C_TYPE_DATE);
        _MAKESTR(SQL_C_TYPE_TIME);
        _MAKESTR(SQL_C_TYPE_TIMESTAMP);
        _MAKESTR(SQL_C_INTERVAL_YEAR);
        _MAKESTR(SQL_C_INTERVAL_MONTH);
        _MAKESTR(SQL_C_INTERVAL_DAY);
        _MAKESTR(SQL_C_INTERVAL_HOUR);
        _MAKESTR(SQL_C_INTERVAL_MINUTE);
        _MAKESTR(SQL_C_INTERVAL_SECOND);
        _MAKESTR(SQL_C_INTERVAL_YEAR_TO_MONTH);
        _MAKESTR(SQL_C_INTERVAL_DAY_TO_HOUR);
        _MAKESTR(SQL_C_INTERVAL_DAY_TO_MINUTE);
        _MAKESTR(SQL_C_INTERVAL_DAY_TO_SECOND);
        _MAKESTR(SQL_C_INTERVAL_HOUR_TO_MINUTE);
        _MAKESTR(SQL_C_INTERVAL_HOUR_TO_SECOND);
        _MAKESTR(SQL_C_INTERVAL_MINUTE_TO_SECOND);
        _MAKESTR(SQL_C_BINARY);
        _MAKESTR(SQL_C_BIT);
        _MAKESTR(SQL_C_SBIGINT);
        _MAKESTR(SQL_C_UBIGINT);
        _MAKESTR(SQL_C_TINYINT);
        _MAKESTR(SQL_C_SLONG);
        _MAKESTR(SQL_C_SSHORT);
        _MAKESTR(SQL_C_STINYINT);
        _MAKESTR(SQL_C_ULONG);
        _MAKESTR(SQL_C_USHORT);
        _MAKESTR(SQL_C_UTINYINT);
        _MAKESTR(SQL_C_GUID);
    }
    return "unknown";
}


#ifdef PYODBC_TRACE
void DebugTrace(const char* szFmt, ...)
{
    va_list marker;
    va_start(marker, szFmt);
    vprintf(szFmt, marker);
    va_end(marker);
}
#endif

#ifdef PYODBC_LEAK_CHECK

// These structures are not thread safe!  They are intended for use while holding the GC lock.

enum {
    MAGIC = 0xE3E3E3E3
};

struct BlockHeader
{
    unsigned int magic;
    // BlockHeader* phdrPrev;
    BlockHeader* next;
    const char* filename;
    int lineno;
    size_t len;
    byte pb[0];
};

static BlockHeader* header_list = 0;
// All allocated block headers in a linked list.

inline void AddToList(BlockHeader* phdr)
{
    phdr->next = header_list;
    header_list = phdr;
}

inline BlockHeader** FindInList(BlockHeader* phdr)
{
    // Search the list for the pointer that points to phdr.  Return 0 if not found.
    BlockHeader** pp = &header_list;
    while (*pp != 0 && *pp != phdr)
        pp = &((*pp)->next);
    return pp;
}

void RemoveFromList(BlockHeader* phdr)
{
    // Find the pointer to `phdr` and set it to the item after phdr.
    BlockHeader** pp = FindInList(phdr);
    I(pp);
    *pp = phdr->next;
}

void DumpList()
{
    printf("\nLIST\n");
    BlockHeader* p = header_list;
    while (p)
    {
        printf(" - hdr=%p %s(%d) ptr=%p\n", p, p->filename, p->lineno, p->pb);
        p = p->next;
    }
}

void* _pyodbc_malloc(const char* filename, int lineno, size_t len)
{
    BlockHeader* phdr = (BlockHeader*)malloc(len + sizeof(BlockHeader));
    if (phdr == 0)
        return 0;

    phdr->magic    = MAGIC;
    phdr->filename = filename;
    phdr->lineno   = lineno;
    phdr->len      = len;
    memset(phdr->pb, 0xCD, len);

    // printf("malloc: header=%p ptr=%p %s %d\n", phdr, phdr->pb, filename, lineno);
    AddToList(phdr);

    I(HeaderFromAlloc(phdr->pb) == phdr);

    DumpList();

    return phdr->pb;
}

inline BlockHeader* HeaderFromAlloc(void* p)
{
    // REVIEW: Redo this.  I did it all in one expression before and messed something up.
    byte* pT = (byte*)p;
    pT -= offsetof(BlockHeader, pb);
    BlockHeader* phdr = (BlockHeader*)pT;
    if (phdr->magic != MAGIC)
    {
        printf("INVALID BLOCK!!!");
        PrintBytes(phdr, sizeof(BlockHeader));
        return 0;
    }
    return phdr;
}

void* _pyodbc_realloc(const char* filename, int lineno, void* p, size_t len)
{
    if (p == 0)
        return _pyodbc_malloc(filename, lineno, len);

    if (len == 0)
    {
        pyodbc_free(p);
        return 0;
    }

    // We're going to always reallocate.


    void* pbNew = _pyodbc_malloc(filename, lineno, len);
    if (!pbNew)
    {
        pyodbc_free(p);
        return 0;
    }

    BlockHeader* phdrOld = p ? HeaderFromAlloc(p) : 0;
    BlockHeader* phdrNew = HeaderFromAlloc(pbNew);

    I(p == 0 || phdrHold); // make sure we print something

    if (phdrOld)
    {
        memcpy(pbNew, p, min(phdrOld->len, phdrNew->len));
    }

    pyodbc_free(p);

    return pbNew;
}

void _pyodbc_free(const char* filename, int lineno, void* p)
{
    if (p == 0)
        return;

    // printf("free start %p list=%p\n", p, header_list);
    BlockHeader* phdr = HeaderFromAlloc(p);
    if (phdr)
    {
        // printf("free: %p %s %d\n", p, phdr->filename, phdr->lineno);
        RemoveFromList(phdr);
        free(phdr);
    }
    else
    {
        printf("ERROR: free of invalid pointer at %s(%d) ptr=%p\n", filename, lineno, p);
    }
    DumpList();
}

char* _pyodbc_strdup(const char* filename, int lineno, const char* sz)
{
    size_t cch = strlen(sz);
    char* pch = (char*)_pyodbc_malloc(filename, lineno, cch+1);
    if (!pch)
        return 0;
    memcpy(pch, sz, cch+1); // +1 for the NULL terminator
    return pch;
}

void pyodbc_leak_check()
{
    if (header_list == 0)
    {
        printf("NO LEAKS\n");
    }
    else
    {
        printf("********************************************************************************\n");
        BlockHeader* phdr = header_list;
        while (phdr)
        {
            printf("LEAK: %s(%d) len=%lu\n", phdr->filename, phdr->lineno, phdr->len);
            phdr = phdr->next;
        }
    }
}

#endif
