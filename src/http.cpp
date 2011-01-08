//! \file http.cpp Defines elements of the HTTP protocol
/***************************************************************************
* Copyright (C) 2007 Eddie Carle [eddie@erctech.org]                       *
*                                                                          *
* This file is part of fastcgi++.                                          *
*                                                                          *
* fastcgi++ is free software: you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as  published   *
* by the Free Software Foundation, either version 3 of the License, or (at *
* your option) any later version.                                          *
*                                                                          *
* fastcgi++ is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or    *
* FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public     *
* License for more details.                                                *
*                                                                          *
* You should have received a copy of the GNU Lesser General Public License *
* along with fastcgi++.  If not, see <http://www.gnu.org/licenses/>.       *
****************************************************************************/


#include <algorithm>
#include <boost/date_time/posix_time/posix_time.hpp>

#include <fastcgi++/http.hpp>

#include "utf8_codecvt.hpp"

void Fastcgipp::Http::Address::assign(const char* start, const char* end)
{
	data=0;
	for(int i=24; i>=0; i-=8)
	{
		char* point=(char*)memchr(start, '.', end-start);
		data|=atoi(start, end)<<i;
		if(!point || point+1>=end) break;
		start=point+1;
	}
}

template std::basic_ostream<char, std::char_traits<char> >& Fastcgipp::Http::operator<< <char, std::char_traits<char> >(std::basic_ostream<char, std::char_traits<char> >& os, const Address& address);
template std::basic_ostream<wchar_t, std::char_traits<wchar_t> >& Fastcgipp::Http::operator<< <wchar_t, std::char_traits<wchar_t> >(std::basic_ostream<wchar_t, std::char_traits<wchar_t> >& os, const Address& address);
template<class charT, class Traits> std::basic_ostream<charT, Traits>& Fastcgipp::Http::operator<<(std::basic_ostream<charT, Traits>& os, const Address& address)
{
	using namespace std;
	if(!os.good()) return os;
	
	try
	{
		typename basic_ostream<charT, Traits>::sentry opfx(os);
		if(opfx)
		{
			streamsize fieldWidth=os.width(0);
			charT buffer[20];
			charT* bufPtr=buffer;
			locale loc(os.getloc(), new num_put<charT, charT*>);

			for(uint32_t mask=0xff000000, shift=24; mask!=0; mask>>=8, shift-=8)
			{
				bufPtr=use_facet<num_put<charT, charT*> >(loc).put(bufPtr, os, os.fill(), static_cast<long unsigned int>((address.data&mask)>>shift));
				*bufPtr++=os.widen('.');
			}
			--bufPtr;

			charT* ptr=buffer;
			ostreambuf_iterator<charT,Traits> sink(os);
			if(os.flags() & ios_base::left)
				for(int i=max(fieldWidth, bufPtr-buffer); i>0; i--)
				{
					if(ptr!=bufPtr) *sink++=*ptr++;
					else *sink++=os.fill();
				}
			else
				for(int i=fieldWidth-(bufPtr-buffer); ptr!=bufPtr;)
				{
					if(i>0) { *sink++=os.fill(); --i; }
					else *sink++=*ptr++;
				}

			if(sink.failed()) os.setstate(ios_base::failbit);
		}
	}
	catch(bad_alloc&)
	{
		ios_base::iostate exception_mask = os.exceptions();
		os.exceptions(ios_base::goodbit);
		os.setstate(ios_base::badbit);
		os.exceptions(exception_mask);
		if(exception_mask & ios_base::badbit) throw;
	}
	catch(...)
	{
		ios_base::iostate exception_mask = os.exceptions();
		os.exceptions(ios_base::goodbit);
		os.setstate(ios_base::failbit);
		os.exceptions(exception_mask);
		if(exception_mask & ios_base::failbit) throw;
	}
	return os;
}

template std::basic_istream<char, std::char_traits<char> >& Fastcgipp::Http::operator>> <char, std::char_traits<char> >(std::basic_istream<char, std::char_traits<char> >& is, Address& address);
template std::basic_istream<wchar_t, std::char_traits<wchar_t> >& Fastcgipp::Http::operator>> <wchar_t, std::char_traits<wchar_t> >(std::basic_istream<wchar_t, std::char_traits<wchar_t> >& is, Address& address);
template<class charT, class Traits> std::basic_istream<charT, Traits>& Fastcgipp::Http::operator>>(std::basic_istream<charT, Traits>& is, Address& address)
{
	using namespace std;
	if(!is.good()) return is;

	ios_base::iostate err = ios::goodbit;
	try
	{
		typename basic_istream<charT, Traits>::sentry ipfx(is);
		if(ipfx)
		{
			uint32_t data=0;
			istreambuf_iterator<charT, Traits> it(is);
			for(int i=24; i>=0; i-=8, ++it)
			{
				uint32_t value;
				use_facet<num_get<charT, istreambuf_iterator<charT, Traits> > >(is.getloc()).get(it, istreambuf_iterator<charT, Traits>(), is, err, value);
				data|=value<<i;
				if(i && *it!=is.widen('.')) err = ios::failbit;
			}
			if(err == ios::goodbit) address=data;
			else is.setstate(err);
		}
	}
	catch(bad_alloc&)
	{
		ios_base::iostate exception_mask = is.exceptions();
		is.exceptions(ios_base::goodbit);
		is.setstate(ios_base::badbit);
		is.exceptions(exception_mask);
		if(exception_mask & ios_base::badbit) throw;
	}
	catch(...)
	{
		ios_base::iostate exception_mask = is.exceptions();
		is.exceptions(ios_base::goodbit);
		is.setstate(ios_base::failbit);
		is.exceptions(exception_mask);
		if(exception_mask & ios_base::failbit) throw;
	}

	return is;
}

void Fastcgipp::Http::charToString(const char* data, size_t size, std::wstring& string)
{
	const size_t bufferSize=512;
	wchar_t buffer[bufferSize];
	using namespace std;

	if(size)
	{
		codecvt_base::result cr=codecvt_base::partial;
		while(cr==codecvt_base::partial)
		{{
			wchar_t* it;
			const char* tmpData;
			mbstate_t conversionState = mbstate_t();
			cr=use_facet<codecvt<wchar_t, char, mbstate_t> >(locale(locale::classic(), new utf8CodeCvt::utf8_codecvt_facet)).in(conversionState, data, data+size, tmpData, buffer, buffer+bufferSize, it);
			string.append(buffer, it);
			size-=tmpData-data;
			data=tmpData;
		}}
		if(cr==codecvt_base::error) throw Exceptions::CodeCvt();
	}
}

int Fastcgipp::Http::atoi(const char* start, const char* end)
{
	bool neg=false;
	if(*start=='-')
	{
		neg=false;
		++start;
	}
	int result=0;
	for(; 0x30 <= *start && *start <= 0x39 && start<end; ++start)
		result=result*10+(*start&0x0f);

	return neg?-result:result;
}

template size_t Fastcgipp::Http::percentEscapedToRealBytes<char>(const char* source, char* destination, size_t size);
template size_t Fastcgipp::Http::percentEscapedToRealBytes<wchar_t>(const wchar_t* source, wchar_t* destination, size_t size);
template<class charT> size_t Fastcgipp::Http::percentEscapedToRealBytes(const charT* source, charT* destination, size_t size)
{
	if (size < 1) return 0;

	unsigned int i=0;
	charT* start=destination;
	while(1)
	{
		if(*source=='%')
		{
			*destination=0;
			for(int shift=4; shift>=0; shift-=4)
			{
				if(++i==size) break;
				++source;
				if((*source|0x20) >= 'a' && (*source|0x20) <= 'f')
					*destination|=((*source|0x20)-0x57)<<shift;
				else if(*source >= '0' && *source <= '9')
					*destination|=(*source&0x0f)<<shift;
			}
			++source;
			++destination;
		}
		else if(*source=='+')
		{
			*destination++=' ';
			++source;
		}
		else
			*destination++=*source++;

		if(++i==size) break;
	}
	return destination-start;
}

template void Fastcgipp::Http::Environment<char>::fill(const char* data, size_t size);
template void Fastcgipp::Http::Environment<wchar_t>::fill(const char* data, size_t size);
template<class charT> void Fastcgipp::Http::Environment<charT>::fill(const char* data, size_t size)
{
	using namespace std;
	using namespace boost;

	while(size)
	{{
		size_t nameSize;
		size_t valueSize;
		const char* name;
		const char* value;
		Protocol::processParamHeader(data, size, name, nameSize, value, valueSize);
		size-=value-data+valueSize;
		data=value+valueSize;

		switch(nameSize)
		{
		case 9:
			if(!memcmp(name, "HTTP_HOST", 9))
				charToString(value, valueSize, host);
			else if(!memcmp(name, "PATH_INFO", 9))
				charToString(value, valueSize, pathInfo);
			break;
		case 11:
			if(!memcmp(name, "HTTP_ACCEPT", 11))
				charToString(value, valueSize, acceptContentTypes);
			else if(!memcmp(name, "HTTP_COOKIE", 11))
				decodeUrlEncoded(value, valueSize, cookies, ';');
			else if(!memcmp(name, "SERVER_ADDR", 11))
				serverAddress.assign(value, value+valueSize);
			else if(!memcmp(name, "REMOTE_ADDR", 11))
				remoteAddress.assign(value, value+valueSize);
			else if(!memcmp(name, "SERVER_PORT", 11))
				serverPort=atoi(value, value+valueSize);
			else if(!memcmp(name, "REMOTE_PORT", 11))
				remotePort=atoi(value, value+valueSize);
			else if(!memcmp(name, "SCRIPT_NAME", 11))
				charToString(value, valueSize, scriptName);
			else if(!memcmp(name, "REQUEST_URI", 11))
				charToString(value, valueSize, requestUri);
			break;
		case 12:
			if(!memcmp(name, "HTTP_REFERER", 12) && valueSize)
				charToString(value, valueSize, referer);
			else if(!memcmp(name, "CONTENT_TYPE", 12))
			{
				const char* end=(char*)memchr(value, ';', valueSize);
				charToString(value, end?end-value:valueSize, contentType);
				if(end)
				{
					const char* start=(char*)memchr(end, '=', valueSize-(end-data));
					if(start)
					{
						boundarySize=valueSize-(++start-value);
						boundary.reset(new char[boundarySize]);
						memcpy(boundary.get(), start, boundarySize);
					}
				}
			}
			else if(!memcmp(name, "QUERY_STRING", 12) && valueSize)
				decodeUrlEncoded(value, valueSize, gets);
			break;
		case 13:
			if(!memcmp(name, "DOCUMENT_ROOT", 13))
				charToString(value, valueSize, root);
			break;
		case 14:
			if(!memcmp(name, "REQUEST_METHOD", 14))
			{
				requestMethod = HTTP_METHOD_ERROR;
				switch(valueSize)
				{
				case 3:
					if(!memcmp(value, requestMethodLabels[HTTP_METHOD_GET], 3)) requestMethod = HTTP_METHOD_GET;
					else if(!memcmp(value, requestMethodLabels[HTTP_METHOD_PUT], 3)) requestMethod = HTTP_METHOD_PUT;
					break;
				case 4:
					if(!memcmp(value, requestMethodLabels[HTTP_METHOD_HEAD], 4)) requestMethod = HTTP_METHOD_HEAD;
					else if(!memcmp(value, requestMethodLabels[HTTP_METHOD_POST], 4)) requestMethod = HTTP_METHOD_POST;
					break;
				case 5:
					if(!memcmp(value, requestMethodLabels[HTTP_METHOD_TRACE], 5)) requestMethod = HTTP_METHOD_TRACE;
					break;
				case 6:
					if(!memcmp(value, requestMethodLabels[HTTP_METHOD_DELETE], 6)) requestMethod = HTTP_METHOD_DELETE;
					break;
				case 7:
					if(!memcmp(value, requestMethodLabels[HTTP_METHOD_OPTIONS], 7)) requestMethod = HTTP_METHOD_OPTIONS;
					else if(!memcmp(value, requestMethodLabels[HTTP_METHOD_OPTIONS], 7)) requestMethod = HTTP_METHOD_CONNECT;
					break;
				}
			}
			else if(!memcmp(name, "CONTENT_LENGTH", 14))
				contentLength=atoi(value, value+valueSize);
			break;
		case 15:
			if(!memcmp(name, "HTTP_USER_AGENT", 15))
				charToString(value, valueSize, userAgent);
			else if(!memcmp(name, "HTTP_KEEP_ALIVE", 15))
				keepAlive=atoi(value, value+valueSize);
			break;
		case 18:
			if(!memcmp(name, "HTTP_IF_NONE_MATCH", 18))
				etag=atoi(value, value+valueSize);
			break;
		case 19:
			if(!memcmp(name, "HTTP_ACCEPT_CHARSET", 19))
				charToString(value, valueSize, acceptCharsets);
			break;
		case 20:
			if(!memcmp(name, "HTTP_ACCEPT_LANGUAGE", 20))
				charToString(value, valueSize, acceptLanguages);
			break;
		case 22:
			if(!memcmp(name, "HTTP_IF_MODIFIED_SINCE", 22))
			{
				stringstream dateStream;
				dateStream.write(value, valueSize);
				dateStream.imbue(locale(locale::classic(), new posix_time::time_input_facet("%a, %d %b %Y %H:%M:%S GMT")));
				dateStream >> ifModifiedSince;
			}
			break;
		}
	}}
}

template void Fastcgipp::Http::Environment<char>::fillPostsMultipart(const char* data, size_t size);
template void Fastcgipp::Http::Environment<wchar_t>::fillPostsMultipart(const char* data, size_t size);
template<class charT> void Fastcgipp::Http::Environment<charT>::fillPostsMultipart(const char* data, size_t size)
{
	using namespace std;
	while(1)
	{{
		size_t bufferSize=postBufferSize+size;
		char* buffer=new char[bufferSize];
		if(postBufferSize) memcpy(buffer, postBuffer.get(), postBufferSize);
		memcpy(buffer+postBufferSize, data, size);
		postBuffer.reset(buffer);
		postBufferSize=bufferSize;

		const char* end=0;
		for(const char* i=buffer+boundarySize; i<buffer+bufferSize-boundarySize; ++i)
			if(!memcmp(i, boundary.get(), boundarySize))
			{
				end=i;
				break;
			}
		
		if(!end)
			return;

		end-=4;
		const char* start=buffer+boundarySize+2;
		const char* bodyStart=start;
		for(; bodyStart<=end-4; ++bodyStart)
			if(!memcmp(bodyStart, "\r\n\r\n", 4)) break;
		bodyStart+=4;


		const char* contentTypeStart;
		ssize_t contentTypeSize=-1;
		const char* nameStart;
		ssize_t nameSize=-1;
		const char* filenameStart;
		ssize_t filenameSize=-1;
		// Fill out above values
		{
			const char cContentType[] = "Content-Type: ";
			const char cContentDisposition[] = "Content-Disposition: ";
			const char cFilename[] = "filename=\"";
			const char cName[] = "name=\"";

			enum ParseState { TITLE, CONTENT_DISPOSITION, FILENAME, NAME, CONTENT_TYPE, SKIP } parseState=TITLE;
			for(const char* i=start; i<bodyStart; ++i)
			{
				switch(*i)
				{
					case '\n':
					case '\r':
					{
						if(parseState==CONTENT_TYPE)
							contentTypeSize=i-contentTypeStart;
						parseState=TITLE;
						break;
					}

					case '"':
					{
						if(parseState==FILENAME)
						{
							filenameSize=i-filenameStart;
							parseState=CONTENT_DISPOSITION;
						}
						else if(parseState==NAME)
						{
							nameSize=i-nameStart;
							parseState=CONTENT_DISPOSITION;
						}
						break;
					}

					default:
					{
						if(parseState==TITLE)
						{
							if(sizeof(cContentType)-1 <= bodyStart-i && !memcmp(cContentType, i, sizeof(cContentType)-1))
							{
								parseState=CONTENT_TYPE;
								i += sizeof(cContentType)-1;
								contentTypeStart=i;
							}
							else if(sizeof(cContentDisposition)-1 <= bodyStart-i && !memcmp(cContentDisposition, i, sizeof(cContentDisposition)-1))
							{
								parseState=CONTENT_DISPOSITION;
								i += sizeof(cContentDisposition)-1;
							}
							else
								parseState=SKIP;
						}
						else if(parseState==CONTENT_DISPOSITION)
						{
							if(sizeof(cFilename)-1 <= bodyStart-i && !memcmp(cFilename, i, sizeof(cFilename)-1))
							{
								parseState=FILENAME;
								i += sizeof(cFilename)-1;
								filenameStart=i;
							}
							else if(sizeof(cName)-1 <= bodyStart-i && !memcmp(cName, i, sizeof(cName)-1))
							{
								parseState=NAME;
								i += sizeof(cName)-1;
								nameStart=i;
							}
						}
						break;
					}
				}
			}
		}

		
		if(nameSize != -1)
		{
			basic_string<charT> name;
			charToString(nameStart, nameSize, name);

			Post<charT>& thePost=posts[name];
			if(contentTypeSize != -1)
			{
				thePost.type=Post<charT>::file;
				charToString(contentTypeStart, contentTypeSize, thePost.contentType);
				if(filenameSize != -1) charToString(filenameStart, filenameSize, thePost.filename);
				thePost.size=end-bodyStart;
				if(thePost.size)
				{
					thePost.data.reset(new char[thePost.size]);
					memcpy(thePost.data.get(), bodyStart, thePost.size);
				}
			}
			else
			{
				thePost.type=Post<charT>::form;
				charToString(bodyStart, end-bodyStart, thePost.value);
			}
		}

		bufferSize=bufferSize-(end-buffer+2);
		if(!bufferSize)
		{
			postBuffer.reset();
			return;
		}
		buffer=new char[bufferSize];
		memcpy(buffer, end+2, bufferSize);
		postBuffer.reset(buffer);
		postBufferSize=bufferSize;
		size=0;
	}}
}

template void Fastcgipp::Http::Environment<char>::fillPostsUrlEncoded(const char* data, size_t size);
template void Fastcgipp::Http::Environment<wchar_t>::fillPostsUrlEncoded(const char* data, size_t size);
template<class charT> void Fastcgipp::Http::Environment<charT>::fillPostsUrlEncoded(const char* data, size_t size)
{
	using namespace std;

	if(!postBuffer.get() && size)
	{
		postBuffer.reset(new char[contentLength]);
		postBufferSize=0;
	}

	// We don't want a buffer overflow so only process a max length of processLength
	size=min(size, contentLength-postBufferSize);

	memcpy(postBuffer.get()+postBufferSize, data, size);
	postBufferSize+=size;

	if(postBufferSize != contentLength)
		return;

	char* nameStart=postBuffer.get();
	size_t nameSize;
	char* valueStart=0;
	size_t valueSize;
	for(char* i=postBuffer.get(); i<=postBuffer.get()+postBufferSize; ++i)
	{
		if(*i == '=' && nameStart && !valueStart)
		{
			nameSize=percentEscapedToRealBytes(nameStart, nameStart, i-nameStart);
			valueStart=i+1;
		}
		else if( (i==postBuffer.get()+postBufferSize || *i == '&') && nameStart && valueStart)
		{
			valueSize=percentEscapedToRealBytes(valueStart, valueStart, i-valueStart);

			basic_string<charT> name;
			charToString(nameStart, nameSize, name);
			nameStart=i+1;
			Post<charT>& thePost=posts[name];
			thePost.type=Post<charT>::form;
			charToString(valueStart, valueSize, thePost.value);
			valueStart=0;
		}
	}
}

bool Fastcgipp::Http::SessionId::seeded=false;

Fastcgipp::Http::SessionId::SessionId()
{
	if(!seeded)
	{
		std::srand(boost::posix_time::microsec_clock::universal_time().time_of_day().fractional_seconds());
		seeded=true;
	}

	for(char* i=data; i<data+size; ++i)
		*i=char(rand()%256);
	timestamp = boost::posix_time::second_clock::universal_time();
}

template const Fastcgipp::Http::SessionId& Fastcgipp::Http::SessionId::operator=<const char>(const char* data_);
template const Fastcgipp::Http::SessionId& Fastcgipp::Http::SessionId::operator=<const wchar_t>(const wchar_t* data_);
template<class charT> const Fastcgipp::Http::SessionId& Fastcgipp::Http::SessionId::operator=(charT* data_)
{
	std::memset(data, 0, size);
	base64Decode(data_, data_+size*4/3, data);
	timestamp = boost::posix_time::second_clock::universal_time();
	return *this;
}

template void Fastcgipp::Http::decodeUrlEncoded<char>(const char* data, size_t size, std::map<std::basic_string<char>, std::basic_string<char> >& output, const char fieldSeperator);
template void Fastcgipp::Http::decodeUrlEncoded<wchar_t>(const char* data, size_t size, std::map<std::basic_string<wchar_t>, std::basic_string<wchar_t> >& output, const char fieldSeperator);
template<class charT> void Fastcgipp::Http::decodeUrlEncoded(const char* data, size_t size, std::map<std::basic_string<charT>, std::basic_string<charT> >& output, const char fieldSeperator)
{
	using namespace std;

	boost::scoped_array<char> buffer(new char[size]);
	memcpy(buffer.get(), data, size);

	char* nameStart=buffer.get();
	size_t nameSize;
	char* valueStart=0;
	size_t valueSize;
	for(char* i=buffer.get(); i<=buffer.get()+size; ++i)
	{
		if(*i == ' ' && nameStart && !valueStart)
			++nameStart;

		else if(*i == '=' && nameStart && !valueStart)
		{
			nameSize=percentEscapedToRealBytes(nameStart, nameStart, i-nameStart);
			valueStart=i+1;
		}
		else if( (i==buffer.get()+size || *i == fieldSeperator) && nameStart && valueStart)
		{
			valueSize=percentEscapedToRealBytes(valueStart, valueStart, i-valueStart);

			basic_string<charT> name;
			charToString(nameStart, nameSize, name);
			nameStart=i+1;
			basic_string<charT>& value=output[name];
			charToString(valueStart, valueSize, value);
			valueStart=0;
		}
	}
}

const char Fastcgipp::Http::base64Characters[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
const char* Fastcgipp::Http::requestMethodLabels[]= {
	"ERROR",
	"HEAD",
	"GET",
	"POST",
	"PUT",
	"DELETE",
	"TRACE",
	"OPTIONS",
	"CONNECT"
};
