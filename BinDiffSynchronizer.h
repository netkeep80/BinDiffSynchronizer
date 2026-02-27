

template <class _Type> class BinDiffSynchronizer
{
    _Type*        Ptr;
    unsigned char OldState[sizeof( _Type )];

  public:
    BinDiffSynchronizer( _Type* ptr )
    {
        Ptr = ptr;
        memcpy( OldState, ptr, sizeof( _Type ) );
    };

    ~BinDiffSynchronizer()
    {
        if ( Server )
            Server->SendObjChange( OldState, (unsigned char*)Ptr, _Type::ClassName() );
    };
};

#define BinDiffSynchronize() BinDiffSynchronizer<typeof( *this )> MethodVisor( this );