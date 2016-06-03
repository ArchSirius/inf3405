#undef UNICODE

#include <winsock2.h>
#include <iostream>
#include <algorithm>
#include <array>
#include <strstream>
#include <fstream>
#include <map>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <string>
#include <sstream>
#include <memory>

using namespace std;

// link with Ws2_32.lib
#pragma comment( lib, "ws2_32.lib" )

typedef struct {
	SOCKET socket;
	string ip;
	USHORT port;
} socketParams;

// External functions
extern DWORD WINAPI voteProcessing(void* sd_);
string itos(int i);
void onServerBoot();

// Returns the candidate for which the client has voted for.  Returns false if vote is not valid.
bool processVote(const array<char, 64>& voteBuffer);

void logInfo(socketParams* params, bool valid);

// Variables
SOCKET sd;
const chrono::minutes DURATION(90);
chrono::time_point<chrono::system_clock> endTime;
const char* logFile = "journal.txt";
const char* candaidatesList = "Liste_des_candidats.txt";
map<int, string> candidates;
map<int, int> votes;
string strCandidates = "";
int nbVotes;


// List of Winsock error constants mapped to an interpretation string.
// Note that this list must remain sorted by the error constants'
// values, because we do a binary search on the list when looking up
// items.
static struct ErrorEntry {
    int nID;
    const char* pcMessage;

    ErrorEntry(int id, const char* pc = 0) : 
    nID(id), 
    pcMessage(pc) 
    { 
    }

    bool operator<(const ErrorEntry& rhs) const
    {
        return nID < rhs.nID;
    }
} gaErrorList[] = {
    ErrorEntry(0,                  "No error"),
    ErrorEntry(WSAEINTR,           "Interrupted system call"),
    ErrorEntry(WSAEBADF,           "Bad file number"),
    ErrorEntry(WSAEACCES,          "Permission denied"),
    ErrorEntry(WSAEFAULT,          "Bad address"),
    ErrorEntry(WSAEINVAL,          "Invalid argument"),
    ErrorEntry(WSAEMFILE,          "Too many open sockets"),
    ErrorEntry(WSAEWOULDBLOCK,     "Operation would block"),
    ErrorEntry(WSAEINPROGRESS,     "Operation now in progress"),
    ErrorEntry(WSAEALREADY,        "Operation already in progress"),
    ErrorEntry(WSAENOTSOCK,        "Socket operation on non-socket"),
    ErrorEntry(WSAEDESTADDRREQ,    "Destination address required"),
    ErrorEntry(WSAEMSGSIZE,        "Message too long"),
    ErrorEntry(WSAEPROTOTYPE,      "Protocol wrong type for socket"),
    ErrorEntry(WSAENOPROTOOPT,     "Bad protocol option"),
    ErrorEntry(WSAEPROTONOSUPPORT, "Protocol not supported"),
    ErrorEntry(WSAESOCKTNOSUPPORT, "Socket type not supported"),
    ErrorEntry(WSAEOPNOTSUPP,      "Operation not supported on socket"),
    ErrorEntry(WSAEPFNOSUPPORT,    "Protocol family not supported"),
    ErrorEntry(WSAEAFNOSUPPORT,    "Address family not supported"),
    ErrorEntry(WSAEADDRINUSE,      "Address already in use"),
    ErrorEntry(WSAEADDRNOTAVAIL,   "Can't assign requested address"),
    ErrorEntry(WSAENETDOWN,        "Network is down"),
    ErrorEntry(WSAENETUNREACH,     "Network is unreachable"),
    ErrorEntry(WSAENETRESET,       "Net connection reset"),
    ErrorEntry(WSAECONNABORTED,    "Software caused connection abort"),
    ErrorEntry(WSAECONNRESET,      "Connection reset by peer"),
    ErrorEntry(WSAENOBUFS,         "No buffer space available"),
    ErrorEntry(WSAEISCONN,         "Socket is already connected"),
    ErrorEntry(WSAENOTCONN,        "Socket is not connected"),
    ErrorEntry(WSAESHUTDOWN,       "Can't send after socket shutdown"),
    ErrorEntry(WSAETOOMANYREFS,    "Too many references, can't splice"),
    ErrorEntry(WSAETIMEDOUT,       "Connection timed out"),
    ErrorEntry(WSAECONNREFUSED,    "Connection refused"),
    ErrorEntry(WSAELOOP,           "Too many levels of symbolic links"),
    ErrorEntry(WSAENAMETOOLONG,    "File name too long"),
    ErrorEntry(WSAEHOSTDOWN,       "Host is down"),
    ErrorEntry(WSAEHOSTUNREACH,    "No route to host"),
    ErrorEntry(WSAENOTEMPTY,       "Directory not empty"),
    ErrorEntry(WSAEPROCLIM,        "Too many processes"),
    ErrorEntry(WSAEUSERS,          "Too many users"),
    ErrorEntry(WSAEDQUOT,          "Disc quota exceeded"),
    ErrorEntry(WSAESTALE,          "Stale NFS file handle"),
    ErrorEntry(WSAEREMOTE,         "Too many levels of remote in path"),
    ErrorEntry(WSASYSNOTREADY,     "Network system is unavailable"),
    ErrorEntry(WSAVERNOTSUPPORTED, "Winsock version out of range"),
    ErrorEntry(WSANOTINITIALISED,  "WSAStartup not yet called"),
    ErrorEntry(WSAEDISCON,         "Graceful shutdown in progress"),
    ErrorEntry(WSAHOST_NOT_FOUND,  "Host not found"),
    ErrorEntry(WSANO_DATA,         "No host data of that type was found")
};
const int kNumMessages = sizeof(gaErrorList) / sizeof(ErrorEntry);


//// WSAGetLastErrorMessage ////////////////////////////////////////////
// A function similar in spirit to Unix's perror() that tacks a canned 
// interpretation of the value of WSAGetLastError() onto the end of a
// passed string, separated by a ": ".  Generally, you should implement
// smarter error handling than this, but for default cases and simple
// programs, this function is sufficient.
//
// This function returns a pointer to an internal static buffer, so you
// must copy the data from this function before you call it again.  It
// follows that this function is also not thread-safe.
const char* WSAGetLastErrorMessage(const char* pcMessagePrefix, int nErrorID = 0)
{
    // Build basic error string
    static char acErrorBuffer[256];
    ostrstream outs(acErrorBuffer, sizeof(acErrorBuffer));
    outs << pcMessagePrefix << ": ";

    // Tack appropriate canned message onto end of supplied message 
    // prefix. Note that we do a binary search here: gaErrorList must be
	// sorted by the error constant's value.
	ErrorEntry* pEnd = gaErrorList + kNumMessages;
    ErrorEntry Target(nErrorID ? nErrorID : WSAGetLastError());
    ErrorEntry* it = lower_bound(gaErrorList, pEnd, Target);
    if ((it != pEnd) && (it->nID == Target.nID)) {
        outs << it->pcMessage;
    }
    else {
        // Didn't find error in list, so make up a generic one
        outs << "unknown error";
    }
    outs << " (" << Target.nID << ")";

    // Finish error message off and return it.
    outs << ends;
    acErrorBuffer[sizeof(acErrorBuffer) - 1] = '\0';
    return acErrorBuffer;
}

int main(void) 
{
	//----------------------
	// Initialize Winsock.
	WSADATA wsaData;
	int iResult = WSAStartup(MAKEWORD(2,2), &wsaData);
	if (iResult != NO_ERROR) {
		cerr << "Error at WSAStartup()\n" << endl;
		return 1;
	}

	//----------------------
	// Create a SOCKET for listening for
	// incoming connection requests.
	SOCKET ServerSocket;
	ServerSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (ServerSocket == INVALID_SOCKET) {
        cerr << WSAGetLastErrorMessage("Error at socket()") << endl;
		WSACleanup();
		return 1;
	}
	char* option = "1";
	setsockopt(ServerSocket, SOL_SOCKET, SO_REUSEADDR, option, sizeof(option));

    //----------------------
    // The sockaddr_in structure specifies the address family,
    // IP address, and port for the socket that is being bound.
	int port=5000;
    
	//Recuperation de l'adresse locale
	hostent *thisHost;
	thisHost=gethostbyname("127.0.0.1");
	char* ip;
	ip=inet_ntoa(*(struct in_addr*) *thisHost->h_addr_list);
	printf("Adresse locale trouvee %s : \n\n",ip);
	sockaddr_in service;
    service.sin_family = AF_INET;
    //service.sin_addr.s_addr = inet_addr("127.0.0.1");
	//	service.sin_addr.s_addr = INADDR_ANY;
	service.sin_addr.s_addr = inet_addr(ip);
    service.sin_port = htons(port);

    if (bind(ServerSocket, (SOCKADDR*) &service, sizeof(service)) == SOCKET_ERROR) {
		cerr << WSAGetLastErrorMessage("bind() failed.") << endl;
		closesocket(ServerSocket);
		WSACleanup();
		return 1;
	}
	
	//----------------------
	// Listen for incoming connection requests.
	// on the created socket
	if (listen(ServerSocket, 30) == SOCKET_ERROR) {
		cerr << WSAGetLastErrorMessage("Error listening on socket.") << endl;
		closesocket(ServerSocket);
		WSACleanup();
		return 1;
	}


	printf("En attente des connections des clients sur le port %d...\n\n",ntohs(service.sin_port));

	onServerBoot();

    while (true) {	

		sockaddr_in sinRemote;
		int nAddrSize = sizeof(sinRemote);
		// Create a SOCKET for accepting incoming requests.
		// Accept the connection.
		SOCKET sd = accept(ServerSocket, (sockaddr*)&sinRemote, &nAddrSize);
        if (sd != INVALID_SOCKET) {
			cout << "Connection acceptee De : " <<
                    inet_ntoa(sinRemote.sin_addr) << ":" <<
                    ntohs(sinRemote.sin_port) << "." <<
                    endl;

			socketParams params{ sd, inet_ntoa(sinRemote.sin_addr), ntohs(sinRemote.sin_port) };
            DWORD nThreadID;
			CreateThread(0, 0, voteProcessing, (void*) &params, 0, &nThreadID);
        }
        else {
            cerr << WSAGetLastErrorMessage("Echec d'une connection.") << 
                    endl;
           // return 1;
        }
    }


  
    // No longer need server socket
	closesocket(ServerSocket);

    WSACleanup();
    return 0;
}


//// EchoHandler ///////////////////////////////////////////////////////
// Handles the incoming data by reflecting it back to the sender.

DWORD WINAPI voteProcessing(void* sd_) 
{
	auto params = reinterpret_cast<socketParams*>(sd_);
	SOCKET sd = (SOCKET)params->socket;
	send(sd, strCandidates.c_str(), strCandidates.size(), 0);

	// Receive vote
	std::array<char, 64> voteBuffer;

	recv(sd, voteBuffer.data(), voteBuffer.size(), 0);
	
	bool valid = processVote(voteBuffer);
	logInfo(params, valid);

	const char* validVote = "1";
	send(sd, validVote, 1, 0);

	closesocket(sd);

	return 0;
}

void onServerBoot() {
	// Read the candidates file
	ifstream candidatesFile(candaidatesList);

	// Cleanup logfile
	ofstream file;
	file.open(logFile, std::ofstream::out | std::ofstream::trunc);
	file.clear();
	file.close();

	// Insert the data in map candidates
	while (!candidatesFile.eof() && !candidatesFile.fail())
	{
		string candidate;
		getline(candidatesFile, candidate, ';');
		candidate.erase(std::remove(candidate.begin(), candidate.end(), '\n'), candidate.end());
		auto nb = candidates.size();
		if (candidate != "") {
			candidates.emplace(nb, candidate);
			votes.emplace(nb, 0);
			strCandidates += itos(nb) + " : " + candidates.at(nb) + "\n";
		}
	}
}

bool processVote(const array<char, 64>& voteBuffer)
{
	// Trunk unecessary characters in the buffer
	auto delimiterPos = std::find(voteBuffer.begin(), voteBuffer.end(), '\0');

	// Check if vote is valid
	bool valid = false;
	if (all_of(voteBuffer.begin(), delimiterPos, [](char c) { return c >= '0' && c <= '9'; }))
	{
		std::string candidateStr = { voteBuffer.begin(), delimiterPos };

		auto candidateId = stoi(candidateStr);
		if (candidates.find(candidateId) != candidates.end())
		{
			votes.at(candidateId)++;
			valid = true;
		}
	}
	nbVotes++;
	return valid;
}

void logInfo(socketParams* params, bool valid) {
	ofstream file;
	file.open(logFile, std::ofstream::out | std::ofstream::app);
	string ip = params->ip;
	string port = itos(params->port);
	auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
	file << std::put_time(std::localtime(&now), "%F %T") << " -- " << ip << ":" << port << " -- " << (valid ? "valide" : "invalide") << endl;
	file.close();
}

void onPollClosed() {
	// If no connections, onPollOver
	// If connections, wait
}

void onPollOver() {
	
}

void onServerShutdown() {

}

bool onConnectionRequest() {
	// Initialize countdown if not initialized
	if (endTime.time_since_epoch().count() == 0) {
		endTime = chrono::system_clock::now() + DURATION;
	}
	// Accept connection if poll is open
	if (chrono::system_clock::now().time_since_epoch().count() < endTime.time_since_epoch().count()) {
		return true;
	}
	return false;
}

void onConnectionAccepted(void* sd_) {
	
}

void onConnectionRefused() {
	// Send error message
}

void onVote() {
	// Get elector info
	// Add vote to votes map
	// Add info to log
	// Disconnect elector
}

void onDisconnect() {
	// Send message
}

string itos(int i) {
	stringstream ss;
	ss << i;
	string s;
	ss >> s;
	return s;
}

// onConnectionRequest() ? onConnectionAccepted() : onConnectionRefused();
