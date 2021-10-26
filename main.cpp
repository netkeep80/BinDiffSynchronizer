#include "protocol.h"
#include "persist.h"
#include "BinDiffSynchronizer.h"
#include "StaticPageDevice.h"


//typedef persist< fptr< double > > pfptr_double;
//pfptr_double a;

struct page
{
    char ptr[1024];
};

/*
template
< 
	unsigned PageSize = 16, 
	unsigned PoolSize = 16,
	unsigned SpaceSize = 8,
	template< class, unsigned, unsigned > class CachePolicy
>
class PageDevice
*/
StaticPageDevice<16,16,8,Cache> SPD;

bool	test1( void )
{
	int	p, b;
	unsigned char* Data;
	// заполним все страницы их номерами, а потом прочитаем и проверим в прямом порядке
	for( p = 0; p < 256; p++ )
	{
		Data = SPD.GetData( p, true )->Data;
		for( b = 0; b < 1 << 16; b++ )
			Data[b] = p;
	}

	for( p = 0; p < 256; p++ )
	{
		Data = SPD.GetData( p, true )->Data;
		for( b = 0; b < 1 << 16; b++ )
			if( Data[b] != p ) return false;
	}

	return true;
}

bool	test2( void )
{
	int	p, b;
	unsigned char* Data;
	// заполним все страницы их номерами, а потом прочитаем и проверим в встречном порядке
	for( p = 0; p < 256; p++ )
	{
		Data = SPD.GetData( p, true )->Data;
		for( b = 0; b < 1 << 16; b++ )
			Data[b] = p;
	}

	for( p = 0; p < 256; p++ )
	{
		Data = SPD.GetData( 255-p, true )->Data;
		for( b = 0; b < 1 << 16; b++ )
			if( Data[b] != 255-p ) return false;
	}

	return true;
}


MemoryDevice<24, 16, 16, Cache, StaticPageDevice> CSMD;

bool	test3( void )
{
	unsigned Address, Data;
	// заполним все страницы их номерами, а потом прочитаем и проверим в прямом порядке
	for( Address = 0; Address < (1 << 24); Address+=4 )
		CSMD.Write( Address, (unsigned char*)&Address, sizeof(Address) );
	
	for( Address = 1<<16; Address < (1 << 24); Address+=4 )
	{
		CSMD.Read( Address, (unsigned char*)&Data, sizeof(Data) );
		if( Data != Address )
		{
			return false;
		}
	}

	return true;
}

/////////////////////////////////////////////////////////////////////////////////////////////////
#define CHECK(name)									\
	if( name() ) cout << #name << ": OK\n";			\
	else { cout << #name << ": FAIL\n"; return; }

void	main( void )
{
	//CHECK( test1 );
	//CHECK( test2 );
	//CHECK( test3 );
	
	//unsigned char* p1ptr;
	//p1ptr = SPD.GetData( 1, true )->Data;

    fptr< page > p1 = "page.p1";

    if( p1 == NULL )
    {
        p1.New("page.p1");
        for( int i = 0; i < 1024; i++ )
            p1->ptr[i] = 0;
    }

    fptr< page > p2 = "page.p2";
    
    if( p2 == NULL )
    {
        p2.New("page.p2");
        for( int i = 0; i < 1024; i++ )
            p2->ptr[i] = 0;
    }
 
    for( int i = 0; i < 1024; i++ )
        p1->ptr[i] ++;

    fptr< double >  a("main.a");
 
    if( a == NULL )
    {
        a.New("main.a");
        *a = 0.0;
    }
    *a += 1.0;
    cout << *a << endl;
}
