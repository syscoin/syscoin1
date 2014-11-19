// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2012 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "init.h"
#include "util.h"
#include "sync.h"
#include "ui_interface.h"
#include "base58.h"
#include "bitcoinhttp.h"
#include "db.h"

#include <boost/asio.hpp>
#include <boost/asio/ip/v6_only.hpp>
#include <boost/bind.hpp>
#include <boost/filesystem.hpp>
#include <boost/foreach.hpp>
#include <boost/iostreams/concepts.hpp>
#include <boost/iostreams/stream.hpp>
#include <boost/algorithm/string.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/filesystem/fstream.hpp>
#include <boost/shared_ptr.hpp>
#include <list>

using namespace std;
using namespace boost;
using namespace boost::asio;
using namespace json_spirit;

static std::string strHTTPUserColonPass;

static const CHTTPContentType vHTTPContentTypes[] = {
    {"htm",  "text/html"},
    {"html", "text/html"},
    {"css",  "text/css"},
    {"txt",  "text/plain"},
    {"log",  "text/plain"},
    {"png",  "image/png"},
    {"jpg",  "image/jpeg"},
    {"gif",  "image/gif"},
    {"js",   "text/javascript"},
	{"otf", "application/x-font-opentype"},
	{"ttf", "application/x-font-ttf"},
	{"eot", "application/vnd.ms-fontobject"},
	{"woff", "application/font-woff"}
};
static const CHTTPContentType defaultContentType = vHTTPContentTypes[0];

// These are created by StartHTTPThreads, destroyed in StopHTTPThreads
static asio::io_service* http_io_service = NULL;
static ssl::context* http_ssl_context = NULL;
static boost::thread_group* http_worker_group = NULL;

static inline unsigned short GetDefaultHTTPPort()
{
    // TODO CP read from config
    return 8080;
}

static string GetContentType(const string& file) {
    vector<string> vWords;
    boost::split(vWords, file, boost::is_any_of("."));
    if (vWords.size() < 2)
        return defaultContentType.type;
    string ext = vWords[vWords.size()-1];

    BOOST_FOREACH(CHTTPContentType type, vHTTPContentTypes) {
        if(type.ext == ext)
            return type.type;
    }
    return defaultContentType.type;
}

static string GetHTTPErrorResponse(string strMsg, int nStatus = 200, string cStatus = "OK") {
    return strprintf(
            "HTTP/1.1 %d %s\r\n"
            "Date: %s\r\n"
            "Connection: close\r\n"
            "Content-Length: %"PRIszu"\r\n"
            "Content-Type: text/html\r\n"
            "Server: syscoin-http/%s\r\n"
            "\r\n"
            "%s",
        nStatus,
        cStatus.c_str(),
        rfc1123Time().c_str(),
        strMsg.size(),
        FormatFullVersion().c_str(),
        strMsg.c_str());
}

static string HTTPReplyWithContentType(int nStatus, const string& strMsg, bool keepalive, const string& strContentType = defaultContentType.type )
{
    const char *cStatus;

    if (nStatus == HTTP_OK) cStatus = "OK";
    else if (nStatus == HTTP_UNAUTHORIZED)
        return GetHTTPErrorResponse(
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>401 Unauthorized.</H1></BODY>\r\n"
            "</HTML>\r\n", 401, "Unauthorized");
    else if (nStatus == HTTP_BAD_REQUEST) 
        return GetHTTPErrorResponse(
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>400 Bad Request.</H1></BODY>\r\n"
            "</HTML>\r\n", 400, "Bad Request");
    else if (nStatus == HTTP_FORBIDDEN) 
        return GetHTTPErrorResponse(
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>403 Forbidden.</H1></BODY>\r\n"
            "</HTML>\r\n", 403, "Forbidden");
    else if (nStatus == HTTP_NOT_FOUND) 
        return GetHTTPErrorResponse(
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>404 Not Found.</H1></BODY>\r\n"
            "</HTML>\r\n", 404, "Not Found");
    else if (nStatus == HTTP_INTERNAL_SERVER_ERROR) 
        return GetHTTPErrorResponse(
            "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01 Transitional//EN\"\r\n"
            "\"http://www.w3.org/TR/1999/REC-html401-19991224/loose.dtd\">\r\n"
            "<HTML>\r\n"
            "<HEAD>\r\n"
            "<TITLE>Error</TITLE>\r\n"
            "<META HTTP-EQUIV='Content-Type' CONTENT='text/html; charset=ISO-8859-1'>\r\n"
            "</HEAD>\r\n"
            "<BODY><H1>500 Internal Server Error.</H1></BODY>\r\n"
            "</HTML>\r\n", 500, "Internal Server Error");
    else cStatus = "";
	if(strContentType == "image/png" || strContentType == "image/jpeg" || strContentType == "image/gif") {
        // deal with adding gzip to header here       
    }
    string headerAndDataStr = strprintf(
            "HTTP/1.1 %d %s\r\n"
            "Date: %s\r\n"
            "Connection: %s\r\n"
            "Content-Length: %"PRIszu"\r\n"
            "Content-Type: %s\r\n"
            "Server: syscoin-http/%s\r\n"
            "\r\n",
        nStatus,
        cStatus,
        rfc1123Time().c_str(),
        keepalive ? "keep-alive" : "close",
        strMsg.size(),
        strContentType.c_str(),
        FormatFullVersion().c_str());
    headerAndDataStr += strMsg;
	return headerAndDataStr;
}

Object HTTPError(int code, const string& message)
{
    Object error;
    error.push_back(Pair("code", code));
    error.push_back(Pair("message", message));
    return error;
}

void HTTPErrorReply(std::ostream& stream, const Object& objError, const Value& id)
{
    // Send error reply from json-rpc error object
    int nStatus = HTTP_INTERNAL_SERVER_ERROR;
    stream << HTTPReplyWithContentType(nStatus, "internal error", false) << std::flush;
}

bool HTTPClientAllowed(const boost::asio::ip::address& address)
{
    // Make sure that IPv4-compatible and IPv4-mapped IPv6 addresses are treated as IPv4 addresses
    if (address.is_v6()
     && (address.to_v6().is_v4_compatible()
      || address.to_v6().is_v4_mapped()))
        return HTTPClientAllowed(address.to_v6().to_v4());

    if (address == asio::ip::address_v4::loopback()
     || address == asio::ip::address_v6::loopback()
     || (address.is_v4()
         // Check whether IPv4 addresses match 127.0.0.0/8 (loopback subnet)
      && (address.to_v4().to_ulong() & 0xff000000) == 0x7f000000))
        return true;

    const string strAddress = address.to_string();
    const vector<string>& vAllow = mapMultiArgs["-httpallowip"];
    BOOST_FOREACH(string strAllow, vAllow)
        if (WildcardMatch(strAddress, strAllow))
            return true;
    return false;
}

//
// IOStream device that speaks SSL but can also speak non-SSL
//
template <typename Protocol>
class SSLIOStreamDevice : public iostreams::device<iostreams::bidirectional> {
public:
    SSLIOStreamDevice(asio::ssl::stream<typename Protocol::socket> &streamIn, bool fUseSSLIn) : stream(streamIn)
    {
        fUseSSL = fUseSSLIn;
        fNeedHandshake = fUseSSLIn;
    }

    void handshake(ssl::stream_base::handshake_type role)
    {
        if (!fNeedHandshake) return;
        fNeedHandshake = false;
        stream.handshake(role);
    }
    std::streamsize read(char* s, std::streamsize n)
    {
        handshake(ssl::stream_base::server); // HTTPS servers read first
        if (fUseSSL) return stream.read_some(asio::buffer(s, n));
        return stream.next_layer().read_some(asio::buffer(s, n));
    }
    std::streamsize write(const char* s, std::streamsize n)
    {
        handshake(ssl::stream_base::client); // HTTPS clients write first
        if (fUseSSL) return asio::write(stream, asio::buffer(s, n));
        return asio::write(stream.next_layer(), asio::buffer(s, n));
    }
    bool connect(const std::string& server, const std::string& port)
    {
        ip::tcp::resolver resolver(stream.get_io_service());
        ip::tcp::resolver::query query(server.c_str(), port.c_str());
        ip::tcp::resolver::iterator endpoint_iterator = resolver.resolve(query);
        ip::tcp::resolver::iterator end;
        boost::system::error_code error = asio::error::host_not_found;
        while (error && endpoint_iterator != end)
        {
            stream.lowest_layer().close();
            stream.lowest_layer().connect(*endpoint_iterator++, error);
        }
        if (error)
            return false;
        return true;
    }

private:
    bool fNeedHandshake;
    bool fUseSSL;
    asio::ssl::stream<typename Protocol::socket>& stream;
};

class AcceptedConnection
{
public:
    virtual ~AcceptedConnection() {}

    virtual std::iostream& stream() = 0;
    virtual std::string peer_address_to_string() const = 0;
    virtual void close() = 0;
};

template <typename Protocol>
class AcceptedConnectionImpl : public AcceptedConnection
{
public:
    AcceptedConnectionImpl(
            asio::io_service& io_service,
            ssl::context &context,
            bool fUseSSL) :
        sslStream(io_service, context),
        _d(sslStream, fUseSSL),
        _stream(_d)
    {
    }

    virtual std::iostream& stream()
    {
        return _stream;
    }

    virtual std::string peer_address_to_string() const
    {
        return peer.address().to_string();
    }

    virtual void close()
    {
        _stream.close();
    }

    typename Protocol::endpoint peer;
    asio::ssl::stream<typename Protocol::socket> sslStream;

private:
    SSLIOStreamDevice<Protocol> _d;
    iostreams::stream< SSLIOStreamDevice<Protocol> > _stream;
};

void ServiceHTTPConnection(AcceptedConnection *conn);

// Forward declaration required for HTTPListen
template <typename Protocol, typename SocketAcceptorService>
static void HTTPAcceptHandler(boost::shared_ptr< basic_socket_acceptor<Protocol, SocketAcceptorService> > acceptor,
                             ssl::context& context,
                             bool fUseSSL,
                             AcceptedConnection* conn,
                             const boost::system::error_code& error);

/**
 * Sets up I/O resources to accept and handle a new connection.
 */
template <typename Protocol, typename SocketAcceptorService>
static void HTTPListen(boost::shared_ptr< basic_socket_acceptor<Protocol, SocketAcceptorService> > acceptor,
                   ssl::context& context,
                   const bool fUseSSL)
{
    // Accept connection
    AcceptedConnectionImpl<Protocol>* conn = new AcceptedConnectionImpl<Protocol>(acceptor->get_io_service(), context, fUseSSL);

    acceptor->async_accept(
            conn->sslStream.lowest_layer(),
            conn->peer,
            boost::bind(&HTTPAcceptHandler<Protocol, SocketAcceptorService>,
                acceptor,
                boost::ref(context),
                fUseSSL,
                conn,
                boost::asio::placeholders::error));
}

/**
 * Accept and handle incoming connection.
 */
template <typename Protocol, typename SocketAcceptorService>
static void HTTPAcceptHandler(boost::shared_ptr< basic_socket_acceptor<Protocol, SocketAcceptorService> > acceptor,
                             ssl::context& context,
                             const bool fUseSSL,
                             AcceptedConnection* conn,
                             const boost::system::error_code& error)
{
    // Immediately start accepting new connections, except when we're cancelled or our socket is closed.
    if (error != asio::error::operation_aborted && acceptor->is_open())
        HTTPListen(acceptor, context, fUseSSL);

    AcceptedConnectionImpl<ip::tcp>* tcp_conn = dynamic_cast< AcceptedConnectionImpl<ip::tcp>* >(conn);

    // TODO: Actually handle errors
    if (error)
    {
        delete conn;
    }

    // Restrict callers by IP.  It is important to
    // do this before starting client thread, to filter out
    // certain DoS and misbehaving clients.
    else if (tcp_conn && !HTTPClientAllowed(tcp_conn->peer.address()))
    {
        // Only send a 403 if we're not using SSL to prevent a DoS during the SSL handshake.
        if (!fUseSSL)
            conn->stream() << HTTPReplyWithContentType(HTTP_FORBIDDEN, "", false) << std::flush;
        delete conn;
    }
    else {
        ServiceHTTPConnection(conn);
        conn->close();
        delete conn;
    }
}

void StartHTTPThreads()
{
    assert(http_io_service == NULL);
    http_io_service = new asio::io_service();
    http_ssl_context = new ssl::context(*http_io_service, ssl::context::sslv23);

    const bool fUseSSL = GetBoolArg("-httpssl");

    if (fUseSSL)
    {
        http_ssl_context->set_options(ssl::context::no_sslv2);

        filesystem::path pathCertFile(GetArg("-httpsslcertificatechainfile", "server.cert"));
        if (!pathCertFile.is_complete()) pathCertFile = filesystem::path(GetDataDir()) / pathCertFile;
        if (filesystem::exists(pathCertFile)) http_ssl_context->use_certificate_chain_file(pathCertFile.string());
        else printf("ThreadHTTPServer ERROR: missing server certificate file %s\n", pathCertFile.string().c_str());

        filesystem::path pathPKFile(GetArg("-httpsslprivatekeyfile", "server.pem"));
        if (!pathPKFile.is_complete()) pathPKFile = filesystem::path(GetDataDir()) / pathPKFile;
        if (filesystem::exists(pathPKFile)) http_ssl_context->use_private_key_file(pathPKFile.string(), ssl::context::pem);
        else printf("ThreadHTTPServer ERROR: missing server private key file %s\n", pathPKFile.string().c_str());

        string strCiphers = GetArg("-httpsslciphers", "TLSv1+HIGH:!SSLv2:!aNULL:!eNULL:!AH:!3DES:@STRENGTH");
        SSL_CTX_set_cipher_list(http_ssl_context->impl(), strCiphers.c_str());
    }

    // Try a dual IPv6/IPv4 socket, falling back to separate IPv4 and IPv6 sockets
    const bool loopback = !mapArgs.count("-httpallowip");
    asio::ip::address bindAddress = loopback ? asio::ip::address_v6::loopback() : asio::ip::address_v6::any();
    ip::tcp::endpoint endpoint(bindAddress, GetArg("-httpport", GetDefaultHTTPPort()));
    boost::system::error_code v6_only_error;
    boost::shared_ptr<ip::tcp::acceptor> acceptor(new ip::tcp::acceptor(*http_io_service));

    bool fListening = false;
    std::string strerr;
    try
    {
        acceptor->open(endpoint.protocol());
        acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));

        // Try making the socket dual IPv6/IPv4 (if listening on the "any" address)
        acceptor->set_option(boost::asio::ip::v6_only(loopback), v6_only_error);

        acceptor->bind(endpoint);
        acceptor->listen(socket_base::max_connections);

        HTTPListen(acceptor, *http_ssl_context, fUseSSL);

        fListening = true;
    }
    catch(boost::system::system_error &e)
    {
        strerr = strprintf(_("An error occurred while setting up the HTTP port %u for listening on IPv6, falling back to IPv4: %s"), endpoint.port(), e.what());
    }

    try {
        // If dual IPv6/IPv4 failed (or we're opening loopback interfaces only), open IPv4 separately
        if (!fListening || loopback || v6_only_error)
        {
            bindAddress = loopback ? asio::ip::address_v4::loopback() : asio::ip::address_v4::any();
            endpoint.address(bindAddress);

            acceptor.reset(new ip::tcp::acceptor(*http_io_service));
            acceptor->open(endpoint.protocol());
            acceptor->set_option(boost::asio::ip::tcp::acceptor::reuse_address(true));
            acceptor->bind(endpoint);
            acceptor->listen(socket_base::max_connections);

            HTTPListen(acceptor, *http_ssl_context, fUseSSL);

            fListening = true;
        }
    }
    catch(boost::system::system_error &e)
    {
        strerr = strprintf(_("An error occurred while setting up the HTTP port %u for listening on IPv4: %s"), endpoint.port(), e.what());
    }

    if (!fListening) {
        uiInterface.ThreadSafeMessageBox(strerr, "", CClientUIInterface::MSG_ERROR);
        StartShutdown();
        return;
    }

    http_worker_group = new boost::thread_group();
    for (int i = 0; i < GetArg("-httpthreads", 4); i++)
        http_worker_group->create_thread(boost::bind(&asio::io_service::run, http_io_service));
}

void StopHTTPThreads()
{
    if (http_io_service == NULL) return;

    http_io_service->stop();
    http_worker_group->join_all();
    delete http_worker_group; http_worker_group = NULL;
    delete http_ssl_context; http_ssl_context = NULL;
    delete http_io_service; http_io_service = NULL;
}

string readFile(const string &fileName)
{
	ifstream ifs(fileName.c_str(), ios::in | ios::binary | ios::ate);

    ifstream::pos_type fileSize = ifs.tellg();
    ifs.seekg(0, ios::beg);

    vector<char> bytes(fileSize);
    ifs.read(&bytes[0], fileSize);

    return string(&bytes[0], fileSize);
}

void ServiceHTTPConnection(AcceptedConnection *conn)
{
    int nProto = 0;
    map<string, string> mapHeaders;
    string strRequest, strMethod, strURI;

    // Read HTTP request line
    if (!ReadHTTPRequestLine(conn->stream(), nProto, strMethod, strURI))
        return;

    // Read HTTP message headers and body
    ReadHTTPMessage(conn->stream(), mapHeaders, strRequest, nProto);

    boost::filesystem::path pathFile = GetDataDir() / "walletui" / strURI;

    if(boost::filesystem::is_directory(pathFile.string())) {
        pathFile = GetDataDir() / "walletui" / strURI / "index.html";
    }

    if(!boost::filesystem::exists(pathFile.string())) {
        conn->stream() << HTTPReplyWithContentType(HTTP_NOT_FOUND, "", false) << std::flush;
        return;
    }

    string fileOut = readFile(pathFile.string());
    try
    {
        conn->stream() << HTTPReplyWithContentType(HTTP_OK, fileOut, false, GetContentType(pathFile.string())) << std::flush;
        printf("HTTPServer 200 %s %s\n", strMethod.c_str(), strURI.c_str());
    }
    catch (Object& objError)
    {
        HTTPErrorReply(conn->stream(), objError, Value::null);
    }
    catch (std::exception& e)
    {
        HTTPErrorReply(conn->stream(), HTTPError(-500, e.what()), Value::null);
    }
}

