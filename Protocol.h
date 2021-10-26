////////////////////////////////////////////////////////////////////////////////
//      This file defines macros for Protocol class creation.
//
//      You can simply declare your Protocol classes like this:
//
//      class Protocol( Control )
//          Method( signal, Args_2( int, double ) );
//          Method( data, Args_2( const char*, unsigned ) );
//          Method( test, Args_10( int, int, int, int, int, int, int, int, int, int ) );
//      };
//
//      Where:
//              Control - is the name of your Protocol class
//              (in result name will be ControlProtocol)
//              signal, data, test - are names of protocol methods
//              Args_NN - macros for method's attributes definition
//              (NN - is a number of method's arguments)
////////////////////////////////////////////////////////////////////////////////
#ifndef __PROTOCOL_H__
#define __PROTOCOL_H__

template< typename _T > struct argument { typedef _T type; };

//template< class _A, class _B >
//class ArgPack
//{
//public:
//	ArgPack( );
//};
//
//template< class _A, class _B, class _C >
//class ArgPack
//{
//public:
//	ArgPack( );
//};

//template< typename a, typename b > struct lenght;
//
//template< typename a, template< typename _T > argument<char> > struct lenght
//{
//	enum { value = 1 };
//};


////////////////////////////////////////////////////////////////////////////////
//      Args_NN MACROS
////////////////////////////////////////////////////////////////////////////////

#define Args_0()
#define Args_1(a)                       argument<##a >::type(arg1)
#define Args_2(a,b)                     Args_1(a), argument<##b >::type(arg2)
#define Args_3(a,b,c)                   Args_2(a,b), argument<##c >::type(arg3)
#define Args_4(a,b,c,d)                 Args_3(a,b,c), argument<##d >::type(arg4)
#define Args_5(a,b,c,d,e)               Args_4(a,b,c,d), argument<##e >::type(arg5)
#define Args_6(a,b,c,d,e,f)             Args_5(a,b,c,d,e), argument<##f >::type(arg6)
#define Args_7(a,b,c,d,e,f,g)           Args_6(a,b,c,d,e,f), argument<##g >::type(arg7)
#define Args_8(a,b,c,d,e,f,g,h)         Args_7(a,b,c,d,e,f,g), argument<##h >::type(arg8)
#define Args_9(a,b,c,d,e,f,g,h,i)       Args_8(a,b,c,d,e,f,g,h), argument<##i >::type(arg9)
#define Args_10(a,b,c,d,e,f,g,h,i,j)    Args_9(a,b,c,d,e,f,g,h,i), argument<##j >::type(arg10)

////////////////////////////////////////////////////////////////////////////////
//      Method MACRO
////////////////////////////////////////////////////////////////////////////////

#define Method( Name, Args ) protected:virtual bool Name##(Args){try{if(Receiver){return Receiver->##Name##(Args);}else{return false;}}catch(...){Exception(ProtocolName(), #Name );return false;}}

////////////////////////////////////////////////////////////////////////////////
//      Protocol MACRO - for local interoperation
////////////////////////////////////////////////////////////////////////////////

#define Protocol( Name ) Name##Protocol{private:const char* ProtocolName() const{ return "##Name##Protocol";} Name##Protocol* Receiver;protected:Name##Protocol():Receiver(NULL){} ~##Name##Protocol(){}public:void Connect( Name##Protocol* Receiver ){this->Receiver=Receiver;}void Connect( Name##Protocol& Receiver ){this->Receiver=&Receiver;}virtual void Exception(const char* which_protocol, const char* which_method){Receiver=NULL;}

////////////////////////////////////////////////////////////////////////////////
//      HostProtocol MACRO - for asynchronous replies
////////////////////////////////////////////////////////////////////////////////

#define HostProtocol( Name ) Name##HostProtocol{private:const char* ProtocolName() const{ return "##Name##HostProtocol";} Name##HostProtocol* Receiver;protected:Name##HostProtocol(Name##HostProtocol* HostReceiver=NULL):Receiver(HostReceiver){} ~##Name##HostProtocol(){}virtual void Exception(const char* which_protocol, const char* which_method){Receiver=NULL;}

#endif
