#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <sstream>
#include <iostream>
#include <ostream>
#include <vector>

namespace Chromaprint
{

std::ostream noop_ostream(NULL);

};

#ifndef NDEBUG

/// This class is a derivate of basic_stringbuf which will output all the written data using the OutputDebugString function
template<typename TChar, typename TTraits = std::char_traits<TChar>>
class OutputDebugStringBuf : public std::basic_stringbuf<TChar,TTraits>
{

public:
	OutputDebugStringBuf() : _buffer(1024)
	{
		setg(nullptr, nullptr, nullptr);
		setp(&_buffer[0], &_buffer[0], &_buffer[0] + _buffer.size());
	}

	~OutputDebugStringBuf()
	{
	}

	int sync()
	{
		try
		{
			MessageOutputer<TChar,TTraits>()(pbase(), pptr());
			setp(&_buffer[0], &_buffer[0], &_buffer[0] + _buffer.size());
			return 0;
		}
		catch(...)
		{
			return -1;
		}
	}

	std::char_traits<char>::int_type overflow(std::char_traits<char>::int_type c = TTraits::eof())
	{
		int syncRet = sync();
		if (c != TTraits::eof())
		{
			_buffer[0] = c;
			setp(&_buffer[0], &_buffer[0] + 1, &_buffer[0] + _buffer.size());
		}
		return syncRet == -1 ? TTraits::eof() : 0;
	}

private:
	std::vector<TChar> _buffer;

	template<typename TChar, typename TTraits> struct MessageOutputer;

	template<> struct MessageOutputer<char,std::char_traits<char>>
	{
		template<typename TIterator> void operator()(TIterator begin, TIterator end) const
		{
			std::string s(begin, end);
			OutputDebugStringA(s.c_str());
		}
	};

	template<> struct MessageOutputer<wchar_t,std::char_traits<wchar_t>>
	{
		template<typename TIterator> void operator()(TIterator begin, TIterator end) const
		{
			std::wstring s(begin, end);
			OutputDebugStringW(s.c_str());
		}
	};
};

static OutputDebugStringBuf<char> charDebugOutput;
static OutputDebugStringBuf<wchar_t> wcharDebugOutput;
#endif

BOOL APIENTRY DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
#ifndef NDEBUG
		std::cerr.rdbuf(&charDebugOutput);
		std::wcerr.rdbuf(&wcharDebugOutput);
#endif
		break;

	case DLL_THREAD_ATTACH:
	case DLL_THREAD_DETACH:
	case DLL_PROCESS_DETACH:
		break;
	}
	return TRUE;
}

